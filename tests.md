# Testing

This project has two kinds of automated tests, both driven by CTest:

- **Unit tests** (`ms_test`) — pure in-process C++ logic, no server, no network. Currently
  covers `parameter_bytestream_t` (`src/server/plugins/api.hxx`): store/load round-trips,
  bounds checking, checksum/version-mismatch rejection.
- **Live replay test** (`live_replay`) — spawns a real `ms_server` (with plugins) and
  replays a captured request/response log against it, asserting the game outcome is
  unchanged. This is the only test that exercises the actual REST API, auth, and the
  plugin ABI end to end.

For the REST API itself see [`server_api.md`](server_api.md); for the plugin ABI these
tests also exercise, see [`server_plugins.md`](server_plugins.md).

## Building

Both are built by default:

```
option(BUILD_TESTS "Build tests" ON)
```

`ms_test` additionally needs `GTest` (found via `find_package`, or fetched via
`FetchContent` if not found). `live_replay` needs a `Python3` interpreter
(`find_package(Python3 COMPONENTS Interpreter)`) and `SERVER_SUPPORT_PLUGINS` to be on —
the committed fixture log exercises `cell/check`, which is plugin-hosted, so there's
nothing meaningful to replay without plugins built. If either dependency is missing, that
test is silently skipped at configure time (not registered with CTest at all) rather than
failing the build.

```
cmake --build build --target ms_server cell_check boards_list ms_test
```

## Running everything

```
ctest --test-dir build --output-on-failure
```

(`--test-dir` works from any directory; `cd build && ctest ...` works too if you're
already there.)

Filter to just one:

```
ctest --test-dir build -R live_replay --output-on-failure
ctest --test-dir build -R ParametersBitstream --output-on-failure
```

## Running `ms_test` directly

```
./build/ms_test                              # all cases
./build/ms_test --gtest_filter='ParametersBitstream.RejectsCorruptedPayload'
```

## Running the live replay directly

`live_replay` is a thin CTest wrapper around
`src/server/tests/live/replay_requests_log.py`, which is also useful on its own — it's a
general-purpose "replay this requests.log against a server" tool, not tied to any one
fixture.

It can either spawn `ms_server` itself (what the CTest target does):

```
python3 src/server/tests/live/replay_requests_log.py \
    --log src/server/tests/live/fixtures/board11_two_sessions.requests.log \
    --server-bin build/ms_server \
    --plugins-dir build/plugins \
    --port 18099
```

or replay against a server you already have running (e.g. one you started by hand to
watch it work, or a remote/staging instance):

```
python3 src/server/tests/live/replay_requests_log.py \
    --log src/server/tests/live/fixtures/board11_two_sessions.requests.log \
    --base-url http://localhost:8080
```

### How replay works

The log format is exactly what `response_t::send()` (`src/server/api.cxx`) writes to
`requests.log` — one line per request (`METHOD /path?query`), immediately followed by one
line per response (`-> STATUS body-json`). A session in the log is a
`POST /session/new...` call followed by everything that client did with the token it got
back. Replay starts a **fresh** session per block (a fresh JWT — tokens expire and aren't
meant to be reused run to run) and reattaches the new token as the bearer token for every
subsequent request in that block, mirroring what the original client did.

Everything after that must be deterministic — same fixed board, same request sequence in,
same responses out — for one reason: **board #11**, created at server startup alongside
the usual 10 random ones, has an explicit, hardcoded mine layout instead of one placed by
`rand()`. `rand()`'s actual output isn't standardized across platforms/compilers/libc, so
a "fixed seed" wouldn't reproduce the same board elsewhere anyway — an explicit layout is
the only way to get a board whose outcome for a given sequence of moves is reproducible
across machines. **Any fixture log you want replay to verify must have played against
board #11** (`POST /session/new?board_id=11`) — replaying a log captured against one of
the 10 random boards will fail unpredictably, through no fault of the server.

### Failure vs. warning

Every action handler (`cell/reveal`, `cell/flag`, `cell/check`, `board/check`) reports its
outcome through a top-level `"status"` field with a fixed vocabulary: `"ok"`/`"boom"` from
the cell actions, `"win"`/`"lose"` from `board/check`. On a fixed board that outcome must
never change, no matter how the rest of a response body evolves — so replay treats a
mismatch there specially:

- **HTTP status code mismatch**: always a failure.
- **`"status"` (outcome) field mismatch**: always a failure, regardless of any flag — a
  boom, win, or loss flipping to something else on the same board and the same moves is a
  real regression.
- **Any other body mismatch** (a field added, removed, or changed elsewhere in the
  response): a **warning** by default, not a failure. The response format is expected to
  keep gaining fields over time (e.g. folding `bombs_count`/`fully_revealed` into action
  replies so a client doesn't need a separate follow-up request) — that's deliberate
  evolution, not a regression, and shouldn't force every fixture to be re-captured just to
  keep the test green.

Pass `--warnings-as-error` to promote those body-shape warnings to failures too, e.g.
right after intentionally changing a response shape, to see the full list of what you're
about to change before updating (or re-capturing) the fixture.

## Capturing a new fixture log

Start the server against board #11 and play through it (TUI client, `curl`, whatever) —
every request and response is written to `requests.log` in the server's working directory
automatically, no flag needed. Copy the relevant session(s) out of it into
`src/server/tests/live/fixtures/`, then point `--log` (or the CMake `add_test(...)` call
for `live_replay`) at the new file. A log can contain multiple sessions back to back;
replay handles each independently.
