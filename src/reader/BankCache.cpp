#include "BankCache.h"

#include "../launcher/Crypto.h"
#include "../shared/MachineFingerprint.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>

#include <Windows.h>

namespace rtx::reader {

namespace {

namespace crypto = rtx::launcher::crypto;

// Encrypted file: 'R','T','X','E' | u16 ver | nonce[12] | tag[16] | ciphertext.
// The ciphertext is AES-256-GCM of the plaintext blob:
//   i64 cached_at | u32 count | count x { i32 item_id, i32 stack }
constexpr char          kMagic[4]  = { 'R', 'T', 'X', 'E' };
constexpr std::uint16_t kVersion   = 2;
constexpr std::size_t   kHeaderLen = 4 + 2 + crypto::kNonceBytes + crypto::kTagBytes;

// Fixed application salt. The machine fingerprint is already a 64-char
// high-entropy secret, so a constant salt is fine -- secrecy comes from the
// key, and GCM uses a fresh random nonce per write.
constexpr std::uint8_t kSalt[crypto::kSaltBytes] = {
    0x52,0x54,0x58,0x42, 0x6e,0x6b,0x76,0x31, 0xa7,0x3c,0x91,0x5e, 0x0d,0xf2,0x84,0x6b
};

std::filesystem::path cache_dir(const std::string& kind) {
    char* up = nullptr; std::size_t len = 0;
    std::filesystem::path base;
    if (_dupenv_s(&up, &len, "USERPROFILE") == 0 && up) { base = up; free(up); }
    return base / "RuneToolsX" / (kind + "cache");
}

std::string sanitize(const std::string& name) {
    std::string out;
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') out.push_back(c);
        else if (c == ' ') out.push_back('_');
    }
    return out.empty() ? "unknown" : out;
}

// AES key derived from the stable machine fingerprint, memoised.
std::mutex                g_key_mu;
std::vector<std::uint8_t> g_key;
std::string               g_key_for;

bool cache_key(std::uint8_t out[crypto::kKeyBytes]) {
    const std::string& secret = rtx::shared::GetMachineFingerprint();
    if (secret.empty()) return false;

    std::lock_guard<std::mutex> lk(g_key_mu);
    if (secret == g_key_for && g_key.size() == crypto::kKeyBytes) {
        std::memcpy(out, g_key.data(), crypto::kKeyBytes);
        return true;
    }
    if (!crypto::DeriveKey(secret, kSalt, crypto::kSaltBytes,
                           crypto::kKdfDefaultIterations, out))
        return false;
    g_key.assign(out, out + crypto::kKeyBytes);
    g_key_for = secret;
    return true;
}

std::vector<std::uint8_t> read_file(const std::filesystem::path& p) {
    std::vector<std::uint8_t> data;
    FILE* f = nullptr;
    if (_wfopen_s(&f, p.wstring().c_str(), L"rb") != 0 || !f) return data;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0 && n < (16 * 1024 * 1024)) {
        data.resize((std::size_t)n);
        if (std::fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
    }
    std::fclose(f);
    return data;
}

}  // namespace

bool WriteContainerCache(const std::string& kind, const std::string& character,
                         const std::vector<BankSlot>& slots) {
    std::uint8_t key[crypto::kKeyBytes];
    if (!cache_key(key)) return false;   // no key -> no cache

    // Build the plaintext blob.
    std::int64_t  ts    = (std::int64_t)std::time(nullptr);
    std::uint32_t count = (std::uint32_t)slots.size();
    std::vector<std::uint8_t> blob;
    blob.reserve(12 + slots.size() * sizeof(BankSlot));
    blob.insert(blob.end(), (std::uint8_t*)&ts, (std::uint8_t*)&ts + 8);
    blob.insert(blob.end(), (std::uint8_t*)&count, (std::uint8_t*)&count + 4);
    if (count)
        blob.insert(blob.end(), (std::uint8_t*)slots.data(),
                    (std::uint8_t*)slots.data() + slots.size() * sizeof(BankSlot));

    std::uint8_t nonce[crypto::kNonceBytes];
    if (!crypto::RandomBytes(nonce, crypto::kNonceBytes)) return false;
    crypto::AeadOut aead;
    if (!crypto::EncryptAesGcm(key, crypto::kKeyBytes, nonce, crypto::kNonceBytes,
                               nullptr, 0, blob.data(), blob.size(), aead))
        return false;

    std::error_code ec;
    std::filesystem::create_directories(cache_dir(kind), ec);
    auto path = cache_dir(kind) / (sanitize(character) + ".bnk");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.wstring().c_str(), L"wb") != 0 || !f) return false;
    bool ok =
        std::fwrite(kMagic, 1, 4, f) == 4 &&
        std::fwrite(&kVersion, sizeof(kVersion), 1, f) == 1 &&
        std::fwrite(nonce, 1, crypto::kNonceBytes, f) == crypto::kNonceBytes &&
        std::fwrite(aead.tag.data(), 1, aead.tag.size(), f) == aead.tag.size() &&
        (aead.ciphertext.empty() ||
         std::fwrite(aead.ciphertext.data(), 1, aead.ciphertext.size(), f)
             == aead.ciphertext.size());
    std::fclose(f);
    return ok;
}

BankCacheData ReadContainerCache(const std::string& kind, const std::string& character) {
    BankCacheData out;
    auto data = read_file(cache_dir(kind) / (sanitize(character) + ".bnk"));
    if (data.size() < kHeaderLen) return out;
    if (std::memcmp(data.data(), kMagic, 4) != 0) return out;
    std::uint16_t ver;
    std::memcpy(&ver, data.data() + 4, 2);
    if (ver != kVersion) return out;

    std::uint8_t key[crypto::kKeyBytes];
    if (!cache_key(key)) return out;

    const std::uint8_t* nonce = data.data() + 6;
    const std::uint8_t* tag   = nonce + crypto::kNonceBytes;
    const std::uint8_t* ct    = tag + crypto::kTagBytes;
    std::size_t         ctlen = data.size() - kHeaderLen;

    std::vector<std::uint8_t> blob;
    if (!crypto::DecryptAesGcm(key, crypto::kKeyBytes, nonce, crypto::kNonceBytes,
                               nullptr, 0, ct, ctlen, tag, crypto::kTagBytes, blob))
        return out;   // wrong key / tampered

    if (blob.size() < 12) return out;
    std::int64_t  ts;    std::memcpy(&ts, blob.data(), 8);
    std::uint32_t count; std::memcpy(&count, blob.data() + 8, 4);
    if (count > 8192) return out;
    std::size_t need = 12 + (std::size_t)count * sizeof(BankSlot);
    if (blob.size() < need) return out;

    out.cached_at = ts;
    out.slots.resize(count);
    if (count)
        std::memcpy(out.slots.data(), blob.data() + 12, count * sizeof(BankSlot));
    return out;
}

bool WriteBankCache(const std::string& character, const std::vector<BankSlot>& slots) {
    return WriteContainerCache("bank", character, slots);
}
BankCacheData ReadBankCache(const std::string& character) {
    return ReadContainerCache("bank", character);
}

}  // namespace rtx::reader
