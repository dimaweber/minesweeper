#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace envelope::detail {

inline constexpr size_t hmac_sha256_tag_size = 32;
inline constexpr size_t aes_gcm_key_size     = 32;  // AES-256
inline constexpr size_t aes_gcm_nonce_size   = 12;  // 96-bit, the GCM-recommended nonce size
inline constexpr size_t aes_gcm_tag_size     = 16;  // 128-bit authentication tag

// HMAC-SHA256, used for the "sign" layer - a shared-secret MAC, not an
// asymmetric signature. That's a deliberate choice for a generic
// wrap/unwrap layer: no public-key trust setup, and it composes cleanly
// with a "signed but not encrypted" mode that an AEAD cipher's own
// authentication (see aes_gcm_* below) can't offer on its own, since AEAD
// only authenticates when it's also encrypting.
[[nodiscard]] std::array<std::byte, hmac_sha256_tag_size> hmac_sha256 (std::span<const std::byte> key, std::span<const std::byte> data);

// Constant-time comparison for the two tag types above - a plain == or
// memcmp on a MAC/signature is a timing side channel (how many leading
// bytes matched leaks through how long the comparison takes).
[[nodiscard]] bool constant_time_equal (std::span<const std::byte> a, std::span<const std::byte> b);

// AES-256-GCM: an AEAD cipher, so encryption already carries its own
// authentication tag - encrypt_key must never be reused with the same
// nonce (aes_gcm_encrypt always draws a fresh random one), or GCM's
// confidentiality and authenticity guarantees both break. Output/input
// shape is [nonce][ciphertext][tag], all three concatenated.
[[nodiscard]] std::expected<std::vector<std::byte>, std::string> aes_gcm_encrypt (std::span<const std::byte> key, std::span<const std::byte> plaintext);
[[nodiscard]] std::expected<std::vector<std::byte>, std::string> aes_gcm_decrypt (std::span<const std::byte> key, std::span<const std::byte> nonce_ciphertext_tag);

}  // namespace envelope::detail
