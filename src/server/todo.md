* rename field/new to session/new 
* add session/stop POST handler to quit midgame or on win/lose -- remove session and allocated field
* add session/restart POST handler to finish the current game and start a new one with the same field id
* when session created – save time as start time in session
* add session/time GET handler to return elapsed time since session start
* add fully_revealed field to action/flag and action/reveal replies – so client doesn't have to call field/fully_revealed to know if game is over
* add action/confirm POST handler (name subject to change) to check non-zero revealed cell's neighbors (middle-click functionality)
* plugin system: `addon_api_i` is only ABI-safe between binaries built with the exact
  same compiler/stdlib/flags (see server_plugins.md's "ABI compatibility" section and
  the `abi_tag()` check this now enforces) - not for genuine 3rd-party plugins on a
  different toolchain. Redo the boundary to move data across as serialized protobuf
  bytes through a plain-C entry point (`const uint8_t*, size_t` in/out) instead of
  passing std::string/std::optional/std::variant/exceptions through a C++ virtual
  interface directly - decouples the two sides' compiler/stdlib version from each
  other for the *data*, since only the wire format needs to agree. Doesn't fully
  solve it on its own though: the *calling convention* (session/board handles,
  logging, resource registration - everything that isn't a data payload) still needs
  a plain-C ABI of its own (opaque handles + function pointers, no exceptions), since
  you can't protobuf-encode a live `restbed::Session&`. Worth prototyping as one
  request/response message pair (e.g. cell_check's request+result) before committing
  to reworking the whole surface.
* plugin system: `boards_` (in `addon_api_t`) has no mutex, unlike `clients_`. Safe
  today only because `add_board()` is exclusively called at startup before the server
  starts serving; a data race waiting to happen if anything ever calls it at request
  time while other threads read via `board()`/`for_each_board()`.