#include <fmt/std.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/types.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <filesystem>
#include <fstream>
#include <inc/logger.hxx>
#include <optional>
#include <wbr/system_handler.hxx>

namespace {
struct evp_pkey_ctx_t {
  EVP_PKEY_CTX* ctx;

  evp_pkey_ctx_t(const evp_pkey_ctx_t&)             = delete;
  evp_pkey_ctx_t& operator= (const evp_pkey_ctx_t&) = delete;

  evp_pkey_ctx_t (evp_pkey_ctx_t&& other) noexcept : ctx(other.ctx) {
    other.ctx = nullptr;
  }

  evp_pkey_ctx_t& operator= (evp_pkey_ctx_t&& other) noexcept {
    if ( this != &other ) {
      if ( ctx )
        EVP_PKEY_CTX_free(ctx);
      ctx       = other.ctx;
      other.ctx = nullptr;
    }
    return *this;
  }

  evp_pkey_ctx_t (EVP_PKEY_CTX* c) : ctx(c) {
  }

  ~evp_pkey_ctx_t ( ) {
    if ( ctx )
      EVP_PKEY_CTX_free(ctx);
  }

  operator EVP_PKEY_CTX*( ) const {
    return ctx;
  }
};

struct evp_pkey_t {
  EVP_PKEY* pkey;

  evp_pkey_t(const evp_pkey_t&)             = delete;
  evp_pkey_t& operator= (const evp_pkey_t&) = delete;

  evp_pkey_t (evp_pkey_t&& other) noexcept : pkey(other.pkey) {
    other.pkey = nullptr;
  }

  evp_pkey_t& operator= (evp_pkey_t&& other) noexcept {
    if ( this != &other ) {
      if ( pkey )
        EVP_PKEY_free(pkey);
      pkey       = other.pkey;
      other.pkey = nullptr;
    }
    return *this;
  }

  evp_pkey_t (EVP_PKEY* p = nullptr) : pkey(p) {
  }

  ~evp_pkey_t ( ) {
    if ( pkey )
      EVP_PKEY_free(pkey);
  }

  operator EVP_PKEY*( ) const {
    return pkey;
  }
};

struct x509_t {
  X509* cert;

  x509_t(const x509_t&)             = delete;
  x509_t& operator= (const x509_t&) = delete;

  x509_t (x509_t&& other) noexcept : cert(other.cert) {
    other.cert = nullptr;
  }

  x509_t& operator= (x509_t&& other) noexcept {
    if ( this != &other ) {
      if ( cert )
        X509_free(cert);
      cert       = other.cert;
      other.cert = nullptr;
    }
    return *this;
  }

  x509_t (X509* c) : cert(c) {
  }

  x509_t ( ) : x509_t(X509_new( )) {
  }

  ~x509_t ( ) {
    if ( cert )
      X509_free(cert);
  }

  operator X509*( ) const {
    return cert;
  }
};

struct bio_t {
  BIO* bio;

  bio_t(const bio_t&)             = delete;
  bio_t& operator= (const bio_t&) = delete;

  bio_t (bio_t&& other) noexcept : bio(other.bio) {
    other.bio = nullptr;
  }

  bio_t& operator= (bio_t&& other) noexcept {
    if ( this != &other ) {
      if ( bio )
        BIO_free(bio);
      bio       = other.bio;
      other.bio = nullptr;
    }
    return *this;
  }

  bio_t (BIO* b) : bio(b) {
  }

  ~bio_t ( ) {
    if ( bio )
      BIO_free(bio);
  }

  operator BIO*( ) const {
    return bio;
  }
};

}  // namespace

bool generate_rsa_private_key (const std::filesystem::path& priv_key_path, const std::filesystem::path pub_key_path, int bits = 2048) {
  SPDLOG_DEBUG("Generate RSA key to {}/{}", priv_key_path, pub_key_path);

  evp_pkey_t pkey = [bits] ( ) -> EVP_PKEY* {
    EVP_PKEY*      pkey = nullptr;
    evp_pkey_ctx_t ctx {EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr)};
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

  bio_t fp {BIO_new_file(priv_key_path.c_str( ), "wb")};
  if ( !fp ) {
    SPDLOG_ERROR("Failed to open {} for writing", priv_key_path);
    return false;
  }

  PEM_write_bio_PrivateKey(fp, pkey, nullptr, nullptr, 0, nullptr, nullptr);

  bio_t fp2 {BIO_new_file(pub_key_path.c_str( ), "wb")};
  if ( !fp2 ) {
    SPDLOG_ERROR("Failed to open {} for writing", pub_key_path);
    return false;
  }

  PEM_write_bio_PUBKEY(fp2, pkey);

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

std::optional<x509_t> create_self_signed_cert (const evp_pkey_t& rsa_key, long days_valid) {
  x509_t cert;
  if ( !cert ) {
    SPDLOG_ERROR("Failed to create new X509 certificate");
    return std::nullopt;
  }
  if ( X509_set_version(cert, 2) != 1 ) {
    const auto err = ERR_get_error( );
    SPDLOG_ERROR("Failed to set X509 version: {}", ERR_error_string(err, nullptr));
    return std::nullopt;
  }

  if ( ASN1_INTEGER_set(X509_get_serialNumber(cert), 1) != 1 ) {
    const auto err = ERR_get_error( );
    SPDLOG_ERROR("Failed to set X509 serial number: {}", ERR_error_string(err, nullptr));
    return std::nullopt;
  }

  if ( X509_gmtime_adj(X509_get_notBefore(cert), 0) == nullptr ) {
    const auto err = ERR_get_error( );
    SPDLOG_ERROR("Failed to set X509 notBefore: {}", ERR_error_string(err, nullptr));
    return std::nullopt;
  }
  if ( X509_gmtime_adj(X509_get_notAfter(cert), days_valid * 24 * 60 * 60) == nullptr ) {
    const auto err = ERR_get_error( );
    SPDLOG_ERROR("Failed to set X509 notAfter: {}", ERR_error_string(err, nullptr));
    return std::nullopt;
  }

  X509_NAME* name = X509_get_subject_name(cert);
  if ( !name ) {
    SPDLOG_ERROR("Failed to get X509 subject name: {}", ERR_error_string(ERR_get_error( ), nullptr));
    return std::nullopt;
  }
  if ( X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("localhost"), -1, -1, 0) != 1 ) {
    SPDLOG_ERROR("Failed to add CN to X509 subject name: {}", ERR_error_string(ERR_get_error( ), nullptr));
    return std::nullopt;
  }
  if ( X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("UA"), -1, -1, 0) != 1 ) {
    SPDLOG_ERROR("Failed to add C to X509 subject name: {}", ERR_error_string(ERR_get_error( ), nullptr));
    return std::nullopt;
  }
  if ( X509_set_issuer_name(cert, name) != 1 ) {
    SPDLOG_ERROR("Failed to set X509 issuer name: {}", ERR_error_string(ERR_get_error( ), nullptr));
    return std::nullopt;
  }

  if ( X509_set_pubkey(cert, rsa_key) != 1 ) {
    SPDLOG_ERROR("Failed to set X509 public key: {}", ERR_error_string(ERR_get_error( ), nullptr));
    return std::nullopt;
  }

  if ( !X509_sign(cert, rsa_key, EVP_sha256( )) ) {
    SPDLOG_ERROR("Failed to sign X509 certificate: {}", ERR_error_string(ERR_get_error( ), nullptr));
    return std::nullopt;
  }

  return cert;
}

bool write_cert_to_file (const x509_t& cert, const std::filesystem::path& cert_path) {
  bio_t out {BIO_new_file(cert_path.string( ).c_str( ), "w")};
  if ( !out ) {
    SPDLOG_ERROR("Failed to open {} for writing", cert_path);
    return false;
  }

  if ( PEM_write_bio_X509(out, cert) <= 0 ) {
    SPDLOG_ERROR("Failed to write X509 certificate to {}", cert_path);
    return false;
  }

  return true;
}

std::optional<evp_pkey_t> generate_dh_params ( ) {
  evp_pkey_ctx_t ctx {EVP_PKEY_CTX_new_from_name(nullptr, "DH", nullptr)};
  if ( !ctx ) {
    SPDLOG_ERROR("Failed to create EVP_PKEY_CTX for DH");
    return std::nullopt;
  }

  if ( EVP_PKEY_paramgen_init(ctx) <= 0 ) {
    SPDLOG_ERROR("Failed to initialize DH parameter generation");
    return std::nullopt;
  }

  if ( EVP_PKEY_CTX_set_dh_paramgen_prime_len(ctx, 2048) <= 0 ) {
    SPDLOG_ERROR("Failed to set DH prime length");
    return std::nullopt;
  }

  if ( EVP_PKEY_CTX_set_dh_paramgen_generator(ctx, 2) <= 0 ) {
    SPDLOG_ERROR("Failed to set DH generator");
    return std::nullopt;
  }

  evp_pkey_t dh_params;
  if ( EVP_PKEY_paramgen(ctx, &dh_params.pkey) <= 0 ) {
    SPDLOG_ERROR("Failed to generate DH parameters");
    return std::nullopt;
  }
  return dh_params;
}

[[maybe_unused]]
std::optional<evp_pkey_t> generate_dh_keypair (const evp_pkey_t& dh_params) {
  evp_pkey_ctx_t ctx {EVP_PKEY_CTX_new_from_pkey(nullptr, dh_params, nullptr)};
  if ( !ctx ) {
    SPDLOG_ERROR("Failed to create EVP_PKEY_CTX for DH key generation");
    return std::nullopt;
  }

  if ( EVP_PKEY_keygen_init(ctx) <= 0 ) {
    SPDLOG_ERROR("Failed to initialize DH key generation");
    return std::nullopt;
  }

  evp_pkey_t dh_keypair;
  if ( EVP_PKEY_keygen(ctx, &dh_keypair.pkey) <= 0 ) {
    SPDLOG_ERROR("Failed to generate DH key pair");
    return std::nullopt;
  }
  return dh_keypair;
}

[[maybe_unused]]
bool write_dh_params_to_file (const evp_pkey_t& dh_params, const std::filesystem::path& dh_path) {
  bio_t out {BIO_new_file(dh_path.c_str( ), "w")};
  if ( !out ) {
    SPDLOG_ERROR("Failed to open {} for writing", dh_path);
    return false;
  }
  if ( PEM_write_bio_Parameters(out, dh_params) <= 0 ) {
    SPDLOG_ERROR("Failed to write DH parameters to {}", dh_path);
    return false;
  }

  return true;
}

[[maybe_unused]]
bool write_dh_keypair_to_file (const evp_pkey_t& dh_keypair, const std::filesystem::path& dh_key_path) {
  bio_t out {BIO_new_file(dh_key_path.c_str( ), "w")};
  if ( !out ) {
    SPDLOG_ERROR("Failed to open {} for writing", dh_key_path);
    return false;
  }
  if ( PEM_write_bio_PrivateKey(out, dh_keypair, nullptr, nullptr, 0, nullptr, nullptr) <= 0 ) {
    SPDLOG_ERROR("Failed to write DH key pair to {}", dh_key_path);
    return false;
  }

  return true;
}

std::optional<evp_pkey_t> load_rsa_private_key (const std::filesystem::path& priv_key_path) {
  const bio_t fp {BIO_new_file(priv_key_path.c_str( ), "r")};
  if ( !fp ) {
    SPDLOG_ERROR("Failed to open {} for reading", priv_key_path);
    return std::nullopt;
  }

  evp_pkey_t pkey {PEM_read_bio_PrivateKey(fp, nullptr, nullptr, nullptr)};

  if ( !pkey ) {
    SPDLOG_ERROR("Failed to read RSA private key from {}", priv_key_path);
    return std::nullopt;
  }

  return pkey;
}

[[maybe_unused]]
std::optional<evp_pkey_t> load_rsa_public_key (const std::filesystem::path& pub_key_path) {
  const bio_t fp {BIO_new_file(pub_key_path.c_str( ), "r")};
  if ( !fp ) {
    SPDLOG_ERROR("Failed to open {} for reading", pub_key_path);
    return std::nullopt;
  }

  evp_pkey_t pkey {PEM_read_bio_PUBKEY(fp, nullptr, nullptr, nullptr)};

  if ( !pkey ) {
    SPDLOG_ERROR("Failed to read RSA public key from {}", pub_key_path);
    return std::nullopt;
  }

  return pkey;
}

bool create_self_signed_ssl_cert (const std::filesystem::path ssl_cert_path, const std::filesystem::path ssl_dh_path, const std::filesystem::path rsa_priv_key_path) {
  SPDLOG_DEBUG("Creating self-signed SSL certificate to {}/{}", ssl_cert_path, ssl_dh_path);

  constexpr long days_valid = 365;

  const auto rsa_key = load_rsa_private_key(rsa_priv_key_path);
  if ( !rsa_key ) {
    SPDLOG_ERROR("Failed to load RSA private key from {}", rsa_priv_key_path);
    return false;
  }

  const std::optional<x509_t> cert = create_self_signed_cert(*rsa_key, days_valid);
  if ( !cert ) {
    SPDLOG_ERROR("Failed to create self-signed SSL certificate");
    return false;
  }

  if ( !write_cert_to_file(*cert, ssl_cert_path) ) {
    SPDLOG_ERROR("Failed to write self-signed SSL certificate to {}", ssl_cert_path);
    return false;
  }

  const std::optional<evp_pkey_t> dh_params = generate_dh_params( );
  if ( !dh_params ) {
    SPDLOG_ERROR("Failed to generate DH parameters");
    return false;
  }
  /*
  const std::optional<evp_pkey_t> dh_keypair = generate_dh_keypair(*dh_params);
  if ( !dh_keypair ) {
    SPDLOG_ERROR("Failed to generate DH key pair");
    return false;
  }
  */

  if (!write_dh_params_to_file(*dh_params, ssl_dh_path)) {
    SPDLOG_ERROR("Failed to write DH parameters to {}", ssl_dh_path);
    return false;
  }
  /*
  if ( !write_dh_keypair_to_file(*dh_keypair, ssl_dh_path) ) {
    SPDLOG_ERROR("Failed to write DH key pair to {}", ssl_dh_path);
    return false;
  }
  */

  return true;
}
