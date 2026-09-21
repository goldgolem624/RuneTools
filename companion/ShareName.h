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
// pid. While no key is set for a pid the legacy pid-only name is produced,
// which is what an older peer on the other side of that channel expects.

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
inline constexpr std::uint32_t kSessionBlobVersion = 1;

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

// Writes "<prefix><pid>" into out. On success returns true and out is NUL
// terminated. If the buffer is too small out is set empty and false comes
// back, so a truncated name can never reach CreateFileMapping or OpenFileMapping.
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

    // Per-session suffix, when one has been set for this pid.
    {
        auto& st = detail::State();
        std::lock_guard<std::mutex> lk(st.mu);
        const detail::SessionEntry* e = detail::FindLocked(st, pidForKey);
        if (e) {
            static const wchar_t kHex[] = L"0123456789abcdef";
            if (i + 1 >= cap) { out[0] = 0; return false; }
            out[i++] = L'_';
            for (std::size_t k = 0; k < sizeof(e->key); ++k) {
                if (i + 2 >= cap) { out[0] = 0; return false; }
                out[i++] = kHex[(e->key[k] >> 4) & 0xF];
                out[i++] = kHex[e->key[k] & 0xF];
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
