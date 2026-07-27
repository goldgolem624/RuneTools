#include "Crypto.h"

#define WIN32_NO_STATUS
#include <Windows.h>
#undef  WIN32_NO_STATUS
#include <bcrypt.h>
#include <dpapi.h>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace rtx::launcher::crypto {

namespace {

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

struct AlgScope { BCRYPT_ALG_HANDLE h = nullptr;
    ~AlgScope() { if (h) BCryptCloseAlgorithmProvider(h, 0); } };
struct KeyScope { BCRYPT_KEY_HANDLE h = nullptr;
    ~KeyScope() { if (h) BCryptDestroyKey(h); } };

bool open_aes_gcm(AlgScope& alg) {
    auto st = BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_AES_ALGORITHM, nullptr, 0);
    if (!NT_SUCCESS(st)) return false;
    st = BCryptSetProperty(alg.h, BCRYPT_CHAINING_MODE,
                           (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                           sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    return NT_SUCCESS(st);
}

bool import_key(BCRYPT_ALG_HANDLE alg,
                const std::uint8_t* key, std::size_t key_len,
                KeyScope& out) {
    return NT_SUCCESS(BCryptGenerateSymmetricKey(
        alg, &out.h, nullptr, 0, (PUCHAR)key, (ULONG)key_len, 0));
}

}  // namespace

bool RandomBytes(std::uint8_t* out, std::size_t n) {
    if (!out || n == 0) return false;
    return NT_SUCCESS(BCryptGenRandom(
        nullptr, out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

bool DeriveKey(const std::string& passphrase,
               const std::uint8_t* salt, std::size_t salt_len,
               std::uint32_t iterations,
               std::uint8_t* out_key) {
    if (passphrase.empty() || !salt || salt_len == 0 ||
        iterations == 0 || !out_key) return false;

    AlgScope alg;
    auto st = BCryptOpenAlgorithmProvider(
        &alg.h, BCRYPT_SHA256_ALGORITHM, nullptr,
        BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!NT_SUCCESS(st)) return false;

    return NT_SUCCESS(BCryptDeriveKeyPBKDF2(
        alg.h,
        (PUCHAR)passphrase.data(), (ULONG)passphrase.size(),
        (PUCHAR)salt, (ULONG)salt_len,
        (ULONGLONG)iterations,
        out_key, (ULONG)kKeyBytes,
        0));
}

bool EncryptAesGcm(const std::uint8_t* key,   std::size_t key_len,
                   const std::uint8_t* nonce, std::size_t nonce_len,
                   const std::uint8_t* aad,   std::size_t aad_len,
                   const std::uint8_t* plaintext, std::size_t plaintext_len,
                   AeadOut& out) {
    if (!key || key_len != kKeyBytes ||
        !nonce || nonce_len != kNonceBytes) return false;

    AlgScope alg;
    if (!open_aes_gcm(alg)) return false;
    KeyScope key_h;
    if (!import_key(alg.h, key, key_len, key_h)) return false;

    out.tag.assign(kTagBytes, 0);
    out.ciphertext.assign(plaintext_len, 0);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info{};
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce    = (PUCHAR)nonce;
    info.cbNonce    = (ULONG)nonce_len;
    info.pbAuthData = (PUCHAR)aad;
    info.cbAuthData = (ULONG)aad_len;
    info.pbTag      = out.tag.data();
    info.cbTag      = (ULONG)out.tag.size();

    ULONG produced = 0;
    auto st = BCryptEncrypt(
        key_h.h,
        (PUCHAR)plaintext, (ULONG)plaintext_len,
        &info,
        nullptr, 0,
        out.ciphertext.data(), (ULONG)out.ciphertext.size(),
        &produced, 0);
    if (!NT_SUCCESS(st)) { out.ciphertext.clear(); out.tag.clear(); return false; }
    out.ciphertext.resize(produced);
    return true;
}

bool DecryptAesGcm(const std::uint8_t* key,   std::size_t key_len,
                   const std::uint8_t* nonce, std::size_t nonce_len,
                   const std::uint8_t* aad,   std::size_t aad_len,
                   const std::uint8_t* ciphertext, std::size_t ciphertext_len,
                   const std::uint8_t* tag,        std::size_t tag_len,
                   std::vector<std::uint8_t>& out_plaintext) {
    if (!key || key_len != kKeyBytes ||
        !nonce || nonce_len != kNonceBytes ||
        !tag || tag_len != kTagBytes) return false;

    AlgScope alg;
    if (!open_aes_gcm(alg)) return false;
    KeyScope key_h;
    if (!import_key(alg.h, key, key_len, key_h)) return false;

    out_plaintext.assign(ciphertext_len, 0);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info{};
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce    = (PUCHAR)nonce;
    info.cbNonce    = (ULONG)nonce_len;
    info.pbAuthData = (PUCHAR)aad;
    info.cbAuthData = (ULONG)aad_len;
    info.pbTag      = (PUCHAR)tag;
    info.cbTag      = (ULONG)tag_len;

    ULONG produced = 0;
    auto st = BCryptDecrypt(
        key_h.h,
        (PUCHAR)ciphertext, (ULONG)ciphertext_len,
        &info,
        nullptr, 0,
        out_plaintext.data(), (ULONG)out_plaintext.size(),
        &produced, 0);
    if (!NT_SUCCESS(st)) { out_plaintext.clear(); return false; }
    out_plaintext.resize(produced);
    return true;
}

std::vector<std::uint8_t> ProtectForCurrentUser(const std::string& plaintext) {
    if (plaintext.empty()) return {};

    DATA_BLOB in{};
    in.pbData = const_cast<BYTE*>(reinterpret_cast<const BYTE*>(plaintext.data()));
    in.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"RuneToolsX-device", nullptr,
                          nullptr, nullptr, 0, &out)) {
        return {};
    }
    std::vector<std::uint8_t> bytes(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return bytes;
}

std::string UnprotectForCurrentUser(const std::vector<std::uint8_t>& cipher) {
    if (cipher.empty()) return {};

    DATA_BLOB in{};
    in.pbData = const_cast<BYTE*>(cipher.data());
    in.cbData = static_cast<DWORD>(cipher.size());

    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr,
                            nullptr, nullptr, 0, &out)) {
        return {};
    }
    std::string s(reinterpret_cast<const char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return s;
}

bool Sha256(const std::uint8_t* data, std::size_t len, std::uint8_t out[32]) {
    if (!out) return false;
    AlgScope alg;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        return false;
    return NT_SUCCESS(BCryptHash(alg.h, nullptr, 0,
                                 (PUCHAR)data, (ULONG)len, out, 32));
}

bool VerifyEcdsaP256(const std::uint8_t* msg, std::size_t msg_len,
                     const std::uint8_t* sig, std::size_t sig_len,
                     const std::uint8_t* pubkey_xy, std::size_t pubkey_len) {
    if (!msg || !sig || sig_len != 64 || !pubkey_xy || pubkey_len != 64) return false;

    std::uint8_t digest[32];
    if (!Sha256(msg, msg_len, digest)) return false;

    AlgScope alg;
    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&alg.h, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0)))
        return false;

    // BCRYPT_ECCKEY_BLOB: { ULONG dwMagic; ULONG cbKey; } then X[32] then Y[32].
    std::vector<std::uint8_t> blob(sizeof(BCRYPT_ECCKEY_BLOB) + 64);
    auto* hdr = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
    hdr->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    hdr->cbKey   = 32;
    memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), pubkey_xy, 64);

    KeyScope key;
    if (!NT_SUCCESS(BCryptImportKeyPair(alg.h, nullptr, BCRYPT_ECCPUBLIC_BLOB,
                                        &key.h, blob.data(), (ULONG)blob.size(), 0)))
        return false;

    return NT_SUCCESS(BCryptVerifySignature(key.h, nullptr, digest, 32,
                                            (PUCHAR)sig, (ULONG)sig_len, 0));
}

}  // namespace rtx::launcher::crypto
