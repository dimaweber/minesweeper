# Minesweeper Server API

This document describes the HTTP API exposed by the `server` binary
(`src/server/server.cxx`, `src/server/handlers.cxx`, `src/server/api.hxx`).

The server is built on top of [restbed](https://github.com/Corvusoft/restbed) and exposes a
small set of resources for creating a game session and interacting with a minesweeper field.

## General conventions

### Response format

Every response body is a flat key/value map. The representation is controlled by the
`format` query parameter, which is accepted by **every** endpoint:

| `format` value                    | `Content-Type`          |
|------------------------------------|--------------------------|
| `json` (default, or any unrecognized value) | `application/json`       |
| `yaml`                              | `application/x-yaml`     |
| `xml`                               | `application/xml`        |

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

Values are serialized using their native type (numbers as numbers, booleans as
booleans, strings as strings).

A field value can also be an **array of values of the same kind**, or a **map of named
values of the same kind**, and both can be nested arbitrarily (arrays of arrays, maps of
maps, arrays containing maps, maps containing arrays, etc.), recursively. This is not
used by any endpoint today, but the underlying `parameter_t`/`parameter_serializer_t`
(see `src/server/api.hxx`) support it for future API extensions. Arrays are serialized
as a JSON/YAML array, or in XML as a sequence of `<item>` elements (nested arrays produce
nested `<item>` elements); maps are serialized as a JSON/YAML object/map, or in XML as
one child element per map key (nested maps produce nested child elements), e.g.:
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

### Coordinates

Cell coordinates `x` and `y` are **1-based**. Valid values are `1 <= x <= width` and
`1 <= y <= height` (see `field_t::coord_to_index`). Out-of-range coordinates result in a
`400` error (`"coordinates out of range"`), for the reveal action, coordinate bounds are
checked while dereferencing the field.

### Session / client model

A "client" represents a single player session bound to one field (board). A client is
created via `POST /field/new` and is identified afterwards by the `id` (`client_id`)
returned in that response. This `id` must be passed as the `id` query parameter to all
other endpoints.

Multiple clients can be bound to the same `field_id`, but each client gets its own
**independent copy** of that field's initial layout (see `addon_api_t::add_new_client`,
which copies `fields.at(field_id)` into the new `client_context_t`). This means several
players can start a session from the same field template (same size and mine layout),
but they are *not* playing on a shared board: actions performed by one client (revealing
or flagging cells) only affect that client's own copy and are invisible to, and unaffected
by, other clients bound to the same `field_id`.

---

## Endpoints

### `POST /field/new`

Creates a new client (game session) bound to a field.

**Handler:** `field_new_handler`

**Query parameters:**

| Name       | Required | Description                                                                                   |
|------------|----------|-----------------------------------------------------------------------------------------------|
| `field_id` | no       | Id of an existing field to attach the new client to. If omitted, a random field is picked from the available fields. |
| `format`   | no       | Response format (`json`\|`yaml`\|`xml`), default `json`.                                             |

**Success response `200`:**

| Field       | Description                          |
|-------------|---------------------------------------|
| `client_id` | Id of the newly created client.       |

**Errors:**

| Status | Condition                                          |
|--------|-----------------------------------------------------|
| `400`  | `field_id` parameter is not a valid number.         |
| `500`  | Failed to create a new client (e.g. unknown `field_id`). |

---

### `GET /field/size`

Returns the dimensions of the field associated with a client.

**Handler:** `field_size_handler`

**Query parameters:**

| Name     | Required | Description                          |
|----------|----------|---------------------------------------|
| `id`     | yes      | Client id, obtained from `/field/new`.|
| `format` | no       | Response format (`json`\|`yaml`\|`xml`), default `json`. |

**Success response `200`:**

| Field    | Description        |
|----------|---------------------|
| `width`  | Field width, in cells.  |
| `height` | Field height, in cells. |

**Errors:**

| Status | Condition                              |
|--------|------------------------------------------|
| `400`  | `id` parameter is missing.               |
| `400`  | `id` parameter is not a valid number.    |
| `400`  | No client found for the given `id`.      |

---

### `GET /field/bombs`

Returns the number of bombs (mines) remaining to be flagged and the total number of
bombs on the field.

**Handler:** `field_bombs_handler`

**Query parameters:**

| Name     | Required | Description                          |
|----------|----------|---------------------------------------|
| `id`     | yes      | Client id, obtained from `/field/new`.|
| `format` | no       | Response format (`json`\|`yaml`\|`xml`), default `json`. |

**Success response `200`:**

| Field   | Description                                            |
|---------|----------------------------------------------------------|
| `bombs` | Number of bombs left, i.e. `total - flags_placed`.       |
| `total` | Total number of bombs on the field.                       |

**Errors:**

| Status | Condition                              |
|--------|------------------------------------------|
| `400`  | `id` parameter is missing.               |
| `400`  | `id` parameter is not a valid number.    |
| `400`  | No client found for the given `id`.      |

---

### `POST /action/reveal`

Reveals a cell on the field. If the revealed cell has 0 mines among its 8 neighbors,
this automatically (recursively) reveals all of its neighbors too, and so on for any
of those neighbors that also turn out to have 0 neighboring mines. This auto-reveal
flood-fill is implemented at the handler level (not inside the field storage class
itself), and it will never automatically reveal a cell that contains a mine.

**Handler:** `action_reveal_handler`

**Query parameters:**

| Name     | Required | Description                                        |
|----------|----------|------------------------------------------------------|
| `id`     | yes      | Client id.                                            |
| `x`      | yes      | Column, 1-based.                                       |
| `y`      | yes      | Row, 1-based.                                          |
| `format` | no       | Response format (`json`\|`yaml`\|`xml`), default `json`.      |

**Success response `200`:**

The response always contains a `cells` array — even when only a single cell got
revealed (a plain reveal or a boom) — so the response shape is uniform regardless of
whether the auto-reveal flood-fill triggered or not:

| Field    | Description                                                                          |
|----------|-----------------------------------------------------------------------------------------|
| `status` | `"ok"`, or `"boom"` if the revealed cell contained a mine (game over).                  |
| `cells`  | Array of one or more `{"x": ..., "y": ..., "count": ...}` objects. For `"boom"`, the array always has exactly one element and it has no `count` field. For `"ok"`, the array has one element for a plain reveal (1+ neighboring mines, or the cell was already revealed), or multiple elements — including the originally requested cell — when the auto-reveal flood-fill opened several connected zero-count cells. |

If the cell was already revealed before this call, an additional field
`"info": "already revealed"` is included in the response (the cell is revealed again,
which is a no-op).

Examples (`format=json`):
```json
{"status": "boom", "cells": [{"x": 3, "y": 4}]}
```
```json
{"status": "ok", "cells": [{"x": 3, "y": 4, "count": 2}]}
```
```json
{
  "status": "ok",
  "cells": [
    {"x": 3, "y": 4, "count": 0},
    {"x": 4, "y": 4, "count": 1},
    {"x": 3, "y": 5, "count": 1}
  ]
}
```

**Errors:**

| Status | Condition                                              |
|--------|----------------------------------------------------------|
| `400`  | `id`, `x` or `y` parameter is missing.                    |
| `400`  | `id` parameter is not a valid number.                     |
| `404`  | No client found for the given `id`.                       |
| `400`  | `x` parameter is not a valid number.                      |
| `400`  | `y` parameter is not a valid number.                      |
| `400`  | The target cell is flagged (`"can't reveal flagged cell"`).|
| `400`  | Coordinates are out of range for the field.                |

---

### `POST /action/flag`

Toggles a flag on a cell (marks/unmarks it as a suspected mine).

**Handler:** `action_flag_handler`

**Query parameters:**

| Name     | Required | Description                                        |
|----------|----------|------------------------------------------------------|
| `id`     | yes      | Client id.                                            |
| `x`      | yes      | Column, 1-based.                                       |
| `y`      | yes      | Row, 1-based.                                          |
| `format` | no       | Response format (`json`\|`yaml`\|`xml`), default `json`.      |

**Success response `200`:**

| Field     | Description                                    |
|-----------|--------------------------------------------------|
| `status`  | `"ok"`.                                          |
| `x`       | Column of the cell.                              |
| `y`       | Row of the cell.                                 |
| `flagged` | New flag state of the cell (`"1"`/`"0"`, since booleans are serialized via `std::to_string`). |

**Errors:**

| Status | Condition                                                                    |
|--------|--------------------------------------------------------------------------------|
| `400`  | `id`, `x` or `y` parameter is missing.                                          |
| `400`  | `id` parameter is not a valid number.                                            |
| `404`  | No client found for the given `id`.                                             |
| `400`  | `x` parameter is not a valid number.                                            |
| `400`  | `y` parameter is not a valid number.                                            |
| `400`  | The target cell is already revealed (`"can't flag revealed cell"`).            |
| `400`  | No bombs are left to flag and the cell is not already flagged (`"can't flag cell when no bombs left"`). |

---

## Typical flow

1. `POST /field/new[?field_id=<id>]` → obtain `client_id`.
2. `GET /field/size?id=<client_id>` → obtain field dimensions to render the board.
3. `GET /field/bombs?id=<client_id>` → display remaining bomb count.
4. `POST /action/reveal?id=<client_id>&x=<x>&y=<y>` → reveal a cell; repeat until either
   `status` is `"boom"` (game over) or all safe cells have been revealed.
5. `POST /action/flag?id=<client_id>&x=<x>&y=<y>` → mark/unmark suspected mines.
