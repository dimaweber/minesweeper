#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace envelope::detail {

// Purely a binary-to-text transport encoding - no security property of its
// own, which is why it's implemented independently of (and doesn't need to
// know anything about) compress/crypto.hxx. The `none` case in
// envelope::base64_mode_t (envelope.hxx) never reaches this module at all -
// envelope.cxx only calls here for the two alphabets that are actually
// base64.
enum class base64_alphabet_t {
  standard,  // RFC 4648 §4: "+/" , "=" padding.
  url_safe,  // RFC 4648 §5: "-_" , no padding (the convention JWTs/RFC 7515 use).
};

[[nodiscard]] std::vector<std::byte> base64_encode (std::span<const std::byte> data, base64_alphabet_t alphabet);

// Accepts input with or without "=" padding regardless of `alphabet` -
// there's exactly one canonical decoded value for a given (unpadded)
// character sequence either way, so being lenient about padding on the way
// in costs nothing and avoids a caller-mismatch footgun (e.g. a
// standard-encoded blob that got its padding stripped somewhere in
// transit).
[[nodiscard]] std::expected<std::vector<std::byte>, std::string> base64_decode (std::span<const std::byte> data, base64_alphabet_t alphabet);

}  // namespace envelope::detail
