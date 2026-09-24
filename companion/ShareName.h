#pragma once
// Names of the per-client shared objects.
//
// Every channel used to build its own copy of this loop into a caller-supplied
// wchar_t* with no capacity, so the only thing keeping the writes in bounds was
// every call site happening to declare a large enough array. The builder below
// takes a capacity, refuses to produce a truncated name, and ships an array
// overload that takes the capacity from the type so a call site cannot get it
// wrong.

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace rtx::ipc {

// Capacity every IPC name buffer is declared with. Sized for the longest
// prefix, a decimal pid, and the per-session suffix appended later.
inline constexpr std::size_t kNameChars = 128;

// ---- Session key ----------------------------------------------------
// Names carry a per-session value in addition to the pid. Keys are kept per
// client pid, because the launcher serves several clients at once and each one
// gets its own session: a single process-wide key would name every client's
// channels with the same value. The module only ever holds the key for its own
// pid. While no key is set for a pid no name is produced at all: a name anyone
// could work out from the pid is one another program could take first.
//
// The suffix is a keyed hash of the channel's own prefix, not the key itself,
// so seeing one channel's name in the object directory says nothing about the
// name of any other.

namespace detail {

struct SessionEntry {
    std::uint32_t pid = 0;
    std::uint8_t  key[16]{};
    bool          used = false;
    std::uint64_t seq  = 0;      // insertion order, for reclaiming the oldest
};

// Fixed capacity: no allocation on the paths the injected module runs, and
// more clients than anyone docks at once.
inline constexpr std::size_t kMaxSessions = 16;

struct SessionState {
    std::mutex    mu;
    SessionEntry  slots[kMaxSessions];
    std::uint32_t gen = 0;      // bumped on every change so holders can rebind
    std::uint64_t seq = 0;      // monotonic, stamps each slot as it is filled
};

inline SessionState& State() {
    static SessionState s;
    return s;
}

// Caller holds the lock.
inline SessionEntry* FindLocked(SessionState& st, std::uint32_t pid) {
    for (auto& e : st.slots)
        if (e.used && e.pid == pid) return &e;
    return nullptr;
}

}   // namespace detail

// Returns the new generation. Setting the same key again is a no-op and
// returns the current generation, so repeated handshakes do not churn handles.
inline std::uint32_t SetSessionKey(std::uint32_t pid, const std::uint8_t* key, std::size_t len) {
    auto& st = detail::State();
    std::lock_guard<std::mutex> lk(st.mu);
    if (!key || len != sizeof(detail::SessionEntry::key) || pid == 0) return st.gen;

    if (auto* e = detail::FindLocked(st, pid)) {
        bool same = true;
        for (std::size_t i = 0; i < len; ++i)
            if (e->key[i] != key[i]) { same = false; break; }
        if (same) return st.gen;
        for (std::size_t i = 0; i < len; ++i) e->key[i] = key[i];
        e->seq = ++st.seq;
        return ++st.gen;
    }

    detail::SessionEntry* slot = nullptr;
    for (auto& e : st.slots)
        if (!e.used) { slot = &e; break; }

    // Full: take the slot that has been held longest. A client that went away
    // never releases its entry, and declining here instead would leave the
    // launcher on the legacy names while the module had already moved to the
    // new ones, which kills every channel for that client.
    if (!slot) {
        slot = &st.slots[0];
        for (auto& e : st.slots)
            if (e.seq < slot->seq) slot = &e;
    }

    slot->pid = pid;
    for (std::size_t i = 0; i < len; ++i) slot->key[i] = key[i];
    slot->used = true;
    slot->seq  = ++st.seq;
    return ++st.gen;
}

inline std::uint32_t ClearSessionKey(std::uint32_t pid) {
    auto& st = detail::State();
    std::lock_guard<std::mutex> lk(st.mu);
    auto* e = detail::FindLocked(st, pid);
    if (!e) return st.gen;
    for (auto& b : e->key) b = 0;
    e->used = false;
    e->pid  = 0;
    return ++st.gen;
}

// Holders of a mapped channel compare this against the value they bound at and
// drop their handles when it moves. Global rather than per pid: a change for
// another client only costs a needless rebind, never a stale binding.
inline std::uint32_t SessionGeneration() {
    auto& st = detail::State();
    std::lock_guard<std::mutex> lk(st.mu);
    return st.gen;
}

inline bool HasSessionKey(std::uint32_t pid) {
    auto& st = detail::State();
    std::lock_guard<std::mutex> lk(st.mu);
    return detail::FindLocked(st, pid) != nullptr;
}

// Layout the launcher writes into the module's address space when it hands
// over a session. Both sides are built from this header so the layout matches.
inline constexpr std::uint32_t kSessionBlobVersion = 2;   // 2: names carry a per-channel keyed hash, not the key

struct SessionBlob {
    std::uint32_t version;    // kSessionBlobVersion
    std::uint32_t pid;        // the client this session is for, checked by the module
    std::uint8_t  key[16];
};

// Returns true when the caller's cached binding predates the current session,
// meaning its handles point at the previous set of names and must be dropped.
inline bool SessionChanged(std::uint32_t& boundGen) {
    std::uint32_t g = SessionGeneration();
    if (g == boundGen) return false;
    boundGen = g;
    return true;
}

// Drops a cached channel binding when the session has moved on, so the next
// call through the owner's usual lazy-map path rebinds under the new names.
//
// The old view is deliberately left mapped and its handle left open. Another
// thread may still be dereferencing that pointer: the session arrives on a
// thread of the launcher's making, while the render and input paths read these
// views on their own threads. Unmapping here would be a use after free in the
// host process. Holding a few kilobytes until the process exits is the cheaper
// side of that trade, and a rebind only happens when a new session is issued.
template <typename T, typename H>
inline bool RebindIfStale(std::uint32_t& boundGen, T*& view, H& map) {
    if (!SessionChanged(boundGen)) return false;
    view = nullptr;
    map  = (H)0;
    return true;
}

namespace detail {

// SipHash-2-4: a keyed pseudo-random function over bytes, 128-bit key, 64-bit result.
inline std::uint64_t Rotl(std::uint64_t x, int b) { return (x << b) | (x >> (64 - b)); }
inline std::uint64_t SipHash24(const std::uint8_t key[16], const std::uint8_t* in, std::size_t len) {
    std::uint64_t k0 = 0, k1 = 0;
    for (int i = 0; i < 8; ++i) { k0 |= (std::uint64_t)key[i] << (8 * i); k1 |= (std::uint64_t)key[8 + i] << (8 * i); }
    std::uint64_t v0 = 0x736f6d6570736575ull ^ k0, v1 = 0x646f72616e646f6dull ^ k1;
    std::uint64_t v2 = 0x6c7967656e657261ull ^ k0, v3 = 0x7465646279746573ull ^ k1;
    auto round = [&] {
        v0 += v1; v1 = Rotl(v1, 13); v1 ^= v0; v0 = Rotl(v0, 32);
        v2 += v3; v3 = Rotl(v3, 16); v3 ^= v2;
        v0 += v3; v3 = Rotl(v3, 21); v3 ^= v0;
        v2 += v1; v1 = Rotl(v1, 17); v1 ^= v2; v2 = Rotl(v2, 32);
    };
    const std::size_t whole = len & ~(std::size_t)7;
    for (std::size_t i = 0; i < whole; i += 8) {
        std::uint64_t m = 0;
        for (int b = 0; b < 8; ++b) m |= (std::uint64_t)in[i + b] << (8 * b);
        v3 ^= m; round(); round(); v0 ^= m;
    }
    std::uint64_t last = (std::uint64_t)(len & 0xFF) << 56;
    for (std::size_t b = 0; b < (len & 7); ++b) last |= (std::uint64_t)in[whole + b] << (8 * b);
    v3 ^= last; round(); round(); v0 ^= last;
    v2 ^= 0xFF; round(); round(); round(); round();
    return v0 ^ v1 ^ v2 ^ v3;
}

}   // namespace detail

// Writes "<prefix><pid>_<suffix>" into out, the suffix a keyed hash of the prefix
// under this client's session. On success returns true and out is NUL terminated.
// Without a session for the pid, or if the buffer is too small, out is set empty
// and false comes back, so no guessable or truncated name can ever reach
// CreateFileMapping or OpenFileMapping.
inline bool BuildName(wchar_t* out, std::size_t cap, const wchar_t* prefix, std::uint32_t pid) {
    if (!out || cap == 0) return false;
    out[0] = 0;
    if (!prefix) return false;

    const std::uint32_t pidForKey = pid;   // the digit loop below consumes pid

    std::size_t i = 0;
    for (const wchar_t* s = prefix; *s; ++s) {
        if (i + 1 >= cap) { out[0] = 0; return false; }
        out[i++] = *s;
    }

    wchar_t digits[16];
    int n = 0;
    if (pid == 0) digits[n++] = L'0';
    while (pid) { digits[n++] = (wchar_t)(L'0' + pid % 10); pid /= 10; }
    while (n) {
        if (i + 1 >= cap) { out[0] = 0; return false; }
        out[i++] = digits[--n];
    }

    // Per-session suffix: 128 bits of SipHash over the prefix (two domains), keyed by the session.
    {
        auto& st = detail::State();
        std::lock_guard<std::mutex> lk(st.mu);
        const detail::SessionEntry* e = detail::FindLocked(st, pidForKey);
        if (!e) { out[0] = 0; return false; }
        std::uint8_t msg[160]; std::size_t mlen = 1;
        for (const wchar_t* s = prefix; *s && mlen + 2 <= sizeof(msg); ++s) {
            msg[mlen++] = (std::uint8_t)(*s & 0xFF);
            msg[mlen++] = (std::uint8_t)((*s >> 8) & 0xFF);
        }
        std::uint64_t h[2];
        for (int d = 0; d < 2; ++d) { msg[0] = (std::uint8_t)d; h[d] = detail::SipHash24(e->key, msg, mlen); }
        static const wchar_t kHex[] = L"0123456789abcdef";
        if (i + 1 >= cap) { out[0] = 0; return false; }
        out[i++] = L'_';
        for (int d = 0; d < 2; ++d) {
            for (int b = 60; b >= 0; b -= 4) {
                if (i + 1 >= cap) { out[0] = 0; return false; }
                out[i++] = kHex[(h[d] >> b) & 0xF];
            }
        }
    }

    out[i] = 0;
    return true;
}

// Preferred form: the capacity comes from the array type.
template <std::size_t N>
inline bool BuildName(wchar_t (&out)[N], const wchar_t* prefix, std::uint32_t pid) {
    static_assert(N >= kNameChars,
                  "IPC name buffers must be declared wchar_t[rtx::ipc::kNameChars]");
    return BuildName(out, N, prefix, pid);
}

}   // namespace rtx::ipc
