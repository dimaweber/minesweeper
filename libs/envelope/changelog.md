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
