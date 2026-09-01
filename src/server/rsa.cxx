#include <fmt/std.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/types.h>

#include <filesystem>
#include <fstream>
#include <inc/logger.hxx>

namespace {
struct evp_pkey_ctx {
  EVP_PKEY_CTX* ctx;

  evp_pkey_ctx (EVP_PKEY_CTX* c) : ctx(c) {
  }

  ~evp_pkey_ctx ( ) {
    if ( ctx )
      EVP_PKEY_CTX_free(ctx);
  }

  operator EVP_PKEY_CTX*( ) {
    return ctx;
  }
};

struct evp_pkey {
  EVP_PKEY* pkey;

  evp_pkey (EVP_PKEY* p) : pkey(p) {
  }

  ~evp_pkey ( ) {
    if ( pkey )
      EVP_PKEY_free(pkey);
  }

  operator EVP_PKEY*( ) {
    return pkey;
  }
};
}  // namespace

bool generate_rsa_private_key (const std::filesystem::path& priv_key_path, const std::filesystem::path pub_key_path, int bits = 2048) {
  SPDLOG_DEBUG("Generate RSA key to {}/{}", priv_key_path, pub_key_path);

  evp_pkey pkey = [bits] ( ) -> EVP_PKEY* {
    EVP_PKEY*    pkey = nullptr;
    evp_pkey_ctx ctx {EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr)};
    if ( !ctx )
      return nullptr;

    if ( EVP_PKEY_keygen_init(ctx) <= 0 ) {
      return nullptr;
    }

    if ( EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0 ) {
      return nullptr;
    }

    if ( EVP_PKEY_keygen(ctx, &pkey) <= 0 ) {
      return nullptr;
    }
    return pkey;
  }( );

  if ( !pkey ) {
    SPDLOG_ERROR("Failed to generate RSA private key");
    return false;
  }

  FILE* fp = ::fopen(priv_key_path.c_str( ), "wb");
  if ( !fp ) {
    SPDLOG_ERROR("Failed to open {} for writing", priv_key_path);
    return false;
  }

  PEM_write_PrivateKey(fp, pkey, nullptr, nullptr, 0, nullptr, nullptr);
  ::fclose(fp);

  FILE* fp2 = ::fopen(pub_key_path.c_str( ), "wb");
  if ( !fp2 ) {
    SPDLOG_ERROR("Failed to open {} for writing", pub_key_path);
    return false;
  }

  PEM_write_PUBKEY(fp2, pkey);
  ::fclose(fp2);

  return true;
}

std::pair<std::string, std::string> rsa_key_pair (const std::filesystem::path rsa_priv_key_path, const std::filesystem::path rsa_pub_key_path) {
  if ( !std::filesystem::exists(rsa_priv_key_path) || !std::filesystem::exists(rsa_pub_key_path) ) {
    SPDLOG_INFO("RSA key pair not found, generating new one");
    if ( !generate_rsa_private_key(rsa_priv_key_path, rsa_pub_key_path) )
      throw std::runtime_error("Failed to generate private.pem");
  }

  std::ifstream priv_key_file {rsa_priv_key_path};
  if ( !priv_key_file.is_open( ) ) {
    SPDLOG_ERROR("Failed to open {}", rsa_priv_key_path);
    throw std::runtime_error {fmt::format("Failed to open {}", rsa_priv_key_path)};
  }
  const std::string priv_key {std::istreambuf_iterator<char>(priv_key_file), std::istreambuf_iterator<char>( )};
  SPDLOG_DEBUG("Loaded RSA private key from {}", rsa_priv_key_path);

  std::ifstream pub_key_file {rsa_pub_key_path};
  if ( !pub_key_file.is_open( ) ) {
    SPDLOG_ERROR("Failed to open {}", rsa_pub_key_path);
    throw std::runtime_error {fmt::format("Failed to open {}", rsa_pub_key_path)};
  }
  const std::string pub_key {std::istreambuf_iterator<char>(pub_key_file), std::istreambuf_iterator<char>( )};
  SPDLOG_DEBUG("Loaded RSA public key from {}", rsa_pub_key_path);

  return {priv_key, pub_key};
}
