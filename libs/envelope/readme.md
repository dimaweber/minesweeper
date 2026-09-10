# envelope

A small C++ library that wraps an arbitrary byte buffer through a fixed pipeline of
optional protective layers — compress, encrypt, sign, base64 — and unwraps it again,
self-describing which layers were actually used so a receiver never has to separately
know or repeat a sender's configuration.

```cpp
#include <envelope/envelope.hxx>

envelope::config_t cfg;
cfg.compress    = true;
cfg.encrypt     = true;
cfg.encrypt_key = my_32_byte_key;   // AES-256
cfg.sign        = true;
cfg.sign_key    = my_sign_key;      // HMAC-SHA256, any length
cfg.base64      = true;             // text-safe output, e.g. for a JSON field

const envelope::envelope_t env(cfg);

const auto wrapped = env.wrap(some_bytes);
if ( !wrapped ) { /* wrapped.error() is a std::string */ }

// ... send *wrapped over the wire ...

const auto unwrapped = env.unwrap(received_bytes);
if ( !unwrapped ) { /* rejected: bad magic, failed signature, wrong key, ... */ }
// *unwrapped == some_bytes
```

`envelope_t` is stateless and reusable — construct one per configuration, call
`wrap()`/`unwrap()` on it as many times as you like.

## Why one class, not four independent wrappers

Compress, encrypt, sign, and base64 don't compose safely in just any order, so this
library doesn't expose them as separately-stackable pieces a caller could apply in the
wrong sequence:

- **Compress before encrypt, always.** Encrypted bytes are high-entropy and don't
  compress; compressing after encrypting buys nothing.
- **Sign after encrypt, over the ciphertext (and the header) — not the plaintext.**
  This is "encrypt-then-MAC," not "MAC-then-encrypt": `unwrap()` verifies the signature
  as the very first thing it does with the bytes, before decrypt or decompress ever run.
  A tampered or forged blob is rejected outright instead of being fed into the
  decryption routine (the class of bug behind padding-oracle attacks) or the
  decompressor (a decompression-bomb vector on unauthenticated input).
- **The signature covers the header's magic + mask too, not just the payload.**
  Otherwise an attacker could flip a mask bit — e.g. turn the "signed" bit off — to
  talk a receiver into skipping verification entirely: the same class of downgrade
  attack as JWT's infamous `"alg": "none"`.

Given that, exposing four independent public wrappers would just hand every call site
the ordering (and downgrade-safety) knowledge, with no enforcement — one gate that
applies whichever layers are configured, in the one order that's actually safe, removes
the possibility of a caller getting it backwards. Each layer's implementation still
lives in its own file (`src/base64.cxx`, `src/compress.cxx`, `src/crypto.cxx`) — only
the public surface is one class.

## Wire format

```
["ENV1"][mask][payload][signature]
  4B      1B    ...       32B, present iff mask's sign bit is set
```

then base64-encoded as a whole, if `config_t::base64` is set. `payload`'s own shape
depends on the mask:

```
compressed?( encrypted?( raw bytes ) )
```

`mask` is a bitset (`compress = 1`, `encrypt = 2`, `sign = 4`) recording which layers
`wrap()` actually applied — `unwrap()` reads it to know what to reverse, rather than
requiring the caller to reconfigure the same toggles used at encode time. Only the
*keys* for whichever layers the mask names need to be supplied out of band; the choice
of layers travels with the data.

## Algorithms

- **compress** — zlib deflate (`compress2`/`uncompress`), prefixed with an 8-byte
  little-endian original-size field so decompression can size its output buffer
  without a streaming API. Capped at 256 MiB claimed size to avoid a
  decompression-bomb-via-size-lie on malformed input.
- **encrypt** — AES-256-GCM (AEAD: confidentiality and authenticity together, via a
  fresh random 96-bit nonce per call). `encrypt_key` must be exactly 32 bytes. A nonce
  is never reused with the same key.
- **sign** — HMAC-SHA256, a shared-secret MAC, not an asymmetric signature — no
  public-key trust setup, and it works as a standalone integrity layer for a
  "signed but not encrypted" mode that GCM's own authentication can't offer on its own
  (AEAD only authenticates when it's also encrypting). Tag comparison uses
  `CRYPTO_memcmp` (constant-time), not `==`/`memcmp`.

## What this doesn't do

- **Key management.** How `encrypt_key`/`sign_key` are generated, distributed, rotated,
  or stored is entirely the caller's problem. This library only consumes them.
- **Replay protection.** A verified, decrypted blob is exactly what was wrapped — but
  nothing stops the same wrapped blob from being replayed later unless the caller adds
  its own nonce/timestamp/sequence-number *inside* the payload and checks it after
  `unwrap()`.
- **Anything about the transport.** No framing beyond the header above, no
  connection/session concept — `wrap()` produces bytes, `unwrap()` consumes them; what
  carries them (HTTP, a socket, a file) is outside this library's job.

## Dependencies

OpenSSL (`libcrypto`, for HMAC/AES-GCM/`CRYPTO_memcmp`/`RAND_bytes`) and zlib. Both
third-party — nothing here depends on anything else in whichever repository it's
currently vendored inside (see the note at the top of
`include/envelope/envelope.hxx`), so this directory can be lifted into its own project
unmodified.

## Building

Currently built as part of the parent project via `add_subdirectory(libs/envelope)`,
producing a static library target named `envelope`. See [`tests.md`](tests.md) for how
its own test suite builds and runs, including what's still missing for a fully
standalone (outside the parent project) build.
