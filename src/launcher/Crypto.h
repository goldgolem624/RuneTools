#pragma once

#include <cstdint>
#include <string>
#include <vector>


namespace rtx::launcher::crypto {

constexpr std::uint32_t kKdfDefaultIterations = 600'000;
constexpr std::size_t   kSaltBytes   = 16;
constexpr std::size_t   kNonceBytes  = 12;
constexpr std::size_t   kTagBytes    = 16;
constexpr std::size_t   kKeyBytes    = 32;   // AES-256

bool RandomBytes(std::uint8_t* out, std::size_t n);

bool DeriveKey(const std::string& passphrase,
               const std::uint8_t* salt, std::size_t salt_len,
               std::uint32_t iterations,
               std::uint8_t* out_key);

struct AeadOut {
    std::vector<std::uint8_t> ciphertext;
    std::vector<std::uint8_t> tag;        // 16 bytes
};

bool EncryptAesGcm(const std::uint8_t* key,   std::size_t key_len,
                   const std::uint8_t* nonce, std::size_t nonce_len,
                   const std::uint8_t* aad,   std::size_t aad_len,
                   const std::uint8_t* plaintext, std::size_t plaintext_len,
                   AeadOut& out);

bool DecryptAesGcm(const std::uint8_t* key,   std::size_t key_len,
                   const std::uint8_t* nonce, std::size_t nonce_len,
                   const std::uint8_t* aad,   std::size_t aad_len,
                   const std::uint8_t* ciphertext, std::size_t ciphertext_len,
                   const std::uint8_t* tag,        std::size_t tag_len,
                   std::vector<std::uint8_t>& out_plaintext);

std::vector<std::uint8_t> ProtectForCurrentUser(const std::string& plaintext);
std::string               UnprotectForCurrentUser(const std::vector<std::uint8_t>& cipher);

bool Sha256(const std::uint8_t* data, std::size_t len, std::uint8_t out[32]);

// ECDSA P-256 verify. `sig` is raw 64-byte r||s (IEEE-P1363); `pubkey_xy` is raw 64-byte X||Y.
bool VerifyEcdsaP256(const std::uint8_t* msg, std::size_t msg_len,
                     const std::uint8_t* sig, std::size_t sig_len,
                     const std::uint8_t* pubkey_xy, std::size_t pubkey_len);

}  // namespace rtx::launcher::crypto
