# Server Code Review

Architectural review of everything under `src/server/` and `src/common/` (the pieces
`ms_server` links against), from a senior-C++/architecture standpoint. Scope is design
and correctness, not style nitpicks — `.clang-format`/`.clang-tidy` already exist for
that (though note: `CMakeLists.txt:38` has the `clang-tidy` integration commented out,
so nothing currently enforces those checks at build time).

Findings are grouped by how urgently they're worth acting on. Every item names the file
and gives a concrete failure scenario, not just a code-smell label.

---

## 1. Should fix — real bugs

### 1.1 Every server run generates the exact same minefields
`board_t::board_t` (`api.cxx:197-206`) places mines with `rand() % width` / `rand() %
height`, and `session_new_handler`'s random-board pick (`handlers.cxx:23`) does
`rand() % api->boards_count()`. Nothing in the codebase ever calls `srand()`. `rand()`'s
default seed is fixed, so **every fresh `ms_server` process lays out bit-for-bit
identical mines on all 10 boards, every time**, and hands out the same "random" board
pick sequence to new sessions. For a minesweeper server this isn't a cosmetic detail —
it undermines the entire premise that boards are unpredictable. Fix: seed a
`std::mt19937`/`std::mt19937_64` once at startup (e.g. `std::random_device`) and use
`std::uniform_int_distribution` instead of `rand() % n` (which is also modulo-biased,
though at these ranges that's the lesser issue).

### 1.2 Malformed (not just expired) JWT breaks the API's own error contract
`http::auth::get_id_from_jwt` (`http_auth.cxx:36-38`):
```cpp
auto verify  = jwt::verify( )...;
auto decoded = jwt::decode(token);   // <-- outside the try block below
try {
  verify.verify(decoded);
} catch (...) { ... }
```
`jwt::decode` throws for a token that isn't even shaped like a JWT (not just one that
fails signature/expiry checks, which *is* caught). That exception is never caught
anywhere up the call chain. I verified this live: sending
`Authorization: Bearer garbage` returns a bare `500` with the plain-text body
`invalid token supplied` — not the documented `{"error": "..."}` JSON shape every other
error path produces (see `server_api.md`'s Errors section, which now documents this as
an explicit exception to the rule — but it shouldn't need to be one). Fix: move
`jwt::decode(token)` inside the existing `try`.

### 1.3 Unknown `board_id` crashes instead of erroring cleanly
`addon_api_t::add_new_client` (`api.cxx:384-393`) calls `board(board_id)`, which is
`boards_.at(board_id)` (`api_impl.hxx:121-123`) — throws `std::out_of_range` for an
unknown id. `session_new_handler` (`handlers.cxx`) never catches it. A `POST
/session/new?board_id=9999` with a syntactically valid but nonexistent id will throw all
the way out of the handler, hitting whatever restbed's default top-level exception
handling does (same uncaught-exception path as 1.2) instead of a clean `400`/`404`.
Contrast with `cell_reveal_handler`, which *does* wrap board access in
`catch (std::out_of_range&)` — the two handlers disagree on discipline for the same kind
of failure.

### 1.4 The "no `board_id` given" path can itself hit 1.3
Board ids are 1-based (`add_board` starts the counter at `boards_count() + 1`,
`api_impl.hxx:102-106`), but the auto-pick path (`handlers.cxx:23`) computes
`rand() % api->boards_count()`, which ranges over `[0, boards_count()-1]` — **including
0**, which is never a valid board id. Some fraction of `POST /session/new` calls with no
`board_id` at all will hit exactly the crash described in 1.3. This is latent today only
because it depends on the (also broken, see 1.1) unseeded RNG's actual sequence — fixing
1.1 without fixing this off-by-one will make it reproducibly crash on whichever draw
happens to be near-zero.

### 1.5 Unbounded `vsprintf` into a fixed buffer in the restbed logger
`rb_log::log` (`server.cxx:63-71`):
```cpp
void log (const Level level, const char* format, ...) override {
  va_list args;
  va_start(args, format);
  char message[1024];
  vsprintf(message, format, args);   // <-- no bound
  ...
}
```
This is a stack buffer overflow (CWE-121) waiting for whatever restbed happens to log
internally to exceed 1024 bytes — it's not attacker-controlled today (restbed's own log
strings, not request data), but it's still a real bug with a trivial fix:
`vsnprintf(message, sizeof(message), format, args)`.

### 1.6 `rb_log::log_if` forwards a `va_list` as a vararg — undefined behavior
`server.cxx:73-81`:
```cpp
void log_if (bool expr, const Level level, const char* format, ...) override {
  if ( !expr ) return;
  va_list args;
  va_start(args, format);
  log(level, format, args);   // <-- passing a va_list where `...` expects args, not a va_list
  va_end(args);
}
```
`log(level, format, args)` calls the `(Level, const char*, ...)` overload with `args` (a
`va_list`) as the *first* vararg — it does not unpack it. Whatever restbed passes through
`log_if` will not be formatted correctly. This needs a `vsnprintf`-based implementation
mirroring the fix in 1.5, not a call to the varargs overload.

### 1.7 Plugin loading isn't guarded against a missing plugins directory
`main()` (`server.cxx:203-216`) always calls `load_plugins(plugins_dir, api)` when
`plugins_dir` is non-empty — and it's *never* empty, because the CLI option's default is
`get_exe_directory() / "plugins"` (`server.cxx:165`), not an empty path. `load_plugins`
(`server.cxx:96-122`) does `std::filesystem::directory_iterator(plugins_dir)` with no
try/catch, and neither does `main`. The throwing (non-`error_code`) overload of
`directory_iterator`'s constructor throws `std::filesystem::filesystem_error` if the
directory doesn't exist — uncaught, `std::terminate()`. This works fine in the CMake
dev-build layout (the plugins subdirectory always exists next to the binary), but ship
the `ms_server` binary anywhere else without its `plugins/` sibling and it crashes on
startup instead of just logging "no plugins found" and continuing.

---

## 2. Concurrency — the board/board-map story is inconsistently protected

`clients_t` (`api_impl.hxx:14-23`) wraps its map in a `std::mutex` and locks on every
operation — good instinct. But:

* **`boards_` (the `board_id_t -> shared_ptr<board_i>` map in `addon_api_t`,
  `api_impl.hxx:159`) has no locking at all.** `add_board`, `board(board_id_t)`, and
  `for_each_board` all touch it unsynchronized. It's not exploitable *today* because all
  10 boards are created single-threaded before `service.start()` is called and nothing
  calls `add_board` afterward — but the interface offers `add_board` as a general,
  always-available `addon_api_i` method to any plugin, at any time, with no documented
  "startup only" contract. A plugin author who calls it from a request handler (a
  perfectly reasonable thing to want — "create a new board on demand") introduces a data
  race the moment two requests hit it concurrently, since restbed runs a 4-worker pool
  (`settings->set_worker_limit(4)`, `server.cxx:237`).

* **A single client's `board_t` has zero internal synchronization once handed out.**
  `board_for_client` returns a bare `shared_ptr<board_i>`; obtaining the pointer is
  thread-safe (`clients_t`'s mutex), but nothing stops two concurrent requests bearing
  the *same* client's JWT (a browser firing overlapping `cell/reveal`/`cell/flag` calls
  is not an exotic scenario) from mutating the same `board_t`'s `data_` vector and
  bit-field cells from two worker threads simultaneously. This is a genuine data race,
  not just a theoretical one, given the server is already configured for 4 concurrent
  workers. Either give each client's board its own lock (a `std::mutex` member alongside
  the board, taken by the handler layer, not by `board_i` itself — keep `board_i` simple)
  or make it explicit/documented that a client must serialize its own requests.

* **`clients_t::begin()`/`end()` (`api_impl.hxx:17-18`, `api.cxx:230-238`) each lock and
  unlock the mutex independently.** They're currently only used internally by `find`
  (via `it != clients_.end()`, which re-locks correctly), never as an iteration pair
  anywhere in the codebase — so no bug exists *today*. But the pair is public API on the
  class; anyone who writes `for (auto it = clients_.begin(); it != clients_.end(); ++it)`
  gets a plausible-looking loop that's actually racy (the lock is released between
  `begin()` and the first comparison, and `++it` isn't synchronized at all). Either
  remove `begin`/`end` (nothing needs them) or replace them with a locked
  `for_each`-style method matching the safe pattern `addon_api_t::for_each_board` already
  uses for the (unfortunately unprotected) board map.

* **`add_board`'s id-assignment is a TOCTOU race**: `board_id_t add_board(...)` computes
  `boards_count() + 1` and then `emplace`s — two concurrent callers can compute the same
  id, and the second `emplace` silently no-ops (loses that board) rather than erroring.
  Same "only safe because it's single-threaded today" caveat as above.

---

## 3. Error-handling strategy is three different idioms glued together

The codebase uses, for conceptually similar "this can fail" situations:
- **`std::expected`** (`result_t<T>`) for JWT/auth (`http_auth.hxx`, `http_api_i`).
- **Exceptions** for invalid coordinates (`board_t::cell` throws `std::out_of_range`).
- **Magic sentinel values** for board queries (`neighbor_bombs_count` returns `-1` for
  both "invalid coordinate" *and* "this cell itself is a mine" — two unrelated failure
  modes sharing one sentinel, `api.hxx`/`api.cxx`).

That last one is more than a style complaint: `neighbor_bombs_count`'s name says "count
of bombs among my neighbors," but it silently means something else ("am I a bomb") when
it returns `-1`, and that overloading is exactly what made `reveal`/`reveal_cells`'s
correctness subtle enough to need careful re-derivation earlier in this project's history
(see the `cell/check` vs. `cell/reveal` boom-safety distinction now spelled out in
`server_api.md`). Consider splitting "is this cell a mine" (already available via
`cell_i::is_boom()`) from "how many mines surround this cell" (a plain, always-valid
`unsigned`/`int` with no magic value) at the `board_i`/`cell_i` level, and let callers
combine them explicitly. It would remove an entire class of "wait, what does -1 mean
*here*" questions.

Picking one strategy per failure *kind* (expected for "caller can/should recover and
report to the client," exceptions reserved for "this is a programming error / genuinely
exceptional," no sentinel-int error codes) would make the codebase much easier to reason
about uniformly. Not urgent to unify everywhere at once, but worth a stated convention
going forward, since new code (plugins especially) has no single pattern to copy.

---

## 4. Dead / misleading code

* **`cell_t` (`api.cxx:39-83`) is entirely unused.** `board_t` uses `cell_ext_t`
  instead. Worse, `cell_t`'s `neighbor_bombs_count()` storage is `unsigned int count_ : 3`
  — a 3-bit field can only hold 0–7, but a cell can have up to **8** neighboring mines.
  If this struct were ever wired back in, it would silently truncate/wrap the one count
  that matters most (a fully-mined neighborhood). Delete it, or fix the bit width and
  actually use it — as-is it's dead code with a latent correctness bug baked in.

* **`session_new_handler`'s self-verification block** (`handlers.cxx`, the
  `/* next verification is not required, just to make sure we understand lib api
  correctly */` comment) decodes and verifies the token it *just signed*, on every single
  `POST /session/new` call, purely as a learning-exercise leftover. It does real
  (if cheap) work — an RSA verify — on every session creation for no functional benefit.
  Either delete it or turn it into an actual assertion (`assert`/debug-only), not
  production request-path code.

* **`m_coord_t`'s template parameter and helper are misspelled `dimention` throughout**
  (`plugins/api.hxx`) — `requires(dimention > 0)`, `dim_name`, etc. It's consistent (not
  a functional bug), but it's part of the public plugin API surface documented in
  `server_plugins.md`; a mechanical rename to `dimension` before more plugins start
  depending on the current spelling would be the cheapest time to fix it.

---

## 5. Type/portability smells

* **`parameter_t : std::variant<std::string, int, uint, long, ulong, bool, ...>`**
  (`plugins/api.hxx`) uses `uint`/`ulong` unqualified — these are not standard C++ types;
  they're glibc/POSIX extensions pulled in transitively by whatever header happens to
  declare them first. Prefer `unsigned int`/`unsigned long` (or fixed-width
  `std::uint32_t`/`std::uint64_t`, matching `board_id_t`/`client_id_t` which are already
  `uint64_t`) so the header is self-contained and portable to a standard-only toolchain.

* **The recursive-variant trick documented in `parameter_t`'s own comment**
  (`plugins/api.hxx`) explicitly says it works because "libstdc++'s node-based
  `std::unordered_map` supports incomplete mapped types... in practice" — i.e., by the
  comment's own admission, this relies on implementation-defined behavior, not the
  standard. Fine on the toolchain this project targets, but worth flagging clearly as a
  "don't port this to libc++/MSVC-STL without re-checking" tripwire, since nothing else
  in the codebase signals that risk.

* **`get_id_from_jwt` returns `result_t<int>`** but `client_id_t` (used everywhere else —
  `next_client_id_`, `clients_.find`, etc.) **is `uint64_t`.** `authorize_client` then
  implicitly converts `int -> uint64_t`. A crafted `client_id` claim with a negative
  numeric string parses fine as `int` and wraps to a huge `uint64_t` on conversion — not
  currently exploitable (it just fails the subsequent lookup → `403`), but it's a
  needless type mismatch between the token layer and the rest of the id-based code. Parse
  directly into `client_id_t`.

* **Build targets C++26** (`CMakeLists.txt:4`, `set(CMAKE_CXX_STANDARD 26)`), not C++23.
  Worth a deliberate decision rather than an accidental one — C++26 compiler/library
  support is still actively moving; if there's no specific C++26 feature in active use
  (a scan of the server code didn't turn one up — everything used, `std::expected`,
  `<=>`, concepts/`requires`, is C++20/23), pinning to C++23 would trade nothing for
  meaningfully better toolchain stability.

---

## 6. Operational / lifecycle gaps

* **No graceful shutdown path exists.** `restbed::Service::start()` blocks; nothing in
  this codebase ever calls `service.stop()`, and there's no `SIGINT`/`SIGTERM` handler.
  `unload_plugins()` (`server.cxx:281`, right after `service.start()` returns) is
  consequently unreachable in practice — the only way to stop the server today is to kill
  the process, which bypasses plugin unload entirely. If plugins ever need to flush state
  on shutdown, there's currently no mechanism that will actually invoke it.

* **`main()`'s setup phase is unguarded.** Only `service.start(settings)` has a
  `try/catch` (`server.cxx:274-279`, for `std::system_error`). `addon_api_t`'s
  constructor can throw `std::runtime_error` (`rsa_key_pair` failing to read/generate
  keys, `rsa.cxx:200-224`), as can `create_self_signed_ssl_cert`'s callees in principle —
  none of that is caught, so a bad RSA-key path or unwritable working directory produces
  a raw `terminate called after throwing an instance of...` instead of a clean log
  message and exit code.

* ~~**RSA/TLS material defaults to relative paths in the current working directory**
  (`minesweeper_rsa.pem`, `minesweeper.crt`, etc., `api_impl.hxx:158-161`), same for
  `restbed.log`. Fine for local dev; means where you `cd` before invoking `ms_server`
  silently determines where key material lands. Worth defaulting to a proper config/data
  directory (or at minimum resolving relative to `get_exe_directory()`, which the code
  already computes for the plugins directory) before this goes anywhere near a real
  deployment.~~ **Addressed**: `--log-dir`/`--data-dir` (both defaulting to
  `get_exe_directory()/"logs"` and `.../"data"`, auto-created) now govern where
  `restbed.log`/`plugins.log`/`requests.log` and the RSA keys/SSL cert land; an explicit
  `--rsa-priv-key`/`--rsa-pub-key`/`--ssl-cert`/`--ssl-dh` still overrides its own path
  independent of `--data-dir`. Fixing this also surfaced a real bug: `http_api_t`'s
  constructor used to load RSA keys eagerly, before argv was even parsed, from its own
  hardcoded default path - no `--rsa-priv-key`/`--data-dir` value could ever actually
  affect which key material got loaded, only what the path *getters* reported afterward.
  Split into `load_rsa_keys()`, called once both paths are finalized.

* **Global `extern std::shared_ptr<addon_api_i> api;` is redeclared verbatim in four
  separate translation units** (`handlers.cxx`, `http_auth.cxx`, and each plugin
  `.cxx`) instead of being declared once in a shared header. It's consistent today, but
  nothing enforces that — a future edit to one copy's signature (e.g., adding `const`)
  would be an ODR violation the compiler won't catch across shared-library boundaries.
  Low cost to centralize in `plugins/api.hxx` (or a small internal header for the
  server-only copy) now, rising cost the more files reference it.

---

## 7. Modernization opportunities (nice-to-have, not urgent)

* `<random>` instead of `rand()`/`%` — see 1.1; the bug fix and the modernization are the
  same change.
* `board_t::coord_to_index(coord_t coords)` and a few similar small-struct parameters
  take `coord_t` by value while every sibling method takes `const coord_t&` — harmless at
  `sizeof(std::array<int,2>)`, just an inconsistency worth a pass.
* `m_coord_t`'s variadic constructor (`plugins/api.hxx`) constrains only
  `sizeof...(args) == dimention`, not that each `Args` converts to `int` — passing the
  wrong argument types fails deep inside the `std::array<int, dimention> vec {args...}`
  initializer with a much less readable error than a `requires (std::convertible_to<Args,
  int> && ...)` clause would give.
* `rest_api_response_i::add_property(const std::string& key, parameter_t value)` takes
  the (potentially deeply nested) `parameter_t` by value and copies it again into the
  internal map (`response_t::add_property`, `api.cxx:525-528`). Not a measurable problem
  at this project's scale, but an easy `std::move`-friendly signature if response-building
  ever needs to handle larger payloads.

---

## 8. What's already in good shape

Worth naming explicitly so it doesn't get lost among the findings above:

* The plugin boundary (`addon_api_i`/`http_api_i`/`board_i`/`cell_i`, all in
  `plugins/api.hxx`) is a clean, fully-virtual seam — plugins genuinely cannot reach past
  it into server internals, which is exactly the property it was designed for.
* `std::expected`-based (`result_t<T>`) error propagation for the auth path is a good,
  modern fit for "this can fail, caller must look" — see the sentinel-value critique in
  §3 for where the codebase *doesn't* yet do this consistently.
* The `format`/`content_type_t` abstraction (json/yaml/xml from one `parameter_map_t`) is
  a well-factored, DRY way to support multiple wire formats without triplicating handler
  logic.

---

## Suggested priority order

1. §1.1–1.7 (all are cheap, isolated fixes with concrete, demonstrated failure modes).
2. Decide and document a concurrency contract for `boards_`/per-client boards (§2) before
   any plugin starts relying on `add_board` outside startup.
3. §3's error-handling convention, stated once, applied going forward (not a big-bang
   refactor of existing code).
4. Everything else here is cleanup/hardening that can happen opportunistically.
