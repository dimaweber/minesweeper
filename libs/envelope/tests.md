# Testing

`envelope`'s test suite is GTest-based, lives in `tests/envelope.unittest.cxx`, and
currently builds as part of the parent (minesweeper) project's CMake configuration —
see "Standalone build" below for what's missing to build it outside that project.

## Building and running (inside the parent project)

```
cmake --build build --target envelope_test
```

Then either run it directly:

```
./build/libs/envelope/tests/envelope_test
./build/libs/envelope/tests/envelope_test --gtest_filter='Envelope.RejectsTamperedMaskWhenSigned'
```

or through CTest, alongside every other test in the parent project:

```
ctest --test-dir build -R Envelope --output-on-failure
```

Building the tests at all is gated by `ENVELOPE_BUILD_TESTS`
(`libs/envelope/CMakeLists.txt`), which defaults to whatever the parent project's
`BUILD_TESTS` option is set to — pass `-DENVELOPE_BUILD_TESTS=OFF` to build the
`envelope` library without its tests regardless of that.

## What's covered

- Round trips: no layers, base64 only, compress only, encrypt only, sign only, all four
  combined, and an empty payload.
- Tamper detection: flipping a payload byte, and — the case that specifically exercises
  the mask-downgrade defense described in [`readme.md`](readme.md) — flipping the mask
  byte itself, both correctly rejected once signing is enabled.
- Wrong key rejected, independently, for both `encrypt_key` and `sign_key`.
- Bad magic number rejected.
- Unwrapping a signed blob with no `sign_key` configured is rejected, not silently
  treated as unsigned.
- `wrap()` refuses to proceed if `encrypt`/`sign` is requested without a validly-sized
  key, rather than silently skipping the layer.
- One `envelope_t` instance reused across several `wrap()`/`unwrap()` calls, confirming
  it's safe to keep around rather than reconstructing per call.

## Standalone build

`tests/CMakeLists.txt` currently links against whatever `GTest::gtest_main` target the
*parent* project's top-level `CMakeLists.txt` already located (via `find_package`/
`FetchContent`) — there is no `find_package(GTest)`/`FetchContent` call inside
`libs/envelope/` itself yet. Building this directory as its own standalone top-level
project (outside minesweeper) will need that added to `libs/envelope/CMakeLists.txt` or
`tests/CMakeLists.txt` before `tests/` will configure on its own. The `envelope` library
target itself has no such gap — its only dependencies (OpenSSL, zlib) are already
resolved via `find_package()` inside `libs/envelope/CMakeLists.txt`.
