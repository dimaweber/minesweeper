#include "envelope/envelope.hxx"

#include <algorithm>
#include <array>

#include "base64.hxx"
#include "compress.hxx"
#include "crypto.hxx"

namespace envelope {

namespace {
// 'E','N','V','1' - a fixed 4-byte sequence rather than a numeric constant,
// so there's no endianness to get wrong writing or comparing it.
constexpr std::array<std::byte, 4> magic_bytes = {std::byte {'E'}, std::byte {'N'}, std::byte {'V'}, std::byte {'1'}};

constexpr uint8_t mask_compress = 1u << 0;
constexpr uint8_t mask_encrypt  = 1u << 1;
constexpr uint8_t mask_sign     = 1u << 2;

constexpr size_t header_size = magic_bytes.size( ) + 1;  // magic + mask byte

// base64_mode_t::none never reaches here - callers only call this for the
// two modes that are actually base64.
constexpr detail::base64_alphabet_t alphabet_for (base64_mode_t mode) {
  return mode == base64_mode_t::url_safe ? detail::base64_alphabet_t::url_safe : detail::base64_alphabet_t::standard;
}
}  // namespace

envelope_t::envelope_t (config_t config) : config_(std::move(config)) {
}

std::expected<std::vector<std::byte>, std::string> envelope_t::wrap (std::span<const std::byte> data) const {
  if ( config_.encrypt && config_.encrypt_key.size( ) != detail::aes_gcm_key_size ) {
    return std::unexpected("envelope_t::wrap: encrypt is enabled but encrypt_key isn't 32 bytes.");
  }
  if ( config_.sign && config_.sign_key.empty( ) ) {
    return std::unexpected("envelope_t::wrap: sign is enabled but sign_key is empty.");
  }

  std::vector<std::byte> payload(data.begin( ), data.end( ));

  if ( config_.compress ) {
    payload = detail::zlib_compress(payload);
  }

  if ( config_.encrypt ) {
    auto encrypted = detail::aes_gcm_encrypt(config_.encrypt_key, payload);
    if ( !encrypted ) {
      return std::unexpected(std::move(encrypted.error( )));
    }
    payload = std::move(*encrypted);
  }

  // The mask has to be complete - including the sign bit - before it's
  // written, since (when signing is on) the signature computed below
  // covers this header too. Setting the sign bit only after signing would
  // leave it outside what's actually authenticated - exactly the downgrade
  // hole described in envelope.hxx.
  const uint8_t mask = static_cast<uint8_t>((config_.compress ? mask_compress : 0) | (config_.encrypt ? mask_encrypt : 0) | (config_.sign ? mask_sign : 0));

  std::vector<std::byte> framed;
  framed.reserve(header_size + payload.size( ) + (config_.sign ? detail::hmac_sha256_tag_size : 0));
  framed.insert(framed.end( ), magic_bytes.begin( ), magic_bytes.end( ));
  framed.push_back(static_cast<std::byte>(mask));
  framed.insert(framed.end( ), payload.begin( ), payload.end( ));

  if ( config_.sign ) {
    const auto tag = detail::hmac_sha256(config_.sign_key, framed);
    framed.insert(framed.end( ), tag.begin( ), tag.end( ));
  }

  if ( config_.base64 != base64_mode_t::none ) {
    return detail::base64_encode(framed, alphabet_for(config_.base64));
  }
  return framed;
}

std::expected<std::vector<std::byte>, std::string> envelope_t::unwrap (std::span<const std::byte> data) const {
  std::vector<std::byte> framed;
  if ( config_.base64 != base64_mode_t::none ) {
    auto decoded = detail::base64_decode(data, alphabet_for(config_.base64));
    if ( !decoded ) {
      return std::unexpected(std::move(decoded.error( )));
    }
    framed = std::move(*decoded);
  } else {
    framed.assign(data.begin( ), data.end( ));
  }

  if ( framed.size( ) < header_size ) {
    return std::unexpected("envelope_t::unwrap: buffer too short to contain a header.");
  }
  if ( !std::equal(magic_bytes.begin( ), magic_bytes.end( ), framed.begin( )) ) {
    return std::unexpected("envelope_t::unwrap: bad magic number - not an envelope, or an incompatible version.");
  }
  const uint8_t mask = static_cast<uint8_t>(framed[magic_bytes.size( )]);

  // Everything from here on is bounded by body_end, which shrinks to
  // exclude the trailing signature *before* that signature is verified -
  // so nothing past this point ever looks at, let alone acts on, bytes
  // that weren't covered by a successful verification.
  size_t body_end = framed.size( );

  if ( mask & mask_sign ) {
    if ( config_.sign_key.empty( ) ) {
      return std::unexpected("envelope_t::unwrap: blob is signed but no sign_key is configured.");
    }
    if ( framed.size( ) < header_size + detail::hmac_sha256_tag_size ) {
      return std::unexpected("envelope_t::unwrap: buffer too short to contain a signature.");
    }
    body_end = framed.size( ) - detail::hmac_sha256_tag_size;

    const std::span<const std::byte> signed_part(framed.data( ), body_end);
    const std::span<const std::byte> got_tag(framed.data( ) + body_end, detail::hmac_sha256_tag_size);
    const auto                       expected_tag = detail::hmac_sha256(config_.sign_key, signed_part);
    if ( !detail::constant_time_equal(expected_tag, got_tag) ) {
      return std::unexpected("envelope_t::unwrap: signature verification failed.");
    }
  }

  std::vector<std::byte> payload(framed.begin( ) + static_cast<ptrdiff_t>(header_size), framed.begin( ) + static_cast<ptrdiff_t>(body_end));

  if ( mask & mask_encrypt ) {
    if ( config_.encrypt_key.size( ) != detail::aes_gcm_key_size ) {
      return std::unexpected("envelope_t::unwrap: blob is encrypted but no valid encrypt_key is configured.");
    }
    auto decrypted = detail::aes_gcm_decrypt(config_.encrypt_key, payload);
    if ( !decrypted ) {
      return std::unexpected(std::move(decrypted.error( )));
    }
    payload = std::move(*decrypted);
  }

  if ( mask & mask_compress ) {
    auto decompressed = detail::zlib_decompress(payload);
    if ( !decompressed ) {
      return std::unexpected(std::move(decompressed.error( )));
    }
    payload = std::move(*decompressed);
  }

  return payload;
}

}  // namespace envelope
