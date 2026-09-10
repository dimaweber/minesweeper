#include <envelope/envelope.hxx>
#include <gtest/gtest.h>

#include <cctype>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> to_bytes (const std::string& s) {
  std::vector<std::byte> out(s.size( ));
  for ( size_t i = 0; i < s.size( ); ++i ) {
    out[i] = static_cast<std::byte>(s[i]);
  }
  return out;
}

std::string to_string (std::span<const std::byte> b) {
  return std::string(reinterpret_cast<const char*>(b.data( )), b.size( ));
}

// Fixed, non-secret keys - this is test fixture material, not anything
// that protects real data.
std::vector<std::byte> test_encrypt_key ( ) {
  std::vector<std::byte> key(32);
  for ( size_t i = 0; i < key.size( ); ++i ) {
    key[i] = static_cast<std::byte>(i);
  }
  return key;
}

std::vector<std::byte> test_sign_key ( ) {
  return to_bytes("envelope-test-signing-key");
}

}  // namespace

TEST (Envelope, RoundTripNoLayers) {
  const envelope::envelope_t env(envelope::config_t {.base64 = envelope::base64_mode_t::none});
  const auto                 data = to_bytes("hello, world!");

  const auto wrapped = env.wrap(data);
  ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );

  const auto unwrapped = env.unwrap(*wrapped);
  ASSERT_TRUE(unwrapped.has_value( )) << unwrapped.error( );
  EXPECT_EQ(to_string(*unwrapped), "hello, world!");
}

TEST (Envelope, RoundTripBase64Only) {
  const envelope::envelope_t env(envelope::config_t {.base64 = envelope::base64_mode_t::standard});
  const auto                 data = to_bytes("hello, world!");

  const auto wrapped = env.wrap(data);
  ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );
  // Should actually be base64 text - printable ASCII only.
  for ( const auto b: *wrapped ) {
    EXPECT_TRUE(std::isprint(static_cast<unsigned char>(b)));
  }

  const auto unwrapped = env.unwrap(*wrapped);
  ASSERT_TRUE(unwrapped.has_value( )) << unwrapped.error( );
  EXPECT_EQ(to_string(*unwrapped), "hello, world!");
}

TEST (Envelope, UrlSafeBase64HasNoPlusSlashOrPadding) {
  // Encrypt too, so the wrapped bytes are high-entropy and near-certain to
  // contain at least one '+' or '/' if standard base64 were used instead -
  // the point of this test is distinguishing "happens to have none of
  // these characters" from "the url_safe alphabet was actually used".
  envelope::config_t cfg {.base64 = envelope::base64_mode_t::url_safe, .encrypt = true};
  cfg.encrypt_key = test_encrypt_key( );
  const envelope::envelope_t env(cfg);

  const auto wrapped = env.wrap(to_bytes(std::string(256, '\x7f')));
  ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );

  const std::string text = to_string(*wrapped);
  EXPECT_EQ(text.find('+'), std::string::npos);
  EXPECT_EQ(text.find('/'), std::string::npos);
  EXPECT_EQ(text.find('='), std::string::npos);

  const auto unwrapped = env.unwrap(*wrapped);
  ASSERT_TRUE(unwrapped.has_value( )) << unwrapped.error( );
  EXPECT_EQ(unwrapped->size( ), 256u);
}

TEST (Envelope, RoundTripCompressOnly) {
  const envelope::envelope_t env(envelope::config_t {.base64 = envelope::base64_mode_t::none, .compress = true});
  const std::string          text(4096, 'x');  // highly repetitive - should compress well
  const auto                 data = to_bytes(text);

  const auto wrapped = env.wrap(data);
  ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );
  EXPECT_LT(wrapped->size( ), data.size( )) << "4096 repeated bytes should compress smaller than the original";

  const auto unwrapped = env.unwrap(*wrapped);
  ASSERT_TRUE(unwrapped.has_value( )) << unwrapped.error( );
  EXPECT_EQ(to_string(*unwrapped), text);
}

TEST (Envelope, RoundTripEncryptOnly) {
  envelope::config_t cfg {.base64 = envelope::base64_mode_t::none, .encrypt = true};
  cfg.encrypt_key = test_encrypt_key( );
  const envelope::envelope_t env(cfg);
  const auto                 data = to_bytes("a secret message");

  const auto wrapped = env.wrap(data);
  ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );
  // Ciphertext shouldn't contain the plaintext verbatim.
  EXPECT_EQ(to_string(*wrapped).find("secret"), std::string::npos);

  const auto unwrapped = env.unwrap(*wrapped);
  ASSERT_TRUE(unwrapped.has_value( )) << unwrapped.error( );
  EXPECT_EQ(to_string(*unwrapped), "a secret message");
}

TEST (Envelope, RoundTripSignOnly) {
  envelope::config_t cfg {.base64 = envelope::base64_mode_t::none, .sign = true};
  cfg.sign_key = test_sign_key( );
  const envelope::envelope_t env(cfg);
  const auto                 data = to_bytes("signed but readable");

  const auto wrapped = env.wrap(data);
  ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );
  // Signed-but-not-encrypted: plaintext should still be visible in the wire bytes.
  EXPECT_NE(to_string(*wrapped).find("signed but readable"), std::string::npos);

  const auto unwrapped = env.unwrap(*wrapped);
  ASSERT_TRUE(unwrapped.has_value( )) << unwrapped.error( );
  EXPECT_EQ(to_string(*unwrapped), "signed but readable");
}

TEST (Envelope, RoundTripAllLayers) {
  envelope::config_t cfg {.base64 = envelope::base64_mode_t::standard, .compress = true, .encrypt = true, .sign = true};
  cfg.encrypt_key = test_encrypt_key( );
  cfg.sign_key    = test_sign_key( );
  const envelope::envelope_t env(cfg);
  const std::string          text = "The quick brown fox jumps over the lazy dog. " + std::string(200, 'z');
  const auto                 data = to_bytes(text);

  const auto wrapped = env.wrap(data);
  ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );

  const auto unwrapped = env.unwrap(*wrapped);
  ASSERT_TRUE(unwrapped.has_value( )) << unwrapped.error( );
  EXPECT_EQ(to_string(*unwrapped), text);
}

TEST (Envelope, RoundTripEmptyPayload) {
  envelope::config_t cfg {.base64 = envelope::base64_mode_t::standard, .compress = true, .encrypt = true, .sign = true};
  cfg.encrypt_key = test_encrypt_key( );
  cfg.sign_key    = test_sign_key( );
  const envelope::envelope_t env(cfg);

  const auto wrapped = env.wrap({ });
  ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );

  const auto unwrapped = env.unwrap(*wrapped);
  ASSERT_TRUE(unwrapped.has_value( )) << unwrapped.error( );
  EXPECT_TRUE(unwrapped->empty( ));
}

TEST (Envelope, RejectsTamperedPayloadWhenSigned) {
  envelope::config_t cfg {.base64 = envelope::base64_mode_t::none, .sign = true};
  cfg.sign_key = test_sign_key( );
  const envelope::envelope_t env(cfg);

  auto wrapped = env.wrap(to_bytes("do not touch me"));
  ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );

  // Flip a bit well inside the payload.
  (*wrapped)[wrapped->size( ) / 2] ^= std::byte {0x01};

  const auto unwrapped = env.unwrap(*wrapped);
  EXPECT_FALSE(unwrapped.has_value( ));
}

TEST (Envelope, RejectsTamperedMaskWhenSigned) {
  // The downgrade-attack case: flipping the mask byte (e.g. to turn the
  // sign bit off) must be caught by signature verification too, since the
  // mask is itself covered by the signature - not just the payload.
  envelope::config_t cfg {.base64 = envelope::base64_mode_t::none, .sign = true};
  cfg.sign_key = test_sign_key( );
  const envelope::envelope_t env(cfg);

  auto wrapped = env.wrap(to_bytes("payload"));
  ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );

  // Byte 4 is the mask (0..3 are the "ENV1" magic).
  (*wrapped)[4] ^= std::byte {0xFF};

  const auto unwrapped = env.unwrap(*wrapped);
  EXPECT_FALSE(unwrapped.has_value( ));
}

TEST (Envelope, RejectsWrongSignKey) {
  envelope::config_t write_cfg {.base64 = envelope::base64_mode_t::none, .sign = true};
  write_cfg.sign_key = test_sign_key( );
  const envelope::envelope_t writer(write_cfg);

  envelope::config_t read_cfg {.base64 = envelope::base64_mode_t::none, .sign = true};
  read_cfg.sign_key = to_bytes("a completely different key");
  const envelope::envelope_t reader(read_cfg);

  const auto wrapped = writer.wrap(to_bytes("payload"));
  ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );

  const auto unwrapped = reader.unwrap(*wrapped);
  EXPECT_FALSE(unwrapped.has_value( ));
}

TEST (Envelope, RejectsWrongEncryptKey) {
  envelope::config_t write_cfg {.base64 = envelope::base64_mode_t::none, .encrypt = true};
  write_cfg.encrypt_key = test_encrypt_key( );
  const envelope::envelope_t writer(write_cfg);

  envelope::config_t read_cfg {.base64 = envelope::base64_mode_t::none, .encrypt = true};
  read_cfg.encrypt_key = std::vector<std::byte>(32, std::byte {0xAB});
  const envelope::envelope_t reader(read_cfg);

  const auto wrapped = writer.wrap(to_bytes("payload"));
  ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );

  const auto unwrapped = reader.unwrap(*wrapped);
  EXPECT_FALSE(unwrapped.has_value( ));
}

TEST (Envelope, RejectsBadMagic) {
  const envelope::envelope_t env(envelope::config_t {.base64 = envelope::base64_mode_t::none});

  auto wrapped = env.wrap(to_bytes("payload"));
  ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );
  (*wrapped)[0] ^= std::byte {0xFF};

  const auto unwrapped = env.unwrap(*wrapped);
  EXPECT_FALSE(unwrapped.has_value( ));
}

TEST (Envelope, RejectsMissingSignKeyOnUnwrapOfSignedBlob) {
  envelope::config_t write_cfg {.base64 = envelope::base64_mode_t::none, .sign = true};
  write_cfg.sign_key = test_sign_key( );
  const envelope::envelope_t writer(write_cfg);

  const envelope::envelope_t reader(envelope::config_t {.base64 = envelope::base64_mode_t::none});  // no sign_key configured

  const auto wrapped = writer.wrap(to_bytes("payload"));
  ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );

  const auto unwrapped = reader.unwrap(*wrapped);
  EXPECT_FALSE(unwrapped.has_value( ));
}

TEST (Envelope, WrapRejectsEncryptWithoutAValidKey) {
  const envelope::envelope_t env(envelope::config_t {.base64 = envelope::base64_mode_t::none, .encrypt = true});  // encrypt_key left empty
  const auto                 wrapped = env.wrap(to_bytes("payload"));
  EXPECT_FALSE(wrapped.has_value( ));
}

TEST (Envelope, WrapRejectsSignWithoutAKey) {
  const envelope::envelope_t env(envelope::config_t {.base64 = envelope::base64_mode_t::none, .sign = true});  // sign_key left empty
  const auto                 wrapped = env.wrap(to_bytes("payload"));
  EXPECT_FALSE(wrapped.has_value( ));
}

TEST (Envelope, InstanceIsReusableAcrossCalls) {
  const envelope::envelope_t env(envelope::config_t {.base64 = envelope::base64_mode_t::none, .compress = true});

  for ( int i = 0; i < 5; ++i ) {
    const auto text    = "message number " + std::to_string(i);
    const auto wrapped = env.wrap(to_bytes(text));
    ASSERT_TRUE(wrapped.has_value( )) << wrapped.error( );
    const auto unwrapped = env.unwrap(*wrapped);
    ASSERT_TRUE(unwrapped.has_value( )) << unwrapped.error( );
    EXPECT_EQ(to_string(*unwrapped), text);
  }
}
