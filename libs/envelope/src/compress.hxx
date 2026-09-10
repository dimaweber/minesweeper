#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace envelope::detail {

// zlib deflate/inflate. The compressed form is prefixed with the original
// (uncompressed) size as a fixed 8-byte little-endian integer - zlib's
// simple uncompress() API needs to know the destination buffer size up
// front, and storing it ourselves is simpler and more robust than either
// guessing a buffer size or reaching for zlib's streaming inflate API for
// what's otherwise a one-shot operation.
[[nodiscard]] std::vector<std::byte> zlib_compress (std::span<const std::byte> data);
[[nodiscard]] std::expected<std::vector<std::byte>, std::string> zlib_decompress (std::span<const std::byte> data);

}  // namespace envelope::detail
