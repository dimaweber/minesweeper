# Changelog

All notable changes to this project will be documented in this file. This library
currently lives inside, and is versioned alongside, the minesweeper repository — see
that project's own top-level `CHANGELOG.md` for the commit(s) that correspond to each
entry below, until this directory is extracted into its own repository.

## [Unreleased]

### Added

- Initial version. `envelope::envelope_t` wraps/unwraps an arbitrary byte buffer
  through a fixed, non-reorderable pipeline of optional layers: compress (zlib) →
  encrypt (AES-256-GCM) → sign (HMAC-SHA256) → base64, in that order regardless of
  which subset is enabled. The header (a 4-byte magic plus a 1-byte mask recording
  which layers were applied) is covered by the signature when signing is enabled, so a
  tampered mask can't be used to downgrade what `unwrap()` checks.
- Own `CMakeLists.txt` (static library target `envelope`), built via
  `add_subdirectory(libs/envelope)` from the parent project — not linked into any
  minesweeper target, built and tested independently.
- GTest-based test suite (`tests/`), registered with the parent project's CTest run;
  see [`tests.md`](tests.md) for what it covers.
- `readme.md`, `tests.md`, this changelog.
- `config_t::base64` changed from `bool` to `base64_mode_t` (`none`/`standard`/
  `url_safe`), adding RFC 4648 §5 "base64url" support (`-`/`_` alphabet, no padding —
  the convention JWTs/URLs/cookies use) alongside the original RFC 4648 §4 alphabet.
  `base64_decode()` accepts padded or unpadded input for either alphabet.
- `config_t::encrypt_key` changed from `std::vector<std::byte>` (runtime-checked for
  exactly 32 bytes) to `std::optional<aes_256_key_t>` (a new public
  `std::array<std::byte, 32>` alias) — the wrong-size case is now unrepresentable at
  the type level rather than a runtime error, and `std::optional` keeps "not
  configured" distinguishable from "configured to all zeros," which a bare
  `std::array` can't express on its own. `detail::aes_gcm_encrypt`/`aes_gcm_decrypt`
  likewise now take the fixed-size key type directly instead of a `std::span` plus a
  runtime length check. `sign_key` is unchanged (`std::vector<std::byte>`) - HMAC keys
  are legitimately any length, so there's no fixed size to encode in the type.
- `aes_256_key_t` changed again, from a plain `std::array<std::byte, 32>` alias to a
  small wrapper class, and `sign_key`'s field type changed from a bare
  `std::vector<std::byte>` to a new `secure_bytes_t` wrapper — both now overwrite their
  bytes with `OPENSSL_cleanse()` on destruction, so key material doesn't sit in freed
  memory once a `config_t`/`envelope_t` is done with it. `aes_256_key_t`'s move
  constructor/assignment also cleanse the moved-from copy immediately, rather than
  leaving it to whenever that object is separately destroyed - `std::array` has no real
  move semantics, so without this a "moved" key would otherwise leave a duplicate,
  uncleansed copy of itself behind. `config_t` itself deliberately stays a plain
  aggregate (its own designated-initializer syntax, `config_t{.compress = true, ...}`,
  still works unchanged) - giving it its own destructor instead would have disqualified
  it from being an aggregate at all, which is why the cleansing logic lives on these two
  field types instead. See [readme.md](readme.md)'s "Key hygiene" section.
