# Minesweeper Server Plugins

This document describes how to write, build, and run a server plugin — a shared library
that contributes new REST resources to the `server` binary. For the REST API itself
(built-in and plugin-provided endpoints alike), see [`server_api.md`](server_api.md).

Plugins are loaded at runtime via `dlopen`, and only ever see the small, fully virtual
interface declared in `src/server/plugins/api.hxx`. They never link against or include
server-internal headers (`handlers.hxx`, `http_auth.hxx`, `api_impl.hxx`) — everything a
plugin needs (looking up boards, logging, registering resources) goes through the
`plugin_api_i` object handed to it at load time. A plugin handler never sees a
`restbed::Session`, builds a response object, or calls an auth function itself — it
declares what it needs (a board? which query params?) at registration, and the host
does auth, param parsing, and response construction generically before/after calling it.
See "Anatomy of a plugin" below for the two handler shapes this makes possible.

## Enabling / building plugin support

Plugin support is a build-time option:

```
option(SERVER_SUPPORT_PLUGINS "Enable support for plugins" ON)
```

It's on by default. When it's off, `SERVER_SUPPORT_PLUGINS=0` is compiled in, the server
never calls `dlopen`, and `src/server/plugins/` isn't even added as a build subdirectory.

Registering new resources also depends on `PalSigslot` being available
(`find_package(PalSigslot)`); when found, the build gets `USE_PALSIGSLOT=1` and
`plugin_api_i` exposes the `ready_to_load_resources_signal()` used below to register
resources at the right time. Existing plugins assume this is available.

Each plugin is its own CMake shared-library target, built from
`src/server/plugins/CMakeLists.txt`:

```cmake
set(TARGET_PLUGIN_my_plugin my_plugin)
set(SOURCES_PLUGIN_MY_PLUGIN my_plugin.cxx)

add_library(${TARGET_PLUGIN_my_plugin} SHARED ${SOURCES_PLUGIN_MY_PLUGIN})
target_link_libraries(${TARGET_PLUGIN_my_plugin} PRIVATE -lfmt)
if (PKGCONFIG_restbed_FOUND)
    target_link_libraries(${TARGET_PLUGIN_my_plugin} PRIVATE PkgConfig::restbed)
endif ()
set_target_properties(${TARGET_PLUGIN_my_plugin} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
```

Add a block like this (copy `boards_list`'s or `cell_check`'s) for every new plugin.
`LIBRARY_OUTPUT_DIRECTORY` must point at `${CMAKE_BINARY_DIR}/plugins` — that's the
directory the server loads `.so` files from by default (see below).

## Running with plugins

```
ms_server --plugins-dir <dir>
```

`--plugins-dir` defaults to `<directory containing the ms_server binary>/plugins`. The
server loads **every** `.so` file directly inside that directory (non-recursive) at
startup, in directory-iteration order (not guaranteed to be alphabetical). If the option
isn't `SERVER_SUPPORT_PLUGINS`-enabled at build time, the `--plugins-dir` CLI option
doesn't even exist.

Startup logs confirm what happened, e.g.:
```
[info] Loading plugins from build/plugins
[info] Loading plugin build/plugins/libcell_check.so
[debug] Plugin cell_check[1.0.0] loaded successfully
[debug] Plugin cell_check[1.0.0] is adding new resource cell/check
[info] Resource published on route '//cell/check'.
```

A plugin that fails to load (missing `init_plugin` symbol, `dlopen` failure) is logged
as an error and skipped — it does not abort startup.

## Anatomy of a plugin

A plugin is a shared library exporting five `extern "C"` symbols (four metadata/ABI
functions plus the entry point) and should also export a sixth, `unload_plugin`:

```cpp
extern "C" {
constexpr const char* name( );
constexpr const char* version( );
constexpr const char* description( );
const char*           abi_tag( );
void                  init_plugin(plugin_api_i& api);
void                  unload_plugin( );
}

ADDON_PLUGIN_ABI_TAG( )  // defines abi_tag() above - see api.hxx and "ABI compatibility" below
```

* `name()` / `version()` / `description()` — used only for logging.
* `abi_tag()` — required, defined for you by invoking the `ADDON_PLUGIN_ABI_TAG()` macro
  once at namespace scope (as above; see both shipped plugins). The server refuses to
  load a plugin missing this export, or reporting a tag that doesn't match its own — see
  "ABI compatibility" below.
* `init_plugin(api)` — called once at load time, only after the ABI check passes. Store
  the pointer (typically in an anonymous-namespace file-local `plugin_api_i*`, see both
  shipped plugins) and use it for everything else the plugin does.
* `unload_plugin()` — called (if present, looked up via `dlsym`) when the server shuts
  down, before `dlclose`. This is not just optional cleanup: it's your only chance to
  disconnect anything you connected to `ready_to_load_resources_signal()` (see
  `install_resource` below) and drop your own `plugin_api_i*` *before* it's dangling.
  `main()` guarantees `api` is still alive, and every plugin's `.so` is still mapped,
  when `unload_plugin()` runs — see "Why references and function pointers, not
  `shared_ptr`/`std::function`" below for why this matters and what can go wrong if you
  skip it.

### The two handler shapes

A handler is one of two distinct function-pointer types, not one signature plus a
"does this need auth" flag. The actual ABI-level types speak only in bytes — no
`parameter_t`, `parameter_map_t`, or `std::string` crosses this function-pointer call as
a C++ object, only pointers and sizes:

```cpp
using simple_handler_t = size_t (*)(const std::byte* params_buf, size_t params_len, std::byte* out_buf, size_t out_cap);
using board_handler_t  = size_t (*)(board_i& board, const std::byte* params_buf, size_t params_len, std::byte* out_buf, size_t out_cap);
```

This is the same "don't cross the ABI boundary as a type-erased/heap-owning C++ object"
reasoning as "Why references and function pointers, not `shared_ptr`/`std::function`"
below — extended from the callback mechanism itself to the *data* crossing it, since a
`parameter_map_t` (a `std::unordered_map` of a recursive `std::variant`) passed by value
would otherwise need host and plugin to agree on more than layout: allocator/heap
behavior for everything nested inside it too.

**You never write a function matching either signature directly.** You write an
ordinary `parameter_map_t`-in/`handler_result_t`-out function — exactly the shape this
document has always described — and register it through one of two adapter templates
instead of passing the bare function:

```cpp
handler_result_t my_handler (board_i& board, const parameter_map_t& params);
// ...
api->add_resource(path, method, board_handler_adapter<my_handler>, params);
```

`simple_handler_adapter<Handler>`/`board_handler_adapter<Handler>` (declared in
`api.hxx`, right after `plugin_api_i`) `load()` the incoming `parameter_map_t` from
`params_buf` via `parameter_bytestream_t`, call `Handler` exactly as before, catch any
exception it throws (turned into a 500 — `Handler` never needs its own top-level
`try`/`catch`), and `store()` the result back into `out_buf`. `Handler` is a *non-type
template parameter*, so each instantiation is itself an ordinary, capture-free function —
a valid `simple_handler_t`/`board_handler_t` value — compiled by whichever side (host or
plugin) registers it; nothing about how you write `my_handler` itself changes, only the
one line that registers it.

`handler_result_t` is `std::expected<parameter_map_t, handler_error_t>`, where
`handler_error_t` is `{int http_code; std::string message;}`. A handler never touches a
`restbed::Session`, builds a response object, or picks a content type — it just returns
the properties to send back on success, or a status code + message on failure, and the
host converts that into a sent response.

The two shapes are registered through two different `add_resource` overloads:

```cpp
void add_resource(std::string_view path, http_methods_t method, simple_handler_t handler, std::vector<param_spec_t> params = { });
void add_resource(std::string_view path, http_methods_t method, board_handler_t  handler, std::vector<param_spec_t> params = { });
```

(passing `simple_handler_adapter<my_handler>`/`board_handler_adapter<my_handler>` — see
above — not `my_handler` itself.)

Only `add_resource`'s `board_handler_t` overload authenticates the caller and resolves
*their* board (via the same JWT-bearer-token flow `server_api.md` documents) before ever
calling the handler — a `board_handler_t` handler is *always* called with an
authenticated caller's own board, and there's no other way to obtain one. A resource
that needs no per-client board (or resolves one itself some other way, e.g. `session/new`
picking a board by an explicit id) uses `simple_handler_t` instead and gets no
authentication step at all. There is no separately-settable "requires auth" bool to get
wrong: the handler's own type is the declaration of what it needs, checked at compile
time by which overload it's passed to.

`params` declares the query parameters the host should parse before calling the handler:

```cpp
struct param_spec_t {
  std::string_view name;
  param_type_t      type          = param_type_t::string;  // string | integer | boolean
  bool              required      = false;
  parameter_t       default_value = std::string { };       // used when absent and not required
};
```

The host parses every declared param from the query string before the handler runs: a
missing `required` one, or one that fails to parse as its declared type, short-circuits
straight to a 400 (`"missing mandatory parameter x"` / `"invalid parameter x"`) without
ever calling the handler. A handler can then just do `std::get<int>(params.at("x"))`
and trust that it's there and well-formed.

### Registering resources

Don't call `add_resource` directly inside `init_plugin`. Resources are collected into a
list and only actually published on the `restbed::Service` once, after all plugins have
had a chance to register — connect to `ready_to_load_resources_signal()` instead and add
your resources from the connected slot:

```cpp
#include <fmt/format.h>
#include <fmt/std.h>

#include <corvusoft/restbed/status_code.hpp>
#include <memory>
#include <sigslot/signal.hpp>

#include "api.hxx"

extern "C" {
constexpr const char* name( );
constexpr const char* version( );
constexpr const char* description( );
const char*           abi_tag( );
void                  init_plugin(plugin_api_i& api);
void                  unload_plugin( );
}

ADDON_PLUGIN_ABI_TAG( )

namespace {
plugin_api_i*                api;
constexpr std::string_view  rest_resource_path = "my/thing";

// board_handler_t: the host already authenticated the caller and resolved
// "board" to be *their* board, and already validated "n" per the param_spec_t
// below, by the time this runs.
handler_result_t my_handler (board_i& board, const parameter_map_t& params) {
  const int n = std::get<int>(params.at("n"));
  if ( n > static_cast<int>(board.width( )) ) {
    return std::unexpected(handler_error_t {restbed::BAD_REQUEST, "n out of range"});
  }
  return parameter_map_t {{"status", "ok"}, {"width", board.width( )}};
}

void install_resource ( ) {
  api->add_resource(rest_resource_path, http_methods_t::GET, board_handler_adapter<my_handler>,
      {
          {.name = "n", .type = param_type_t::integer, .required = true},
  });
}
}  // namespace

constexpr const char* name ( ) { return "my_plugin"; }
constexpr const char* version ( ) { return "1.0.0"; }
constexpr const char* description ( ) { return "Does a thing"; }

void init_plugin ([[maybe_unused]] plugin_api_i& api_iface) {
  api = &api_iface;
  api->ready_to_load_resources_signal( ).connect(install_resource);
}

void unload_plugin ( ) {
  api->ready_to_load_resources_signal( ).disconnect(install_resource);
  api = nullptr;
}
```

See `src/server/plugins/boards_list.cxx` for the simplest possible `simple_handler_t`
(no params, no board) and `src/server/plugins/cell_check.cxx` for a fuller
`board_handler_t` example.

## The `plugin_api_i` object

Everything a plugin can do goes through the `plugin_api_i` interface
(`src/server/plugins/api.hxx`). The relevant surface for plugin authors:

**Logging**
```cpp
enum log_level_t { trace, debug, info, warn, error, critical };
void log(log_level_t level, std::string_view msg) const;
void log(log_level_t level, fmt::format_string<Args...> fmt, Args&&... args) const; // fmt overload
```

**Registering resources** — see "The two handler shapes" above for `simple_handler_t`/
`board_handler_t`, `param_spec_t`, and `handler_result_t`/`handler_error_t`.
`parameter_t` (used in both `params` and a handler's returned `parameter_map_t`) is a
recursive variant (`string`/`int`/`uint`/`long`/`ulong`/`bool`/`parameter_list_t`/
`parameter_map_t`) — see the "Response format" section in `server_api.md` for how
arrays/maps get serialized across json/yaml/xml.

**Boards and clients**
```cpp
size_t                     boards_count( ) const noexcept;
board_id_t                 add_board(std::unique_ptr<board_i> board);
using board_manipulation_func_t = void (*)(void* user_data, board_id_t, board_i&);
void                       for_each_board(board_manipulation_func_t func, void* user_data);
board_i&                   board(board_id_t board_id);                    // throws std::out_of_range if not found
std::optional<client_id_t> add_new_client(board_id_t board_id);
std::optional<board_i&>    board_for_client(client_id_t client_id);
std::unique_ptr<board_i>   create_board(std::size_t width, std::size_t height, int bombs_count);
std::unique_ptr<board_i>   create_fixed_board(std::size_t width, std::size_t height, std::vector<coord_t> mines); // explicit mine layout, not rand()-placed
```
`board_for_client` is the per-client independent copy of the board (see
`server_api.md`'s session model) — it's what the host resolves and hands your
`board_handler_t` handler once authentication succeeds, so a plugin handler itself
never needs to call it (or `authorize_client`) directly. `board`/`add_board`/
`boards_count`/`for_each_board` are still there for code that needs a board by id
rather than "the current client's board" (`session/new` and `boards_list` both work
this way — see below).

`for_each_board` takes a plain callback plus an opaque `user_data` pointer instead of a
capturing lambda (again, see "Why references and function pointers" below) — thread
per-call context through `user_data` and cast it back inside the callback:
```cpp
void collect_board (void* user_data, board_id_t board_id, board_i& board) {
  auto& boards = *static_cast<parameter_list_t*>(user_data);
  parameter_map_t board_info;
  board_info.emplace("board_id", board_id);
  board_info.emplace("width", board.width( ));
  board_info.emplace("height", board.height( ));
  boards.emplace_back(board_info);
}
// ...
parameter_list_t boards;
api->for_each_board(collect_board, &boards);
```
(this is exactly how `boards_list` implements `GET /boards/list`, see below).

**The board itself** — `board_i` (obtained from `board_for_client`/`board`/`create_board`):
```cpp
std::size_t width( ) const noexcept;
std::size_t height( ) const noexcept;
coord_t     coord(int x, int y) const noexcept;        // validated; falsy if out of range
cell_i&     cell(const coord_t& coord);                 // throws std::out_of_range if !coord
int         bombs_total( ) const;
int         flags_count( ) const;
int         bombs_count( ) const;                        // bombs_total() - flags_count()
int         unrevealed_count( ) const;
std::vector<coord_t> neighbors(const coord_t& coord) const;
int         neighbor_bombs_count(const coord_t& coord) const;
int         neighbor_flags_count(const coord_t& coord) const;
int         neighbor_revealed_count(const coord_t& coord) const;
int         neighbor_unrevealed_count(const coord_t& coord) const;
bool        none_of_cell(std::function<bool(const cell_i&)> func) const; // std::function is fine here: called synchronously, never stored
int         reveal(const coord_t& coord);                              // reveals a single cell
std::vector<reveal_result_t> reveal_cells(const coord_t& coord);        // auto-reveal flood-fill
```
`reveal_cells` reveals `coord`, and if it turns out to have 0 neighboring mines, keeps
recursively expanding into its neighbors (and their neighbors, ...) for as long as each
newly revealed cell also has 0 neighboring mines. `reveal_result_t` is
`{coord_t coord; int count;}` (`count < 0` means that cell was a mine). **Note:**
`reveal_cells` does *not* check whether `coord` itself is a mine before revealing it —
callers seeding it with an already-verified-safe cell (as `cell/reveal` does, via
`cell_i::is_boom()`) can never boom past the entry cell, since a 0-neighbor-mine cell's
neighbors are themselves guaranteed safe by definition. But a caller that seeds it with
an unverified cell — as the `cell_check` plugin does, calling `reveal_cells` on every
flag-count-satisfied neighbor without checking `is_boom()` first — can boom a real mine
if the flags around the reference cell don't actually sit on all of its mines (see
`src/server/plugins/cell_check.cxx`).

**Cells** — `cell_i` (from `board.cell(coord)`):
```cpp
bool is_revealed( ) const;
bool is_flag( ) const;
bool is_boom( ) const;         // is a mine
void set_revealed( );
void set_boom( );
void toggle_flag( );
int  neighbor_bombs_count( ) const;
void set_neighbor_bombs_count(int count);
```

**Coordinates** — `coord_t` (alias for `m_coord_t<2>`):
```cpp
int  x( ) const noexcept;
int  y( ) const noexcept;
int  operator[](size_t index) const noexcept;         // 0=x, 1=y
     operator bool( ) const noexcept;                  // false for the "invalid" sentinel
std::string dim_name(int i) const noexcept;            // "x"/"y" — for building responses generically
```

**RSA/SSL paths** (rarely needed by plugins; used internally for JWT signing/HTTPS) live
on `http_api_i`, not directly on `plugin_api_i` — reach them through `http_api()`:
```cpp
http_api_i* http_api( ); // on plugin_api_i

// on http_api_i:
std::filesystem::path rsa_priv_key_path( ) const;
std::filesystem::path rsa_pub_key_path( ) const;
std::filesystem::path ssl_cert_path( ) const;
std::filesystem::path ssl_dh_path( ) const;
const std::string&    rsa_private_key( ) const;
const std::string&    rsa_public_key( ) const;
```
`http_api_i` also has the `set_*` counterparts of the four paths above (the host applies
CLI-configured paths through them at startup) and `authorize_client(restbed::Session&)` —
used internally by the host's dispatch to implement `board_handler_t`'s auth step, not
something a plugin handler calls itself (see "The two handler shapes" above).

## Why references and function pointers, not `shared_ptr`/`std::function`

Every type on the `plugin_api_i`/`http_api_i`/`board_i` boundary is a plain reference,
raw pointer, or C-style function pointer (`simple_handler_t`, `board_handler_t`,
`board_manipulation_func_t`) — deliberately, not `std::shared_ptr` or `std::function`.
This isn't style preference;
plugins are loaded with `dlopen` and unloaded with `dlclose`, and `dlclose` unmaps a
plugin's code unconditionally, with no awareness that some object elsewhere might still
reference it.

A `std::function` (or a capturing lambda passed where one is expected) is type-erased:
its "manager" code — the thunk that knows how to call, copy, and destroy the wrapped
callable — is compiled into whichever translation unit instantiates it, which for a
plugin-supplied callback means *inside the plugin's `.so`*. If that `std::function`
object outlives `dlclose()`-ing the plugin — because it ended up stored in something
the main app owns for longer, e.g. a registered REST resource or a signal connection —
destroying or invoking it later jumps into unmapped memory. Likewise a
`shared_ptr<plugin_api_i>` handed to a plugin creates a second owner of the server's
`plugin_api_t`: if a plugin never releases its copy, the object can end up being destroyed
from *inside* some other plugin's `dlclose`, after the first plugin's code is already
gone but before the object holding a connection into it is.

A plain function pointer has none of this: it's just an address, with no manager code
generated anywhere, so it stays valid as data as long as you don't call through it after
the pointee is unmapped. That's why `unload_plugin()` matters — it's your one guaranteed
window (while `api` and every plugin's `.so`, including your own, are still mapped) to
disconnect from `ready_to_load_resources_signal()` and drop your `plugin_api_i*`, so that
nothing plugin-side survives to be touched after any `.so` is closed. `main()` on its
side destroys `service` and calls `unload_plugin()` on every plugin *before* dlclosing
any of them, and only destroys `api` itself afterwards — see `shutdown_guard_t` in
`src/server/server.cxx` for the exact ordering and why it's structured as an RAII guard
rather than cleanup code at the end of `main()` (every early-return path, including an
exception from a failed `bind()`, needs the same ordering guarantee).

## ABI compatibility

A virtual C++ interface like `plugin_api_i` only has a well-defined, agreed memory
layout between binaries built with the same compiler, the same standard library
(never mix libstdc++ and libc++), the same `_GLIBCXX_USE_CXX11_ABI` setting, and the
same version of `api.hxx` itself (it's a header, and `sigslot::signal<>` inside it is
also header-only — any drift in flags or version changes its layout too). There is no
general, portable way to make this kind of ABI safe across arbitrary
compilers/toolchains — that would need a plain-C ABI instead (`const char*`/`void*`/
fixed-width integers only, no exceptions crossing the boundary), which is a much
bigger, deliberate redesign, not something to bolt on incrementally (see the
protobuf-boundary idea in `src/server/todo.md`). **Until/unless that happens, build
every plugin with the exact same toolchain and flags as the server**, from the same
`api.hxx`.

To turn a violation of that rule into a loud, logged refusal instead of a
memory-corruption crash sometime later, every plugin must export `abi_tag()` (via
`ADDON_PLUGIN_ABI_TAG()`, see above). It reports a string combining `api.hxx`'s
`ADDON_API_ABI_VERSION`, the compiler identity, `__cplusplus`, and
`_GLIBCXX_USE_CXX11_ABI`, computed at compile time in the plugin's own translation
unit. The server computes the same tag for itself and compares before calling
`init_plugin()`; on any mismatch (or a missing `abi_tag()` export), it logs an error
naming both tags and skips the plugin entirely, the same way it already does for a
plugin missing `init_plugin()`.

**Bump `ADDON_API_ABI_VERSION`** (in `api.hxx`) whenever `plugin_api_i`/`http_api_i`/
`board_i`/`cell_i` change shape in a way that would make an already-built plugin call
the wrong vtable slot — reordering, removing, or changing the signature of an existing
virtual method. Appending a new virtual method at the end doesn't need a bump on its
own merits, but doing so still means every not-yet-rebuilt plugin is now built against
an incomplete view of the interface, so bump anyway if you want old plugins refused
rather than silently missing the new capability.

**Implementation note, if you ever touch `addon_api_abi_tag()` in `api.hxx`:** it must
stay `static` (internal linkage), never `inline`. This function is defined in a header
included by both the server executable and every plugin — an `inline` (weak,
default-visibility) definition would be resolved through the process's global symbol
scope, and an executable's own exported symbols always win that resolution for
anything it `dlopen()`s, regardless of `RTLD_LOCAL` on the loaded library. That would
silently interpose the *host's* copy of this function into every plugin's call to it,
making the check compare the host's tag against itself no matter what the plugin was
actually built with — which is exactly what happened the first time this was written
with `inline`, and is the kind of bug that's easy to reintroduce by "simplifying" this
back to `inline` later.

## Shipped examples

* **`boards_list`** (`src/server/plugins/boards_list.cxx`) — `GET /boards/list`. The
  simplest possible plugin: a `simple_handler_t` with no declared params, iterates
  `for_each_board` (via the callback+`user_data` pattern above) and reports
  `board_id`/`width`/`height` for each.
* **`cell_check`** (`src/server/plugins/cell_check.cxx`) — `POST /cell/check`. Full
  example of a `board_handler_t` with two required `integer` params (`x`/`y`): the host
  has already authenticated the caller, resolved their board, and validated `x`/`y` are
  present and numeric by the time the handler runs, which then validates board/cell
  state and calls `board.reveal_cells(...)` per qualifying neighbor.

Both are fully documented from the client's perspective in `server_api.md`.

## Conventions to follow

* Only include `"api.hxx"` (i.e. `src/server/plugins/api.hxx`) — never reach into
  `handlers.hxx`, `http_auth.hxx`, or `api_impl.hxx`. If something a plugin needs isn't
  exposed on `plugin_api_i`/`http_api_i`/`board_i`/`cell_i`, that's a gap in those
  interfaces to fix, not a reason to link against server internals.
* Register with the handler shape matching what you actually need — `board_handler_t`
  if you need the caller's authenticated board, `simple_handler_t` otherwise — rather
  than declaring one and then manually resolving (or skipping) auth inside the handler
  body. Declare every query param the handler needs via `param_spec_t` at registration
  instead of parsing the query string by hand; the host validates presence/type for you
  before the handler ever runs.
* Keep a plugin's own `plugin_api_i*` in an anonymous namespace, matching both shipped
  plugins — never `std::shared_ptr<plugin_api_i>` (see above for why).
* Always define `unload_plugin()`, disconnecting anything you connected to
  `ready_to_load_resources_signal()` and then nulling your `plugin_api_i*`, matching both
  shipped plugins. Skipping it leaves a live connection into your `.so` for `api`'s
  signal to hold past your own `dlclose`.
* Prefer lowercase error messages (`"can't reveal flagged cell"`) to match the built-in
  handlers' style — `cell_check` uses capitalized messages (`"Cell is not revealed"`),
  which is an inconsistency in the current codebase, not something to imitate.
* Log liberally at `debug` level inside handlers (both shipped plugins do) — plugin
  loading/unloading and resource registration happen far from where a request is
  eventually served, so debug logs are the easiest way to trace what a loaded plugin is
  doing.
