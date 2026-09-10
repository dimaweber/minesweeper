#include "base64.hxx"

#include <array>
#include <cstdint>

namespace envelope::detail {

namespace {
constexpr char encode_table[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/',
};

constexpr std::array<int8_t, 256> make_decode_table ( ) {
  std::array<int8_t, 256> table {};
  for ( auto& v: table ) {
    v = -1;
  }
  for ( int i = 0; i < 64; ++i ) {
    table[static_cast<unsigned char>(encode_table[i])] = static_cast<int8_t>(i);
  }
  return table;
}

constexpr std::array<int8_t, 256> decode_table = make_decode_table( );
}  // namespace

std::vector<std::byte> base64_encode (std::span<const std::byte> data) {
  std::vector<std::byte> out;
  out.reserve(((data.size( ) + 2) / 3) * 4);

  size_t i = 0;
  for ( ; i + 3 <= data.size( ); i += 3 ) {
    const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) | static_cast<uint32_t>(data[i + 2]);
    out.push_back(std::byte {static_cast<uint8_t>(encode_table[(n >> 18) & 0x3F])});
    out.push_back(std::byte {static_cast<uint8_t>(encode_table[(n >> 12) & 0x3F])});
    out.push_back(std::byte {static_cast<uint8_t>(encode_table[(n >> 6) & 0x3F])});
    out.push_back(std::byte {static_cast<uint8_t>(encode_table[n & 0x3F])});
  }

  const size_t remaining = data.size( ) - i;
  if ( remaining == 1 ) {
    const uint32_t n = static_cast<uint32_t>(data[i]) << 16;
    out.push_back(std::byte {static_cast<uint8_t>(encode_table[(n >> 18) & 0x3F])});
    out.push_back(std::byte {static_cast<uint8_t>(encode_table[(n >> 12) & 0x3F])});
    out.push_back(std::byte {'='});
    out.push_back(std::byte {'='});
  } else if ( remaining == 2 ) {
    const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
    out.push_back(std::byte {static_cast<uint8_t>(encode_table[(n >> 18) & 0x3F])});
    out.push_back(std::byte {static_cast<uint8_t>(encode_table[(n >> 12) & 0x3F])});
    out.push_back(std::byte {static_cast<uint8_t>(encode_table[(n >> 6) & 0x3F])});
    out.push_back(std::byte {'='});
  }

  return out;
}

std::expected<std::vector<std::byte>, std::string> base64_decode (std::span<const std::byte> data) {
  if ( data.empty( ) ) {
    return std::vector<std::byte> {};
  }
  if ( data.size( ) % 4 != 0 ) {
    return std::unexpected("envelope: base64 input length is not a multiple of 4.");
  }

  std::vector<std::byte> out;
  out.reserve((data.size( ) / 4) * 3);

  for ( size_t i = 0; i < data.size( ); i += 4 ) {
    int8_t vals[4] {};
    int    pad = 0;
    for ( int j = 0; j < 4; ++j ) {
      const auto c = static_cast<unsigned char>(data[i + j]);
      if ( c == '=' ) {
        vals[j] = 0;
        ++pad;
      } else {
        if ( pad > 0 ) {
          return std::unexpected("envelope: base64 padding is only valid at the end of the input.");
        }
        const int8_t v = decode_table[c];
        if ( v < 0 ) {
          return std::unexpected("envelope: invalid base64 character.");
        }
        vals[j] = v;
      }
    }
    if ( pad > 2 ) {
      return std::unexpected("envelope: too much base64 padding.");
    }

    const uint32_t n = (static_cast<uint32_t>(vals[0]) << 18) | (static_cast<uint32_t>(vals[1]) << 12) | (static_cast<uint32_t>(vals[2]) << 6) | static_cast<uint32_t>(vals[3]);
    out.push_back(std::byte {static_cast<uint8_t>((n >> 16) & 0xFF)});
    if ( pad < 2 ) {
      out.push_back(std::byte {static_cast<uint8_t>((n >> 8) & 0xFF)});
    }
    if ( pad < 1 ) {
      out.push_back(std::byte {static_cast<uint8_t>(n & 0xFF)});
    }
  }

  return out;
}

}  // namespace envelope::detail
