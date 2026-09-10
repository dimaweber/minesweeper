#include "compress.hxx"

#include <zlib.h>

#include <cstdint>
#include <stdexcept>

namespace envelope::detail {

namespace {
constexpr size_t size_prefix_bytes = 8;

// A hostile or corrupted size prefix could otherwise claim an enormous
// uncompressed size, making decompress() allocate gigabytes before zlib
// ever gets a chance to reject the actual (much smaller, malformed) input -
// a decompression-bomb-via-size-lie. This cap is independent of, and in
// addition to, envelope_t's own sign-before-decompress ordering (which
// keeps a *tampered* blob from reaching this code at all when signing is
// enabled) - compression can be used on its own, with no signature to
// protect it.
constexpr uint64_t max_reasonable_uncompressed_size = 256ull * 1024 * 1024;

void append_u64_le (std::vector<std::byte>& out, uint64_t v) {
  for ( int i = 0; i < 8; ++i ) {
    out.push_back(static_cast<std::byte>((v >> (i * 8)) & 0xFF));
  }
}

uint64_t read_u64_le (std::span<const std::byte> b) {
  uint64_t v = 0;
  for ( int i = 0; i < 8; ++i ) {
    v |= static_cast<uint64_t>(b[i]) << (i * 8);
  }
  return v;
}
}  // namespace

std::vector<std::byte> zlib_compress (std::span<const std::byte> data) {
  uLongf                  bound = compressBound(static_cast<uLong>(data.size( )));
  std::vector<std::byte>  compressed(bound);
  uLongf                  dest_len = bound;
  const int               rc       = compress2(reinterpret_cast<Bytef*>(compressed.data( )), &dest_len, reinterpret_cast<const Bytef*>(data.data( )), static_cast<uLong>(data.size( )), Z_BEST_COMPRESSION);
  // compress2() into a buffer sized via compressBound() only fails on
  // Z_MEM_ERROR (allocation failure) - not a recoverable condition this
  // library's callers can meaningfully act on, so treat it like any other
  // out-of-memory failure elsewhere in C++ (throw, don't silently corrupt).
  if ( rc != Z_OK ) {
    throw std::runtime_error("envelope: zlib compression failed unexpectedly");
  }
  compressed.resize(dest_len);

  std::vector<std::byte> framed;
  framed.reserve(size_prefix_bytes + compressed.size( ));
  append_u64_le(framed, data.size( ));
  framed.insert(framed.end( ), compressed.begin( ), compressed.end( ));
  return framed;
}

std::expected<std::vector<std::byte>, std::string> zlib_decompress (std::span<const std::byte> data) {
  if ( data.size( ) < size_prefix_bytes ) {
    return std::unexpected("envelope: compressed payload too short for its size prefix.");
  }

  const uint64_t original_size = read_u64_le(data);
  if ( original_size > max_reasonable_uncompressed_size ) {
    return std::unexpected("envelope: claimed decompressed size is implausibly large.");
  }

  std::vector<std::byte> out(original_size);
  uLongf                 dest_len = static_cast<uLongf>(original_size);
  const int              rc       = uncompress(reinterpret_cast<Bytef*>(out.data( )), &dest_len, reinterpret_cast<const Bytef*>(data.data( ) + size_prefix_bytes), static_cast<uLong>(data.size( ) - size_prefix_bytes));
  if ( rc != Z_OK ) {
    return std::unexpected("envelope: decompression failed - corrupt input or a size-prefix mismatch.");
  }
  out.resize(dest_len);
  return out;
}

}  // namespace envelope::detail
