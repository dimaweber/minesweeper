#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace envelope::detail {

// Standard RFC 4648 base64 (the "+/" alphabet, "=" padding) - purely a
// binary-to-text transport encoding, no security property of its own,
// which is why it's implemented independently of (and doesn't need to
// know anything about) compress/crypto.hxx.
[[nodiscard]] std::vector<std::byte> base64_encode (std::span<const std::byte> data);
[[nodiscard]] std::expected<std::vector<std::byte>, std::string> base64_decode (std::span<const std::byte> data);

}  // namespace envelope::detail
