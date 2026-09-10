#include "crypto.hxx"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

#include <stdexcept>

namespace envelope::detail {

std::array<std::byte, hmac_sha256_tag_size> hmac_sha256 (std::span<const std::byte> key, std::span<const std::byte> data) {
  std::array<std::byte, hmac_sha256_tag_size> tag {};
  unsigned int                                len    = 0;
  const unsigned char*                        result = HMAC(EVP_sha256( ), key.data( ), static_cast<int>(key.size( )), reinterpret_cast<const unsigned char*>(data.data( )), data.size( ), reinterpret_cast<unsigned char*>(tag.data( )), &len);
  // HMAC() with a correctly-sized stack output buffer and a standard,
  // always-available digest (SHA-256) only returns nullptr on an internal
  // allocation failure - not a recoverable condition, same treatment as
  // any other out-of-memory failure in this library.
  if ( !result || len != hmac_sha256_tag_size ) {
    throw std::runtime_error("envelope: HMAC-SHA256 failed unexpectedly");
  }
  return tag;
}

bool constant_time_equal (std::span<const std::byte> a, std::span<const std::byte> b) {
  if ( a.size( ) != b.size( ) ) {
    return false;
  }
  return CRYPTO_memcmp(a.data( ), b.data( ), a.size( )) == 0;
}

namespace {
struct cipher_ctx_t {
  EVP_CIPHER_CTX* ctx;

  cipher_ctx_t ( ) : ctx(EVP_CIPHER_CTX_new( )) {
  }

  ~cipher_ctx_t ( ) {
    if ( ctx ) {
      EVP_CIPHER_CTX_free(ctx);
    }
  }

  cipher_ctx_t(const cipher_ctx_t&)            = delete;
  cipher_ctx_t& operator= (const cipher_ctx_t&) = delete;

  operator EVP_CIPHER_CTX* ( ) const {
    return ctx;
  }
};
}  // namespace

std::expected<std::vector<std::byte>, std::string> aes_gcm_encrypt (const aes_key_t& key, std::span<const std::byte> plaintext) {
  std::array<std::byte, aes_gcm_nonce_size> nonce {};
  if ( RAND_bytes(reinterpret_cast<unsigned char*>(nonce.data( )), static_cast<int>(nonce.size( ))) != 1 ) {
    return std::unexpected("envelope: failed to generate a random nonce.");
  }

  const cipher_ctx_t ctx;
  if ( !ctx.ctx ) {
    return std::unexpected("envelope: failed to allocate a cipher context.");
  }

  if ( EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm( ), nullptr, nullptr, nullptr) != 1 ) {
    return std::unexpected("envelope: failed to initialize AES-256-GCM.");
  }
  if ( EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size( )), nullptr) != 1 ) {
    return std::unexpected("envelope: failed to set the GCM nonce length.");
  }
  if ( EVP_EncryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.data( )), reinterpret_cast<const unsigned char*>(nonce.data( ))) != 1 ) {
    return std::unexpected("envelope: failed to set the AES-256-GCM key/nonce.");
  }

  std::vector<std::byte> ciphertext(plaintext.size( ));
  int                    out_len = 0;
  if ( !plaintext.empty( ) ) {
    if ( EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(ciphertext.data( )), &out_len, reinterpret_cast<const unsigned char*>(plaintext.data( )), static_cast<int>(plaintext.size( ))) != 1 ) {
      return std::unexpected("envelope: AES-256-GCM encryption failed.");
    }
  }
  int final_len = 0;
  if ( EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(ciphertext.data( )) + out_len, &final_len) != 1 ) {
    return std::unexpected("envelope: AES-256-GCM encryption finalization failed.");
  }
  ciphertext.resize(static_cast<size_t>(out_len + final_len));

  std::array<std::byte, aes_gcm_tag_size> tag {};
  if ( EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size( )), tag.data( )) != 1 ) {
    return std::unexpected("envelope: failed to read the AES-256-GCM tag.");
  }

  std::vector<std::byte> out;
  out.reserve(nonce.size( ) + ciphertext.size( ) + tag.size( ));
  out.insert(out.end( ), nonce.begin( ), nonce.end( ));
  out.insert(out.end( ), ciphertext.begin( ), ciphertext.end( ));
  out.insert(out.end( ), tag.begin( ), tag.end( ));
  return out;
}

std::expected<std::vector<std::byte>, std::string> aes_gcm_decrypt (const aes_key_t& key, std::span<const std::byte> nonce_ciphertext_tag) {
  if ( nonce_ciphertext_tag.size( ) < aes_gcm_nonce_size + aes_gcm_tag_size ) {
    return std::unexpected("envelope: encrypted payload too short for its nonce/tag.");
  }

  const auto nonce      = nonce_ciphertext_tag.subspan(0, aes_gcm_nonce_size);
  const auto tag         = nonce_ciphertext_tag.subspan(nonce_ciphertext_tag.size( ) - aes_gcm_tag_size, aes_gcm_tag_size);
  const auto ciphertext = nonce_ciphertext_tag.subspan(aes_gcm_nonce_size, nonce_ciphertext_tag.size( ) - aes_gcm_nonce_size - aes_gcm_tag_size);

  const cipher_ctx_t ctx;
  if ( !ctx.ctx ) {
    return std::unexpected("envelope: failed to allocate a cipher context.");
  }

  if ( EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm( ), nullptr, nullptr, nullptr) != 1 ) {
    return std::unexpected("envelope: failed to initialize AES-256-GCM.");
  }
  if ( EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(nonce.size( )), nullptr) != 1 ) {
    return std::unexpected("envelope: failed to set the GCM nonce length.");
  }
  if ( EVP_DecryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast<const unsigned char*>(key.data( )), reinterpret_cast<const unsigned char*>(nonce.data( ))) != 1 ) {
    return std::unexpected("envelope: failed to set the AES-256-GCM key/nonce.");
  }

  std::vector<std::byte> plaintext(ciphertext.size( ));
  int                    out_len = 0;
  if ( !ciphertext.empty( ) ) {
    if ( EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plaintext.data( )), &out_len, reinterpret_cast<const unsigned char*>(ciphertext.data( )), static_cast<int>(ciphertext.size( ))) != 1 ) {
      return std::unexpected("envelope: AES-256-GCM decryption failed.");
    }
  }

  if ( EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size( )), const_cast<std::byte*>(tag.data( ))) != 1 ) {
    return std::unexpected("envelope: failed to set the expected AES-256-GCM tag.");
  }

  int final_len = 0;
  if ( EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plaintext.data( )) + out_len, &final_len) != 1 ) {
    return std::unexpected("envelope: AES-256-GCM authentication failed - wrong key, or the data was tampered with.");
  }
  plaintext.resize(static_cast<size_t>(out_len + final_len));
  return plaintext;
}

}  // namespace envelope::detail
