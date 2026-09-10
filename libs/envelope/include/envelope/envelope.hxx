#pragma once

// This header, and everything under libs/envelope/, must never include or
// link against anything from the rest of this repository (src/**). The
// point of keeping it this way is that it can be lifted out into its own
// project unmodified - its only dependencies are third-party (OpenSSL,
// zlib), the same kind any consumer of a standalone byte-wrapper library
// would need anyway.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace envelope {

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

  // AES-256-GCM. encrypt_key must be exactly 32 bytes iff encrypt is true.
  bool                   encrypt = false;
  std::vector<std::byte> encrypt_key;

  // HMAC-SHA256. sign_key must be non-empty iff sign is true.
  bool                   sign = false;
  std::vector<std::byte> sign_key;
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
