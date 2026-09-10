#pragma once

// This header, and everything under libs/envelope/, must never include or
// link against anything from the rest of this repository (src/**). The
// point of keeping it this way is that it can be lifted out into its own
// project unmodified - its only dependencies are third-party (OpenSSL,
// zlib), the same kind any consumer of a standalone byte-wrapper library
// would need anyway.

#include <openssl/crypto.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace envelope {

inline constexpr size_t aes_256_key_size = 32;

// A 32-byte key that overwrites itself with OPENSSL_cleanse() - not a
// plain memset()/loop, which a compiler is free to optimize away as a
// dead store once it can prove the buffer is about to be destroyed;
// OPENSSL_cleanse() is specifically written to defeat that - on
// destruction, so key material doesn't just sit in freed memory
// (recoverable from a core dump, a use-after-free, or - if this process's
// pages ever get paged out - a swap file) once it's no longer needed.
//
// std::array has no real move semantics (there's no pointer to steal;
// "moving" one just copies its elements), so a plain destructor-only fix
// would still leave a moved-from copy of the key sitting uncleansed in
// whatever memory the moved-from object occupied, until *that* object was
// separately destroyed. Move here cleanses the source proactively instead
// of just waiting for its own destructor, so a duplicate copy of the key
// doesn't linger any longer than it has to.
class aes_256_key_t {
public:
  aes_256_key_t ( ) = default;
  aes_256_key_t (const std::array<std::byte, aes_256_key_size>& bytes) : bytes_(bytes) {  // NOLINT: implicit by design
  }

  aes_256_key_t (const aes_256_key_t&)            = default;
  aes_256_key_t& operator= (const aes_256_key_t&) = default;

  aes_256_key_t (aes_256_key_t&& other) noexcept : bytes_(other.bytes_) {
    OPENSSL_cleanse(other.bytes_.data( ), other.bytes_.size( ));
  }

  aes_256_key_t& operator= (aes_256_key_t&& other) noexcept {
    if ( this != &other ) {
      bytes_ = other.bytes_;
      OPENSSL_cleanse(other.bytes_.data( ), other.bytes_.size( ));
    }
    return *this;
  }

  ~aes_256_key_t ( ) {
    OPENSSL_cleanse(bytes_.data( ), bytes_.size( ));
  }

  [[nodiscard]] std::byte&       operator[] (size_t i) noexcept { return bytes_[i]; }
  [[nodiscard]] const std::byte& operator[] (size_t i) const noexcept { return bytes_[i]; }

  void fill (std::byte v) noexcept {
    bytes_.fill(v);
  }

  [[nodiscard]] constexpr size_t size ( ) const noexcept {
    return aes_256_key_size;
  }

  // For handing to code (crypto.hxx) that only needs a read-only view and
  // has no business owning - let alone cleansing - a copy of its own.
  [[nodiscard]] const std::array<std::byte, aes_256_key_size>& bytes ( ) const noexcept {
    return bytes_;
  }

private:
  std::array<std::byte, aes_256_key_size> bytes_ {};
};

// A std::vector<std::byte> with the same on-destruction OPENSSL_cleanse()
// treatment as aes_256_key_t above - used for sign_key (config_t below),
// which (unlike encrypt_key) is legitimately variable-length, so it can't
// be a fixed-size array. Unlike aes_256_key_t, std::vector's move already
// transfers ownership cleanly (the moved-from vector is left empty, with
// nothing of the key left behind in its old buffer), so only copy and
// destruction need any special handling here.
class secure_bytes_t {
public:
  secure_bytes_t ( ) = default;
  secure_bytes_t (std::vector<std::byte> bytes) : bytes_(std::move(bytes)) {  // NOLINT: implicit by design
  }

  secure_bytes_t(const secure_bytes_t&)                = default;
  secure_bytes_t& operator= (const secure_bytes_t&)     = default;
  secure_bytes_t(secure_bytes_t&&) noexcept             = default;
  secure_bytes_t& operator= (secure_bytes_t&&) noexcept = default;

  ~secure_bytes_t ( ) {
    OPENSSL_cleanse(bytes_.data( ), bytes_.size( ));
  }

  [[nodiscard]] bool   empty ( ) const noexcept { return bytes_.empty( ); }
  [[nodiscard]] size_t size ( ) const noexcept { return bytes_.size( ); }

  [[nodiscard]] const std::vector<std::byte>& bytes ( ) const noexcept {
    return bytes_;
  }

private:
  std::vector<std::byte> bytes_;
};

// Whether, and how, wrap()/unwrap() go through base64 - a transport-framing
// choice (does this need to be text-safe, e.g. embedded in JSON or a URL,
// or can it be raw bytes on a binary socket), not a protective layer, which
// is why - unlike compress/encrypt/sign - it isn't part of the header's
// mask: the two sides of a conversation are expected to agree on this out
// of band, the same way they agree on which keys to use.
enum class base64_mode_t {
  none,      // raw bytes in, raw bytes out - no text encoding at all.
  standard,  // RFC 4648 §4: "+/" alphabet, "=" padding.
  url_safe,  // RFC 4648 §5: "-_" alphabet, no padding - safe to drop directly
             // into a URL, a cookie, or a JWT-style token without further
             // escaping, which is the whole reason this variant exists.
};

// Which optional protective layers wrap() applies - and, self-described by
// the header it writes, which ones unwrap() looks for. A receiver never
// needs to separately know or repeat which layers a sender used; it only
// needs the key(s) for whichever ones the header says were actually used.
struct config_t {
  base64_mode_t base64 = base64_mode_t::standard;

  bool compress = false;

  // AES-256-GCM. Always exactly 32 bytes when present - encrypt_key's type
  // makes the wrong-size case unrepresentable, so std::optional is what's
  // left to say "not configured" (a default-constructed aes_256_key_t is
  // all zeros, which is a weak key, not an absent one - not the same
  // thing, and worth keeping distinguishable). Required iff encrypt is
  // true.
  bool                          encrypt = false;
  std::optional<aes_256_key_t>  encrypt_key;

  // HMAC-SHA256. Unlike encrypt_key, there's no single correct size to
  // encode in the type - HMAC keys are legitimately any length - so this
  // stays a std::vector, and empty (rather than std::optional) is enough
  // to mean "not configured": a genuine zero-length HMAC key isn't a
  // meaningful thing to intentionally use. Required (non-empty) iff sign
  // is true.
  bool           sign = false;
  secure_bytes_t sign_key;
};

// Wraps/unwraps an arbitrary byte buffer through a fixed pipeline of
// optional layers - compress, then encrypt, then sign, then base64 - in
// that order, every time, regardless of which subset config_t enables.
// That order is load-bearing, not a style choice:
//
//   - compress has to run before encrypt: encrypted bytes are
//     high-entropy/incompressible, so compressing after encrypting buys
//     nothing.
//   - sign has to run after encrypt, over the ciphertext (and the header),
//     not over the plaintext before encryption ("encrypt-then-MAC", not
//     "MAC-then-encrypt"). That way unwrap() verifies the signature as the
//     very first thing it does with the payload bytes - before decrypt or
//     decompress ever touches them - so a tampered or forged blob is
//     rejected outright instead of being fed into the decryption routine
//     (the class of bug behind padding-oracle attacks) or the decompressor
//     (a decompression-bomb vector on unauthenticated input).
//   - the signature covers the header's magic+mask too, not just the
//     payload: otherwise an attacker could flip a mask bit - e.g. turn off
//     the "signed" bit - to talk a receiver into skipping verification
//     entirely, the same class of downgrade attack as JWT's infamous
//     "alg: none".
//
// Stateless and reusable: unlike parameter_bytestream_t elsewhere in this
// repo, one envelope_t instance is safe to call wrap()/unwrap() on
// repeatedly - each call is independent, nothing here tracks an
// in-progress buffer.
class envelope_t {
public:
  explicit envelope_t (config_t config);

  [[nodiscard]] std::expected<std::vector<std::byte>, std::string> wrap (std::span<const std::byte> data) const;
  [[nodiscard]] std::expected<std::vector<std::byte>, std::string> unwrap (std::span<const std::byte> data) const;

private:
  config_t config_;
};

}  // namespace envelope
