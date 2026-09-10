# Changelog

All notable changes to this project, by date, newest first. There are no version tags
yet, so entries are grouped by day. Written from the actual git history
(`git log`) — each entry reflects what its commit's message says, not a
reconstruction after the fact.

## 2026-09-10

- **Add `--log-dir`/`--data-dir` to `ms_server`; fix RSA keys loading before argv is
  parsed** (`545e5c0`). RSA/TLS material and every log file defaulted to relative paths in
  the current working directory (a gap flagged in `ai_review.md`), so where you `cd`
  before invoking `ms_server` silently determined where key material and logs landed.
  Added `--log-dir`/`--data-dir`, both defaulting next to the binary
  (`get_exe_directory()/"logs"` and `.../"data"`, same pattern `--plugins-dir` already
  used) and auto-created if missing — unlike `--plugins-dir`, these are write targets this
  process owns, not a pre-existing input directory. `restbed.log`/`plugins.log`/
  `requests.log` now go under `--log-dir`; the RSA key pair and SSL cert/DH params default
  under `--data-dir`, though an explicit `--rsa-priv-key`/`--rsa-pub-key`/`--ssl-cert`/
  `--ssl-dh` still wins outright, independent of `--data-dir`. Fixing this surfaced a real
  bug: `http_api_t`'s constructor eagerly loaded the RSA key pair from its own hardcoded
  default path *before argv was even parsed* — no `--rsa-priv-key`/`--data-dir` value
  could ever actually affect which key material got loaded, only what the path *getters*
  reported afterward. Split into `http_api_i::load_rsa_keys()` (appended at the interface's
  end, no ABI bump needed), called once both paths are finalized post-parse. That reorder
  surfaced a second bug: moving the plugin logger's setup to after CLI parsing meant
  `--help` (or any parse error, which returns from `main()` during `CLI11_PARSE` before
  that setup runs) triggered `shutdown_guard_t`'s destructor calling `unload_plugins()`,
  which dereferenced the not-yet-initialized plugin logger unconditionally — `ms_server
  --help` segfaulted. Fixed with a null check (verified under `gdb` before and after).
- **Add a fixed, reproducible board #11; log all requests/responses; add an
  automated live-replay regression test** (`bd1db71`). `rand()`'s actual output
  isn't standardized across platforms/compilers/libc, so a fixed seed gives no
  cross-machine reproducibility guarantee — added
  `plugin_api_i::create_fixed_board(width, height, mines)` (appended at the very
  end of the interface, no ABI version bump needed) backed by a new
  `board_t(width, height, std::span<const coord_t>)` constructor that places
  mines at explicit coordinates instead of via `rand()`. `server.cxx` creates
  board #11 from it at startup alongside the usual 10 random boards; its mine
  layout was transcribed from a real client session screenshot and verified —
  by hand against all 90 checkable neighbor-count digits before being
  committed, then independently again via a live session against the running
  server. Also added a fourth dedicated `spdlog` logger (`requests` →
  `requests.log`, matching the existing `restbed`/`plugins` logger pattern),
  wired into `response_t::send()` — the single funnel every response already
  goes through — logging every request/response pair regardless of which of
  `dispatch()`'s paths produced it. On top of that,
  `src/server/tests/live/replay_requests_log.py` replays a captured
  `requests.log` against a live server (spawning `ms_server` itself, or
  targeting one already running) and compares status codes and
  structurally-parsed JSON bodies against what was logged; since every action
  handler already reports its outcome through a top-level `"status"` field
  (`"ok"`/`"boom"`/`"win"`/`"lose"`) and response bodies are expected to keep
  gaining fields over time, only an HTTP status or `"status"`-field mismatch is
  a hard failure by default — any other body difference is a warning,
  promotable via `--warnings-as-error`. Wired into CMake as a `live_replay`
  CTest target with a committed fixture covering a losing and a fully-winning
  session against board #11. Added `tests.md` documenting how to build and run
  both `ms_test` and `live_replay`.
- **Self-describing, checksummed `parameter_bytestream_t` wire header** (`8bfb43e`).
  `store()`/`load()`'s header (previously just a version byte) now also carries a
  fixed-width payload length and a CRC32 checksum, closing two gaps flagged as
  `@todo` comments in the header itself. `check_capacity()` for reads only ever
  validated against `buffer_size_` (the physical buffer), not against how much of
  it was actual payload — handing `load()` a buffer bigger than what was
  `store()`d (its full capacity, or stale bytes from an earlier use) could
  silently read past the real payload as if it were valid; `load()` now narrows
  `buffer_size_` to the length it just read before `deserialize()` runs, and
  separately verifies `deserialize()` consumed exactly that many bytes. The CRC32
  catches corruption that still happens to parse as syntactically valid (wrong
  but plausible) data, which the tag/length grammar alone can't. Both fields are
  fixed-width, outside the recursive `numberX`/`stringN` grammar, same reasoning
  as `wire_version` (bumped to 2 for this header shape change). Also tightened
  `write()`'s capacity checks for strings/integers — they used to always reserve
  the 8-byte worst case even though the point of `numberN`/`stringN` is that most
  values need far less — via a new `bytes_needed()` helper, which also let
  `write<integral T>`'s duplicate magnitude computation be removed. Left the
  other two ideas from the same `@todo` (zip/unzip, a stronger sha256 signature)
  alone: both would mean a permanent compression/crypto dependency baked into
  every plugin `.so` via this header, for a same-process boundary with neither an
  adversary nor a bandwidth problem the CRC32 doesn't already cover — if ever
  added, they belong as wrappers around `store()`/`load()`'s raw bytes in a
  future standalone (non-header-only) version of this type, not inside it. Added
  `RejectsCorruptedPayload` and `IgnoresGarbageBeyondTheStoredPayload` tests.
- **Route all handler params/results through `parameter_bytestream_t` across the
  plugin ABI** (`fc49b1c`). `plugin_api_i::simple_handler_t`/`board_handler_t`
  crossed the `dlopen` boundary carrying a `parameter_map_t` by value (and
  returning one inside `handler_result_t`) — a `std::unordered_map<std::string,
  std::variant<...>>` whose allocator/heap behavior had to agree between host and
  plugin, not just its layout. Both are now byte-only: `size_t(*)(..., const
  std::byte* params_buf, size_t params_len, std::byte* out_buf, size_t out_cap)`,
  returning bytes written (`0` = failure — a valid `store()` is always ≥ 1 byte, an
  unambiguous sentinel). No `parameter_t`/`parameter_map_t`/`std::string` crosses
  that function-pointer call as a C++ object anymore. Every existing handler body
  is unchanged (`handlers.cxx`, `cell_check.cxx`, `boards_list.cxx` still write an
  ordinary `parameter_map_t`-in/`handler_result_t`-out function); added
  `handler_wire::to_wire`/`from_wire` (`api.hxx`) converting `handler_result_t`
  to/from one `parameter_t` envelope (`{"ok":true,"body":...}` or
  `{"ok":false,"http_code":...,"message":...}`), and
  `simple_handler_adapter<Handler>`/`board_handler_adapter<Handler>` templates
  that `load()` params, call the plugin author's function unchanged, catch any
  exception locally so nothing unwinds across the `.so` boundary, and `store()`
  the envelope into a host-provided output buffer — `Handler` as a non-type
  template parameter means each instantiation is an ordinary capture-free
  function, a valid `simple_handler_t`/`board_handler_t` value. Only the
  registration call sites changed, e.g. `cell_check_handler` →
  `board_handler_adapter<cell_check_handler>`. `dispatch()` `store()`s parsed
  query params once via a growing-buffer retry helper (safe — pure
  serialization, no side effects) and calls the handler with a fixed 1 MiB output
  buffer, deliberately *not* retried on failure: a `board_handler_t` like
  `cell_check_handler` has already mutated the board by the time it tries to
  serialize its result, so re-invoking it would double those side effects instead
  of just getting more room. Added `parameter_bytestream_t::size()` to support
  this. Verified against a running server across every endpoint, including both
  plugin-hosted handlers.
- **Harden `parameter_bytestream_t`: bounds-checked, versioned `store()`/`load()`
  returning `result_t`** (`9715093`). `store()`/`load()` are now the only public
  surface — `[[nodiscard]]`, returning `result_t<void>`/`result_t<parameter_t>`
  (`std::expected<T, std::string>`, whose alias moved earlier in `api.hxx` so this
  class can use it) instead of ever letting an exception reach the caller;
  `serialize()`/`deserialize()` moved to `private` and stay throwing internally, but
  every failure (buffer overrun, malformed tag, version mismatch, reusing one
  instance for a second `store()`/`load()`) is normalized to one string at that
  boundary. Every `write_*`/`read_*` primitive now funnels through a
  `check_capacity()` that throws on overrun, replacing `write_*`'s old `0`/`false`
  return that no caller ever checked, and adding a bound check `read_string()`
  (among others) previously lacked entirely — a truncated/corrupted buffer could
  read past `buffer_size_`. An unrecognized `type_tag` byte now throws instead of
  `deserialize()` silently returning a null `parameter_t`. Also fixed real UB from
  the previous commit: negating `INT64_MIN` while still a signed `int64_t` (write
  side, and the `neg_number8` read case) is signed-overflow UB; both now cast to
  `uint64_t` first and negate in the unsigned, modular domain. Added a one-byte
  `wire_version`, checked once per buffer by `store()`/`load()` outside the
  recursive tag grammar. Return-value cleanup: `write_tag`/`write_len`/`put_byte`
  and all `write(...)` overloads are now `void`; removed two dead helpers,
  `write_value<T>()` and templated `write_len<T>()`. Tests updated for the new API
  (shared `roundtrip()` helper), `INT64_MAX`/`INT64_MIN` added to the integer test
  list, and two new tests added: `RejectsReusedInstance`,
  `RejectsWireVersionMismatch`.
- Make the bitstream more compact: variable-width int/length encoding instead of a
  fixed 8 bytes (`1785a5c`). String length and number values were always written as
  a full 8-byte `int64_t`/`size_t`, mostly zero bytes for the common case of small
  numbers. Replaced the single `number`/`string` tag with eight width-specific tags
  each (`number1`..`number8`, `string1`..`string8`, plus `neg_number1`..`neg_number8`
  for negatives written as sign + magnitude), so a value only costs as many bytes as
  its magnitude actually needs — 21 more `type_tag` values in exchange for a
  meaningfully smaller payload on the common case.
- Add googletest support; add `parameter_bytestream_t` with basic tests (`9cd0c0c`).
  New `BUILD_TESTS` CMake option wires up GoogleTest (`ms_test` target,
  `gtest_discover_tests()`) and a first `parameters_bitstream.unittest.cxx`.
  Introduces `parameter_bytestream_t` in `api.hxx`: `serialize()`/`deserialize()`
  pack/unpack a `parameter_t` into a raw byte buffer (`std::byte*`/
  `std::span<std::byte>`), tagging each value with a `type_tag` so `deserialize()`
  can recover which alternative was written. Only `int64_t` is actually stored for
  numbers at this point — every other integral type is meant to be cast in/out by
  the caller.

## 2026-09-09

- Fix `server_plugins.md`: RSA/SSL paths live on `http_api_i`, not `plugin_api_i`
  (`cc5a658`). Its "RSA/SSL paths" subsection still listed
  `rsa_priv_key_path`/`ssl_cert_path`/etc. as direct `plugin_api_i` methods after
  `becbca4` moved them onto `http_api_i`; now shown reached via `http_api()`, with a
  note that `http_api_i` also carries the `set_*` counterparts and
  `authorize_client()`.
- Update `server_plugins.md` for the two-handler-shape plugin API (`9a8ef5d`). Still
  documented the pre-`72091bb` API end to end — `addon_api_i` (renamed `plugin_api_i`
  in `b074072`), a single `rest_handler_t(restbed::Session&)` handler signature,
  plugins calling `create_response`/`authorize_client` themselves. Rewrote "Anatomy of
  a plugin" to add a "The two handler shapes" section (`simple_handler_t` vs.
  `board_handler_t`, `handler_result_t`, `param_spec_t`), replaced the
  `create_response`/`authorize_client` example with a `board_handler_t` one matching
  `cell_check.cxx`/`boards_list.cxx` as they now look, and updated every
  `addon_api_i`/`addon_api_t` mention to `plugin_api_i`/`plugin_api_t` (left the
  `ADDON_*` macro/function names alone — those weren't renamed in code).
- **Replace per-handler auth/param boilerplate with two typed handler shapes**
  (`72091bb`). Every REST handler but `session_new_handler` repeated the same
  ~10-line block: pull query params by hand, build a response, call
  `authorize_client(session)`, resolve `board_for_client(id)`, and only then run its
  actual logic — a handler that forgot the auth call would simply serve
  unauthenticated requests, since the check lived by convention, not by construction.
  Replaced the single `rest_handler_t`/`resource_t` shape with two distinct
  function-pointer types: `simple_handler_t` (no auth, no board — `session/new` picks
  its own board by id/random since there's no client yet to authenticate;
  `boards/list` needs no board at all) and `board_handler_t` (can only be registered
  through the overload that makes the host authenticate the caller and resolve
  *their* board before ever calling the handler — no path hands a `board_i&` to a
  handler without going through auth first, and no separate bool to set wrong).
  `handler_result_t` is `std::expected<parameter_map_t, handler_error_t>`; `resource_t`
  also carries a `param_spec_t` list so the new `plugin_api_t::dispatch()` parses a
  resource's declared params generically — and now reports "missing mandatory
  parameter x" separately from "invalid parameter x" instead of conflating both into
  one sentinel-based check the way `cell_reveal_handler` used to. `dispatch()` also
  runs the handler inside a `try`/`catch`, turning an uncaught exception into a 500.
  Net effect: `handlers.cxx`, `cell_check.cxx` and `boards_list.cxx` lost all their
  auth/param/response boilerplate and no longer touch `restbed::Session` at all;
  `create_response` was removed from `plugin_api_i` since nothing needs it through
  that interface once `dispatch()` owns response construction. Verified end-to-end
  against a running server (auth failures, `session/new`'s random/explicit/invalid/
  out-of-range `board_id`, missing-vs-invalid params, reveal/flag/check).
- Ignore `plugins.log` (`141489d`). The dedicated plugin logger added in `e215db5`
  writes `plugins.log` at the server's cwd; `.gitignore` never picked up the new
  runtime file.
- Move rsa/cert related code to `http_api_i` instead of `plugin_api_i` — plugins
  actually don't need it (`becbca4`). `rsa_priv_key_path`/`rsa_pub_key_path`/
  `ssl_cert_path`/`ssl_dh_path` (plus their `set_*` counterparts) and
  `rsa_private_key`/`rsa_public_key` moved off `plugin_api_i` onto `http_api_i`,
  reachable via `http_api()`; nothing plugin-facing used them directly, they exist
  only for JWT signing/HTTPS setup, which was already `http_api_i`'s job.
- Rename `addon_api_*` to `plugin_api_*` (`b074072`) — `addon_api_i`/`addon_api_t`
  become `plugin_api_i`/`plugin_api_t` throughout, matching what the interface
  actually is (the surface plugins get handed), not what it originally started out
  as. The `ADDON_API_ABI_VERSION`/`ADDON_PLUGIN_ABI_TAG`/`addon_api_abi_tag` ABI-check
  names were deliberately left alone.
- **Guard `boards_` with a mutex, add a dedicated plugin logger and RAII library
  handle** (`e215db5`). Closes the concurrency TODO from `992be62`: added
  `boards_access_` (a `std::mutex`) to `addon_api_t` and took it in
  `boards_count()`, `add_board()`, `for_each_board()`, and `board()`. Caught a
  self-deadlock while wiring this up — `add_board()` took the lock and then called
  `boards_count()`, which takes the same non-recursive mutex again; since `main()`
  calls `add_board()` ten times at startup, the server would have hung before ever
  serving a request. Fixed by reading `boards_.size()` directly, since the lock is
  already held. Also gave the plugin subsystem its own `spdlog` logger (console +
  `plugins.log` file sink) instead of routing through the app-wide `SPDLOG_*`
  macros, moved the plugin machinery into a named `plugin` namespace, and wrapped
  `dlopen`/`dlsym`/`dlclose` in an RAII `library_handle_t` so `plugin_handle_t`'s
  constructor can bail out on ABI mismatch or missing exports without a manual
  `close()` call. Checked off the completed items in `todo.md` (`session/new`
  rename, `action/confirm` handler, and the `boards_` mutex) to match actual state.

## 2026-09-08

- **Add plugin ABI-tag check: refuse to load a plugin built against a mismatched
  toolchain/header** (`992be62`). `addon_api_i`'s virtual-interface ABI only has a
  well-defined layout between binaries built with the same compiler, standard
  library, and version of `api.hxx` — there was no way to detect a mismatch before
  now; it would just corrupt memory somewhere unrelated to the actual defect. Added
  `ADDON_API_ABI_VERSION` + `addon_api_abi_tag()` (folding in compiler id,
  `__cplusplus`, and `_GLIBCXX_USE_CXX11_ABI`) to `api.hxx`, and an
  `ADDON_PLUGIN_ABI_TAG()` macro plugins invoke once to export it as `abi_tag()`;
  `plugin_handle_t` now `dlsym`s and compares this before resolving `init_plugin`,
  refusing (and logging both tags) on any mismatch or missing export. Caught a real
  bug in this before shipping: `addon_api_abi_tag()` must be `static`, not `inline` —
  an `inline` (weak, default-visibility) definition in a header included by both the
  server and every plugin gets resolved through the process's global symbol scope,
  where the executable's own copy always wins regardless of `RTLD_LOCAL` on the
  plugin, silently interposing the host's tag into every plugin's call to it, so the
  check compared the host against itself no matter what the plugin was built with —
  found via a deliberate two-plugin version-mismatch test before shipping it. Also
  documents the requirement in `server_plugins.md`'s new "ABI compatibility" section,
  and adds two follow-up TODOs: a future protobuf-based data boundary to actually
  decouple plugin/server toolchain versions (this tag only fails loudly on a
  mismatch, it doesn't remove the constraint), and the unguarded `boards_` map as a
  latent concurrency issue.
- **Fix shutdown-order regression: destroy `api` after `unload_plugins()`, not before**
  (`b76dbad`). The RAII `shutdown_guard_t` added earlier the same day had `api.reset()`
  running before `unload_plugins()`, but a plugin's `unload_plugin()` dereferences its
  own `addon_api_i*` (to disconnect from `api`'s signal) before dropping it — a
  use-after-free. Reordered so `api` is destroyed only after every plugin has
  disconnected and released its pointer. Also made `cell_check`'s `unload_plugin()`
  disconnect from the signal like `boards_list`'s already did.
- **Remove `shared_ptr`/`std::function` across the plugin ABI boundary, fix shutdown
  crash and hang** (`5322eaf`). Plugin callbacks and `addon_api_i`/`board_i` handles
  were `std::function`/`shared_ptr` objects whose type-erasure or control-block code
  was compiled into the plugin's `.so`; destroying or invoking one after that `.so`
  was `dlclose`'d jumped into unmapped memory — the real cause of a crash that looked
  like it was "in `http_api_t`'s destructor". Switched the boundary to plain
  references and C-style function-pointer + `void*` `user_data` callbacks. Re-enabled
  `boards/list` (previously disabled for the same cross-boundary reason) using the new
  callback + `user_data` pattern. Moved the SIGINT/SIGTERM `service->stop()` call onto
  a dedicated thread instead of running it inline in the asio signal handler, since
  `Service::stop()` resets and re-runs its own `io_context` to drain it, and that
  handler runs nested inside that same `io_context`'s own worker-thread `run()` call —
  asio forbids `reset()`/`run()` while another `run()` for the same `io_context` is
  active on the stack, which was hanging shutdown indefinitely. (Paired with two fixes
  in the separate restbed repo this project depends on: closing listening acceptors in
  `Service::stop()` so its drain step can't block forever waiting on a connection that
  will never arrive, and making stop()'s once-only guard a per-instance member instead
  of a function-local `static bool` shared across every `Service` instance in the
  process.)
- **Add senior-level server code review, fix the correctness bugs it found**
  (`599eb07`). `ai_review.md`: architecture/correctness review of `src/server`
  covering concurrency gaps, error-handling inconsistencies, dead code, and verified
  bugs. Fixes applied (mine-generation reseeding intentionally left alone — the future
  generator app owns real board generation):
  - `get_id_from_jwt`: `jwt::decode()` was outside the `try`/`catch`, so a malformed
    (not just expired/invalid) bearer token threw all the way out to a bare 500 with a
    plain-text body instead of the documented `{"error": ...}` JSON shape.
  - `add_new_client`: an unknown `board_id` threw uncaught `std::out_of_range` from the
    map lookup, crashing instead of failing cleanly.
  - `session_new_handler`: validate the explicit `board_id` query param's range, not
    just its parse; dropped a leftover self-verification block that re-decoded every
    freshly minted token for no functional reason.
  - Board id numbering made 1-based end-to-end — `boards/list` and `session/new`
    previously disagreed on 0-based vs 1-based, silently binding sessions to the wrong
    board, and the random pick could compute board id 0 and crash session creation
    ~1 in 10 times.
  - `rb_log::log`: unbounded `vsprintf` into a fixed 1024-byte buffer, changed to
    bounded `vsnprintf`.
  - `rb_log::log_if`: was passing a `va_list` as a vararg to `log()` (UB, formatted
    nothing correctly); given its own `vsnprintf` path.
  - `load_plugins`: `directory_iterator` throws if `--plugins-dir` doesn't exist,
    uncaught — crashed on startup instead of a clean "no plugins" outcome.

## 2026-09-07

- Update `server_api.md` and add `server_plugins.md` (`5ad6ae7`). `server_api.md` was
  describing a long-gone id-query-param auth model and missing several handlers;
  rewritten to describe the current REST API (JWT bearer-token auth, correct status
  codes, native-bool field types, the `boards/list`/`cell/check` plugin endpoints, and
  the actual boom semantics of `cell/reveal` vs `cell/check`). `server_plugins.md`
  added from scratch, documenting the plugin entry points, the
  `ready_to_load_resources_signal()` registration pattern, and the full
  `addon_api_i`/`board_i`/`cell_i`/`coord_t` surface.
- Virtualize `authorize_client` and `reveal_cells` so plugins don't touch main-app
  symbols (`f7d0d5b`). Added `http_api_i::authorize_client`, exposed via
  `addon_api_i::http_api()`; built-in and plugin handlers now go through it instead of
  reaching into `handlers::` directly. Moved `reveal_cells` from a free function into
  `board_i::reveal_cells()`. Extracted all JWT/auth logic out of `handlers.cxx` into
  `http_auth.hxx`/`.cxx` under `http::auth`.
- Rename `coord` to `m_coord_t`, add `http_api_i` interface stub, misc fixes
  (`ee43489`). Reworked the multidimensional coord type into `m_coord_t` with an
  encapsulated `vec`, spaceship/equality operators, and `to_string()`. Added the
  `http_api_i` interface as a stub. Increased the client HTTP timeout from 5s to 30s.
  Fixed a structured-binding bug in the `reveal_cells` visited-set check.
- Struct for coord (`e374f6f`).
- Make api fully virtual, store number of bombs in cell, add classes for cell and for
  coord (`279a3f8`, `5e54e17` — committed twice with the same message).

## 2026-09-05

- Get rid of two "field" terms — game area/storage is now "board", and records in
  yaml/json/xml are "properties" (`b131fe3`).
- Two plugins: `cell-check` handler, both server and client (`fd87d90`).

## 2026-09-03

- Add plugin support (`f946644`) — the `dlopen`-based plugin system this changelog's
  09-08 entries later hardened.
- Add HTTPS support to server and client (`72c6ecd`, `ced7e37` — committed twice with
  the same message).

## 2026-09-02

- Change HTTP codes, generalize some functions (`22e1fa0`).
- Redo plain `client_id` to JWT to improve security (`977dcb3`).

## 2026-09-01

- Flag / bomb symbols in the TUI client (`f92711b`).

## 2026-08-30

- Fix `fully_revealed` — was a bug with 1 unrevealed/unflagged mine left; add restbed
  logging (`54abac6`).

## 2026-08-29

- Add TODOs for client and server (`34dfb44`).
- Check game board for lose/win condition, add client support (`962aa6a`).
- Simplify `/action/reveal` to always return a `cells` array; bold numbers/flags in the
  UI (`2901d37`). The server previously special-cased a single-cell reveal as a flat
  x/y/count shape instead of the uniform array, which broke multi-cell flood-fill
  handling on the client.
- Add map (`parameter_map_t`) support to recursive parameter serialization
  (`30fedab`) — `parameter_t` can now nest maps and arrays of itself arbitrarily,
  serialized recursively across yaml/json/xml.
- Widen client board cells to compensate for non-square terminal fonts (`5689f82`).
- Add XML response format, fix client for variant-based fields, use `std::visit` for
  serialization, fix curses wide-char double-line box and flag contrast (`fee626a`).
  TUI cell rendering reworked to an independent 3x3 bordered box per cell, with a real
  double-line ncursesw frame for the selected cell.
- Server + TUI client minimal proof of concept (`97852c3`, `98d7261` — committed twice
  with the same message).

## 2026-07-24

- `/new` and `/size` endpoints (`2400e5e`).
- Initial commit (`e344c84`).
