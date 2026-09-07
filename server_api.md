# Minesweeper Server API

This document describes the **HTTP REST API** exposed by the `server` binary. It does
not cover the C++ plugin API used to extend the server — see
[`server_plugins.md`](server_plugins.md) for that.

The server is built on top of [restbed](https://github.com/Corvusoft/restbed). Besides a
core set of always-available resources, it can load optional plugins (shared libraries)
that contribute additional resources; two are shipped with the project
(`boards/list`, `cell/check`, documented below alongside the core endpoints, since from
a client's point of view they're ordinary resources). Whether they're actually available
depends on how the server was started (see `--plugins-dir` and `SERVER_SUPPORT_PLUGINS`
in `server_plugins.md`).

## General conventions

### Response format

Every response body is a flat key/value map. The representation is controlled by the
`format` query parameter, which is accepted by **every** endpoint:

| `format` value                              | `Content-Type`        |
|----------------------------------------------|------------------------|
| `json` (default, or any unrecognized value)   | `application/json`     |
| `yaml`                                        | `application/x-yaml`   |
| `xml`                                         | `application/xml`      |

`format` also accepts the full MIME type (`application/json`, `application/x-yaml`,
`application/xml`) in addition to the short aliases `json`/`yaml`/`xml`. XML bodies are
serialized with [tinyxml2](https://github.com/leethomason/tinyxml2) as a `<response>`
root element containing one child element per field, e.g.:
```xml
<response>
    <status>ok</status>
    <x>3</x>
    <y>4</y>
</response>
```

Example JSON body:
```json
{
  "status": "ok",
  "x": 3,
  "y": 4
}
```

Values are serialized using their native type — numbers as numbers, booleans as JSON
`true`/`false` (not stringified), strings as strings.

A field value can also be an **array of values**, or a **map of named values** — the
elements of an array, and the values in a map, are not required to share a type: each
one can independently be a scalar, another array, or another map, nested arbitrarily
(arrays of arrays, maps of maps, arrays containing maps, maps containing arrays, etc.),
recursively — the `cells` array returned by `cell/reveal` is the one endpoint that
actually uses this today. Arrays
are serialized as a JSON/YAML array, or in XML as a sequence of `<item>` elements (nested
arrays produce nested `<item>` elements); maps are serialized as a JSON/YAML object/map,
or in XML as one child element per map key (nested maps produce nested child elements),
e.g.:
```json
{
  "values": [1, 2, ["a", true]],
  "info": {"name": "foo", "tags": [1, 2]}
}
```
```xml
<response>
    <values>
        <item>1</item>
        <item>2</item>
        <item>
            <item>a</item>
            <item>true</item>
        </item>
    </values>
    <info>
        <name>foo</name>
        <tags>
            <item>1</item>
            <item>2</item>
        </tags>
    </info>
</response>
```

### Errors

On error, the server replies with a non-2xx HTTP status code and a body containing a
single `error` field with a human-readable message, encoded using the same `format`
rules described above:

```json
{
  "error": "missing mandatory parameter id"
}
```

**Exception:** authorization failures caused by a malformed (not just expired/invalid)
JWT — e.g. a garbage `Authorization` header value — currently surface as a `500` response
with a **plain-text** body (not the `{"error": ...}` JSON shape above), since token
decoding happens outside the code path that formats errors. A syntactically valid but
expired or signature-invalid token does go through the normal `401` `{"error": ...}` path.

### Coordinates

Cell coordinates `x` and `y` are **1-based**. Valid values are `1 <= x <= width` and
`1 <= y <= height`. Out-of-range coordinates result in a `400` error
(`"coordinates out of range"`).

### Authentication / session model

A "client" represents a single player session bound to one board. A client is created
via `POST /session/new`, which returns a signed JWT `token`. That token must be sent as
a bearer credential on every other endpoint:

```
Authorization: Bearer <token>
```

The token embeds the client's id as a claim, is signed with RS256, issued by
`"minesweeper"`, and expires 24 hours after issuance. Requests with a missing
`Authorization` header, or an expired/invalid token, get a `401` response; see the
Errors section above for the malformed-token special case.

Multiple clients can be bound to the same `board_id`, but each client gets its own
**independent copy** of that board's initial layout. This means several players can start
a session from the same board template (same size and mine layout), but they are *not*
playing on a shared board: actions performed by one client (revealing or flagging cells)
only affect that client's own copy and are invisible to, and unaffected by, other clients
bound to the same `board_id`.

---

## Endpoints

### `POST /session/new`

Creates a new client (game session) bound to a board.

**Query parameters:**

| Name       | Required | Description                                                                                              |
|------------|----------|-----------------------------------------------------------------------------------------------------------|
| `board_id` | no       | Id of an existing board to attach the new client to. If omitted, a board is picked at random from those available. |
| `format`   | no       | Response format (`json`\|`yaml`\|`xml`), default `json`.                                                  |

**Success response `200`:**

| Field   | Description                              |
|---------|--------------------------------------------|
| `token` | Bearer JWT identifying the new client. Pass it as `Authorization: Bearer <token>` on subsequent calls. |

**Errors:**

| Status | Condition                                                    |
|--------|-----------------------------------------------------------------|
| `400`  | `board_id` parameter is not a valid number.                     |
| `500`  | Failed to create a new client.                                  |

---

### `GET /board/size`

Returns the dimensions of the board associated with the authenticated client.

**Headers:** `Authorization: Bearer <token>` (required)

**Query parameters:**

| Name     | Required | Description                                              |
|----------|----------|------------------------------------------------------------|
| `format` | no       | Response format (`json`\|`yaml`\|`xml`), default `json`. |

**Success response `200`:**

| Field    | Description        |
|----------|---------------------|
| `width`  | Board width, in cells.  |
| `height` | Board height, in cells. |

**Errors:**

| Status | Condition                                                     |
|--------|------------------------------------------------------------------|
| `401`  | Missing/invalid/expired `Authorization` bearer token.             |
| `403`  | No board is bound to the authenticated client.                    |

---

### `GET /board/bombs`

Returns the number of bombs (mines) remaining to be flagged and the total number of
bombs on the board.

**Headers:** `Authorization: Bearer <token>` (required)

**Query parameters:**

| Name     | Required | Description                                              |
|----------|----------|-------------------------------------------------------------|
| `format` | no       | Response format (`json`\|`yaml`\|`xml`), default `json`. |

**Success response `200`:**

| Field   | Description                                          |
|---------|----------------------------------------------------------|
| `bombs` | Number of bombs left, i.e. `total - flags_placed`.        |
| `total` | Total number of bombs on the board.                        |

**Errors:** same as `GET /board/size`.

---

### `GET /board/fully_revealed`

Reports whether the board is in a "fully revealed" state — every cell is revealed
except for exactly the mines, all of which are flagged.

**Headers:** `Authorization: Bearer <token>` (required)

**Query parameters:**

| Name     | Required | Description                                              |
|----------|----------|-------------------------------------------------------------|
| `format` | no       | Response format (`json`\|`yaml`\|`xml`), default `json`. |

**Success response `200`:**

| Field            | Description                                                                 |
|------------------|--------------------------------------------------------------------------------|
| `fully_revealed` | `true` iff `unrevealed_count == bombs_total` **and** `flags_count == bombs_total`. |

**Errors:** same as `GET /board/size`.

---

### `POST /board/check`

Checks the end-of-game condition once the board is fully revealed (see above), and
reports whether the client won or lost. Mines are never revealed by normal play (only
flagged) — the only way a mine gets its "revealed" bit set is by booming it, via either
`cell/reveal` or `cell/check`. A win means none of the mines were boomed and all of them
are flagged.

**Headers:** `Authorization: Bearer <token>` (required)

**Query parameters:**

| Name     | Required | Description                                              |
|----------|----------|-------------------------------------------------------------|
| `format` | no       | Response format (`json`\|`yaml`\|`xml`), default `json`. |

**Success response `200`:**

| Field    | Description            |
|----------|--------------------------|
| `status` | `"win"` or `"lose"`.      |

**Errors:**

| Status | Condition                                                                    |
|--------|----------------------------------------------------------------------------------|
| `401`  | Missing/invalid/expired `Authorization` bearer token.                             |
| `403`  | No board is bound to the authenticated client.                                    |
| `400`  | Board isn't fully revealed yet (`"can't check field when unrevealed cells are left"`). |

---

### `POST /cell/reveal`

Reveals a cell on the board. If the revealed cell has 0 mines among its 8 neighbors,
this automatically (recursively) reveals all of its neighbors too, and so on for any of
those neighbors that also turn out to have 0 neighboring mines. This auto-reveal
flood-fill can never boom a mine: a cell with a 0 neighbor-mine count means, by
definition, that none of its 8 neighbors are mines, so every cell the flood-fill expands
into through such a cell is guaranteed safe. The only way this endpoint reports `"boom"`
is for the single originally-requested cell, if it was a mine itself.

**Headers:** `Authorization: Bearer <token>` (required)

**Query parameters:**

| Name     | Required | Description                                              |
|----------|----------|-------------------------------------------------------------|
| `x`      | yes      | Column, 1-based.                                              |
| `y`      | yes      | Row, 1-based.                                                 |
| `format` | no       | Response format (`json`\|`yaml`\|`xml`), default `json`. |

**Success response `200`:**

The response always contains a `cells` array — even when only a single cell got
revealed (a plain reveal or a boom) — so the response shape is uniform regardless of
whether the auto-reveal flood-fill triggered or not:

| Field    | Description                                                                          |
|----------|-----------------------------------------------------------------------------------------|
| `status` | `"ok"`, or `"boom"` if the revealed cell contained a mine (game over).                  |
| `cells`  | Array of one or more `{"x": ..., "y": ..., "count": ..., "bomb": ...}` objects. `count` is the number of mines among that cell's neighbors (`-1` if the cell itself was a mine); `bomb` is `true` iff `count < 0`. For `"boom"`, the array has exactly one element and it has no `count`/`bomb` fields. For `"ok"`, the array has one element for a plain reveal (1+ neighboring mines, or the cell was already revealed), or multiple elements — including the originally requested cell — when the auto-reveal flood-fill opened several connected zero-count cells. |

If the cell was already revealed before this call, an additional field
`"info": "already revealed"` is included in the response (the cell is revealed again,
which is a no-op).

Examples (`format=json`):
```json
{"status": "boom", "cells": [{"x": 3, "y": 4}]}
```
```json
{"status": "ok", "cells": [{"x": 3, "y": 4, "count": 2, "bomb": false}]}
```
```json
{
  "status": "ok",
  "cells": [
    {"x": 3, "y": 4, "count": 0, "bomb": false},
    {"x": 4, "y": 4, "count": 1, "bomb": false},
    {"x": 3, "y": 5, "count": 1, "bomb": false}
  ]
}
```

**Errors:**

| Status | Condition                                                    |
|--------|-------------------------------------------------------------------|
| `401`  | Missing/invalid/expired `Authorization` bearer token.               |
| `403`  | No board is bound to the authenticated client.                      |
| `400`  | `x` or `y` parameter is missing (`"missing mandatory parameter x or y"`). |
| `400`  | The target cell is flagged (`"can't reveal flagged cell"`).         |
| `400`  | Coordinates are out of range for the board.                          |

---

### `POST /cell/flag`

Toggles a flag on a cell (marks/unmarks it as a suspected mine).

**Headers:** `Authorization: Bearer <token>` (required)

**Query parameters:**

| Name     | Required | Description                                              |
|----------|----------|-------------------------------------------------------------|
| `x`      | yes      | Column, 1-based.                                              |
| `y`      | yes      | Row, 1-based.                                                 |
| `format` | no       | Response format (`json`\|`yaml`\|`xml`), default `json`. |

**Success response `200`:**

| Field     | Description                            |
|-----------|-------------------------------------------|
| `status`  | `"ok"`.                                    |
| `x`       | Column of the cell.                        |
| `y`       | Row of the cell.                           |
| `flagged` | New flag state of the cell (`true`/`false`). |

**Errors:**

| Status | Condition                                                                              |
|--------|--------------------------------------------------------------------------------------------|
| `401`  | Missing/invalid/expired `Authorization` bearer token.                                       |
| `400`  | `x` or `y` parameter is missing.                                                             |
| `403`  | No board is bound to the authenticated client.                                              |
| `400`  | `x` parameter is not a valid number.                                                         |
| `400`  | `y` parameter is not a valid number.                                                         |
| `400`  | Coordinates are out of range for the board.                                                   |
| `400`  | The target cell is already revealed (`"can't flag revealed cell"`).                          |
| `400`  | No bombs are left to flag and the cell is not already flagged (`"can't flag cell when no bombs left"`). |

---

### `GET /boards/list` *(plugin: `boards_list`)*

Lists every board currently held by the server (not scoped to any client).

**Query parameters:**

| Name     | Required | Description                                              |
|----------|----------|-------------------------------------------------------------|
| `format` | no       | Response format (`json`\|`yaml`\|`xml`), default `json`. |

**Success response `200`:**

| Field    | Description                                                                 |
|----------|----------------------------------------------------------------------------------|
| `boards` | Array of `{"board_id": ..., "width": ..., "height": ...}` objects, one per board. |

No authentication required.

---

### `POST /cell/check` *(plugin: `cell_check`)*

"Chord"/middle-click-style helper: given an already-revealed, non-mine cell whose
number of flagged neighbors matches its neighbor-mine count, reveals all of its
remaining (unrevealed, unflagged) neighbors, running the same auto-reveal flood-fill
`cell/reveal` uses on each one. Unlike `cell/reveal`, this **can** boom: each qualifying
neighbor is fed into the flood-fill directly, without first confirming it isn't a mine
itself (`cell/reveal` only ever seeds the flood-fill with a cell already confirmed safe).
So if the flag count happens to match the mine count while the flags are on the wrong
cells, revealing the neighbors can hit a real, unflagged mine — matching the classic
minesweeper chording risk.

**Headers:** `Authorization: Bearer <token>` (required)

**Query parameters:**

| Name     | Required | Description                                              |
|----------|----------|-------------------------------------------------------------|
| `x`      | yes      | Column, 1-based, of the already-revealed cell.                |
| `y`      | yes      | Row, 1-based, of the already-revealed cell.                    |
| `format` | no       | Response format (`json`\|`yaml`\|`xml`), default `json`. |

**Success response `200`:**

| Field    | Description                                                                        |
|----------|------------------------------------------------------------------------------------------|
| `status` | `"ok"`, or `"boom"` if any newly revealed cell contained a mine.                          |
| `cells`  | Array of `{"x": ..., "y": ..., "count": ..., "bomb": ...}` objects for every newly revealed cell. Empty if all neighbors were already revealed or flagged. |

**Errors:**

| Status | Condition                                                                    |
|--------|------------------------------------------------------------------------------------|
| `401`  | Missing/invalid/expired `Authorization` bearer token.                                |
| `403`  | No board is bound to the authenticated client (`"Client not found"`).                |
| `400`  | `x` or `y` parameter is missing (`"Missing required query parameters: x, y"`).        |
| `400`  | Coordinates are out of range (`"Invalid coordinates"`).                              |
| `400`  | The cell isn't revealed yet (`"Cell is not revealed"`).                              |
| `400`  | The cell is a mine (`"Cell is a bomb"`).                                             |
| `400`  | Flagged-neighbor count doesn't match mine-neighbor count (`"Number of flags around cell does not match number of bombs"`). |

Note: this plugin's error messages are capitalized, unlike the lowercase style used by
the core endpoints above — a cosmetic inconsistency between the built-in handlers and
this particular plugin, not a documented convention to rely on.

---

## Typical flow

1. `POST /session/new[?board_id=<id>]` → obtain `token`; send it as
   `Authorization: Bearer <token>` on every following call.
2. `GET /board/size` → obtain board dimensions to render the board.
3. `GET /board/bombs` → display remaining bomb count.
4. `POST /cell/reveal?x=<x>&y=<y>` → reveal a cell; repeat until either `status` is
   `"boom"` (game over) or `GET /board/fully_revealed` reports `true`.
5. `POST /cell/flag?x=<x>&y=<y>` → mark/unmark suspected mines.
6. `POST /cell/check?x=<x>&y=<y>` → chord-reveal the neighbors of an already-satisfied,
   revealed cell (if the `cell_check` plugin is loaded).
7. `POST /board/check` → once fully revealed, find out whether the client won or lost.
