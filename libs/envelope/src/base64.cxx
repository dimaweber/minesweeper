#include "base64.hxx"

#include <array>
#include <cstdint>

namespace envelope::detail {

namespace {
constexpr char standard_table[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/',
};

// Identical to standard_table except for the last two entries - the only
// two characters RFC 4648 §5 actually changes.
constexpr char url_safe_table[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', '_',
};

constexpr const char* encode_table_for (base64_alphabet_t alphabet) {
  return alphabet == base64_alphabet_t::url_safe ? url_safe_table : standard_table;
}

constexpr std::array<int8_t, 256> make_decode_table (const char (&table)[64]) {
  std::array<int8_t, 256> out {};
  for ( auto& v: out ) {
    v = -1;
  }
  for ( int i = 0; i < 64; ++i ) {
    out[static_cast<unsigned char>(table[i])] = static_cast<int8_t>(i);
  }
  return out;
}

constexpr std::array<int8_t, 256> standard_decode_table = make_decode_table(standard_table);
constexpr std::array<int8_t, 256> url_safe_decode_table = make_decode_table(url_safe_table);

constexpr const std::array<int8_t, 256>& decode_table_for (base64_alphabet_t alphabet) {
  return alphabet == base64_alphabet_t::url_safe ? url_safe_decode_table : standard_decode_table;
}
}  // namespace

std::vector<std::byte> base64_encode (std::span<const std::byte> data, base64_alphabet_t alphabet) {
  const char* table       = encode_table_for(alphabet);
  const bool  use_padding = alphabet == base64_alphabet_t::standard;

  std::vector<std::byte> out;
  out.reserve(((data.size( ) + 2) / 3) * 4);

  size_t i = 0;
  for ( ; i + 3 <= data.size( ); i += 3 ) {
    const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) | static_cast<uint32_t>(data[i + 2]);
    out.push_back(std::byte {static_cast<uint8_t>(table[(n >> 18) & 0x3F])});
    out.push_back(std::byte {static_cast<uint8_t>(table[(n >> 12) & 0x3F])});
    out.push_back(std::byte {static_cast<uint8_t>(table[(n >> 6) & 0x3F])});
    out.push_back(std::byte {static_cast<uint8_t>(table[n & 0x3F])});
  }

  const size_t remaining = data.size( ) - i;
  if ( remaining == 1 ) {
    const uint32_t n = static_cast<uint32_t>(data[i]) << 16;
    out.push_back(std::byte {static_cast<uint8_t>(table[(n >> 18) & 0x3F])});
    out.push_back(std::byte {static_cast<uint8_t>(table[(n >> 12) & 0x3F])});
    if ( use_padding ) {
      out.push_back(std::byte {'='});
      out.push_back(std::byte {'='});
    }
  } else if ( remaining == 2 ) {
    const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8);
    out.push_back(std::byte {static_cast<uint8_t>(table[(n >> 18) & 0x3F])});
    out.push_back(std::byte {static_cast<uint8_t>(table[(n >> 12) & 0x3F])});
    out.push_back(std::byte {static_cast<uint8_t>(table[(n >> 6) & 0x3F])});
    if ( use_padding ) {
      out.push_back(std::byte {'='});
    }
  }

  return out;
}

std::expected<std::vector<std::byte>, std::string> base64_decode (std::span<const std::byte> data, base64_alphabet_t alphabet) {
  if ( data.empty( ) ) {
    return std::vector<std::byte> {};
  }

  const auto& dtable = decode_table_for(alphabet);

  // Padding (if any - "=" is only ever valid trailing this class of
  // encoding) is stripped up front so the rest of this function handles
  // padded (standard) and unpadded (url_safe) input identically, driven
  // purely by how many real characters are left.
  size_t len = data.size( );
  while ( len > 0 && static_cast<char>(data[len - 1]) == '=' ) {
    --len;
  }
  if ( data.size( ) - len > 2 ) {
    return std::unexpected("envelope: too much base64 padding.");
  }
  if ( len % 4 == 1 ) {
    return std::unexpected("envelope: invalid base64 input length.");
  }

  auto decode_char = [&dtable] (std::byte b) -> std::expected<int8_t, std::string> {
    const int8_t v = dtable[static_cast<unsigned char>(b)];
    if ( v < 0 ) {
      return std::unexpected("envelope: invalid base64 character.");
    }
    return v;
  };

  std::vector<std::byte> out;
  out.reserve((len / 4 + 1) * 3);

  size_t i = 0;
  for ( ; i + 4 <= len; i += 4 ) {
    int8_t vals[4];
    for ( int j = 0; j < 4; ++j ) {
      const auto v = decode_char(data[i + j]);
      if ( !v ) {
        return std::unexpected(v.error( ));
      }
      vals[j] = *v;
    }
    const uint32_t n = (static_cast<uint32_t>(vals[0]) << 18) | (static_cast<uint32_t>(vals[1]) << 12) | (static_cast<uint32_t>(vals[2]) << 6) | static_cast<uint32_t>(vals[3]);
    out.push_back(std::byte {static_cast<uint8_t>((n >> 16) & 0xFF)});
    out.push_back(std::byte {static_cast<uint8_t>((n >> 8) & 0xFF)});
    out.push_back(std::byte {static_cast<uint8_t>(n & 0xFF)});
  }

  const size_t tail = len - i;
  if ( tail == 2 ) {
    const auto v0 = decode_char(data[i]);
    const auto v1 = decode_char(data[i + 1]);
    if ( !v0 ) return std::unexpected(v0.error( ));
    if ( !v1 ) return std::unexpected(v1.error( ));
    const uint32_t n = (static_cast<uint32_t>(*v0) << 18) | (static_cast<uint32_t>(*v1) << 12);
    out.push_back(std::byte {static_cast<uint8_t>((n >> 16) & 0xFF)});
  } else if ( tail == 3 ) {
    const auto v0 = decode_char(data[i]);
    const auto v1 = decode_char(data[i + 1]);
    const auto v2 = decode_char(data[i + 2]);
    if ( !v0 ) return std::unexpected(v0.error( ));
    if ( !v1 ) return std::unexpected(v1.error( ));
    if ( !v2 ) return std::unexpected(v2.error( ));
    const uint32_t n = (static_cast<uint32_t>(*v0) << 18) | (static_cast<uint32_t>(*v1) << 12) | (static_cast<uint32_t>(*v2) << 6);
    out.push_back(std::byte {static_cast<uint8_t>((n >> 16) & 0xFF)});
    out.push_back(std::byte {static_cast<uint8_t>((n >> 8) & 0xFF)});
  }

  return out;
}

}  // namespace envelope::detail
