#include "handlers.hxx"

#include <corvusoft/restbed/request.hpp>
#include <deque>
#include <set>
#include <tuple>
#include <wbr/string_manipulations.hxx>

extern addon_api_t api;

void field_new_handler (SessionPtr session) {
  const auto        request      = session->get_request( );
  const std::string format_str   = request->get_query_parameter("format", "json");
  const std::string field_id_str = request->get_query_parameter("field_id", "");

  const content_type_t content_type = to_content_type(format_str);
  field_id_t           field_id {0};

  response_t r(session);

  if ( !field_id_str.empty( ) ) {
    std::errc ec;
    field_id = wbr::str::num<field_id_t, wbr::str::num_match_t::full>(field_id_str, ec);
    if ( ec != std::errc { } ) {
      return r.send_error(400, content_type, "invalid field_id parameter");
    }
  } else {
    field_id = rand( ) % api.fields.size( );
  }

  const std::optional<client_id_t> id = api.add_new_client(field_id);
  if ( !id ) {
    return r.send_error(500, content_type, "failed to create new client");
  }

  SPDLOG_DEBUG("Created new client with id {}", *id);

  r.add_field("client_id", *id);
  return r.send(200, content_type);
}

void field_size_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string id_str  = request->get_query_parameter("id", "");
  const std::string format  = request->get_query_parameter("format", "json");

  const content_type_t content_type = to_content_type(format);

  response_t r {session};

  if ( id_str.empty( ) ) {
    return r.send_error(400, content_type, "missing mandatory parameter id");
  }

  std::errc         ec;
  const client_id_t id = wbr::str::num<client_id_t, wbr::str::num_match_t::full>(id_str, ec);
  if ( ec != std::errc { } ) {
    return r.send_error(400, content_type, "invalid parameter id");
  }

  field_t* field = api.field_for_client(id);
  if ( !field ) {
    return r.send_error(400, content_type, "client not found");
  }

  SPDLOG_DEBUG("Size request for client {}: {}x{}", id, field->w_, field->h_);

  r.add_field("width", field->w_).add_field("height", field->h_);
  return r.send(200, content_type);
}

void field_bombs_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string id_str  = request->get_query_parameter("id", "");
  const std::string format  = request->get_query_parameter("format", "json");

  const content_type_t content_type = to_content_type(format);

  response_t r {session};

  if ( id_str.empty( ) ) {
    return r.send_error(400, content_type, "missing mandatory parameter id");
  }

  std::errc         ec;
  const client_id_t id = wbr::str::num<client_id_t, wbr::str::num_match_t::full>(id_str, ec);
  if ( ec != std::errc { } ) {
    return r.send_error(400, content_type, "invalid parameter id");
  }

  field_t* field = api.field_for_client(id);
  if ( !field ) {
    return r.send_error(400, content_type, "client not found");
  }

  SPDLOG_DEBUG("Bombs request for client {}: {}/{} bombs", id, field->bombs_count( ), field->bombs_total( ));

  r.add_field("bombs", field->bombs_count( )).add_field("total", field->bombs_total( ));
  return r.send(200, content_type);
}

void field_fully_revealed_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string id_str  = request->get_query_parameter("id", "");
  const std::string format  = request->get_query_parameter("format", "json");

  const content_type_t content_type = to_content_type(format);

  response_t r {session};

  if ( id_str.empty( ) ) {
    return r.send_error(400, content_type, "missing mandatory parameter id");
  }

  std::errc         ec;
  const client_id_t id = wbr::str::num<client_id_t, wbr::str::num_match_t::full>(id_str, ec);
  if ( ec != std::errc { } ) {
    return r.send_error(400, content_type, "invalid parameter id");
  }

  field_t* field = api.field_for_client(id);
  if ( !field ) {
    return r.send_error(400, content_type, "client not found");
  }

  SPDLOG_DEBUG("Fully revealed request for client {}: {} unrevealed cells", id, field->unrevealed_count( ));

  r.add_field("fully_revealed", field->unrevealed_count( ) == field->bombs_total( ));
  return r.send(200, content_type);
}

void action_reveal_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string id_str  = request->get_query_parameter("id", "");
  const std::string x_str   = request->get_query_parameter("x", "");
  const std::string y_str   = request->get_query_parameter("y", "");
  const std::string format  = request->get_query_parameter("format", "json");

  const auto content_type = to_content_type(format);

  response_t r {session};

  if ( id_str.empty( ) || x_str.empty( ) || y_str.empty( ) ) {
    return r.send_error(400, content_type, "missing mandatory parameter id, x or y");
  }

  std::errc         ec;
  const client_id_t id = wbr::str::num<client_id_t, wbr::str::num_match_t::full>(id_str, ec);
  if ( ec != std::errc { } ) {
    return r.send_error(400, content_type, "invalid parameter id");
  }

  field_t* ptr = api.field_for_client(id);

  if ( ptr == nullptr ) {
    return r.send_error(404, content_type, "client not found");
  }

  field_t& field = *ptr;

  const auto x = wbr::str::num<int, wbr::str::num_match_t::full>(x_str, ec);
  if ( ec != std::errc { } ) {
    return r.send_error(400, content_type, "invalid parameter x");
  }

  const auto y = wbr::str::num<int, wbr::str::num_match_t::full>(y_str, ec);
  if ( ec != std::errc { } ) {
    return r.send_error(400, content_type, "invalid parameter y");
  }

  // Counts the bombs among the 8 cells surrounding (cx, cy).
  const auto count_neighbor_bombs = [&field] (int cx, int cy) {
    const std::initializer_list<std::pair<int, int>> neighbors = {
        {cx - 1, cy - 1},
        {cx,     cy - 1},
        {cx + 1, cy - 1},
        {cx - 1, cy    },
        {cx + 1, cy    },
        {cx - 1, cy + 1},
        {cx,     cy + 1},
        {cx + 1, cy + 1}
    };
    return std::ranges::count_if(neighbors, [&field] (const auto& p) { return field.is_boom(p); });
  };

  try {
    if ( field.is_flag(x, y) ) {
      return r.send_error(400, content_type, "can't reveal flagged cell");
    }
    if ( field.is_revealed(x, y) ) {
      r.add_field("info", "already revealed");
    }
    field.reveal(x, y);
    if ( field.is_boom(x, y) ) {
      // boom -- still returned as a (single-element) cells array, for API uniformity.
      parameter_map_t cell;
      cell.emplace("x", x);
      cell.emplace("y", y);
      r.add_field("status", "boom").add_field("cells", parameter_list_t {cell});
      return r.send(200, content_type);
    }

    // Auto-reveal: opening a cell with 0 neighbouring mines recursively opens all
    // of its neighbours (and, transitively, their neighbours), but this flood-fill
    // can never open a mine. This is implemented here at the handler level (rather
    // than inside field_t) so field_t keeps a single simple reveal() primitive, and
    // a future "plain" (non-auto) reveal endpoint could reuse it unchanged.
    std::vector<std::tuple<int, int, uint>> revealed;
    std::set<std::pair<int, int>>           visited;

    const uint count = count_neighbor_bombs(x, y);
    revealed.emplace_back(x, y, count);
    visited.emplace(x, y);

    if ( count == 0 ) {
      std::deque<std::pair<int, int>> queue;
      queue.emplace_back(x, y);

      while ( !queue.empty( ) ) {
        const auto [cx, cy] = queue.front( );
        queue.pop_front( );

        const std::initializer_list<std::pair<int, int>> neighbors = {
            {cx - 1, cy - 1},
            {cx,     cy - 1},
            {cx + 1, cy - 1},
            {cx - 1, cy    },
            {cx + 1, cy    },
            {cx - 1, cy + 1},
            {cx,     cy + 1},
            {cx + 1, cy + 1}
        };

        for ( const auto& [nx, ny]: neighbors ) {
          if ( nx < 1 || nx > static_cast<int>(field.w_) || ny < 1 || ny > static_cast<int>(field.h_) )
            continue;
          if ( !visited.emplace(nx, ny).second )
            continue;
          if ( field.is_flag(nx, ny) || field.is_revealed(nx, ny) || field.is_boom(nx, ny) )
            continue;

          field.reveal(nx, ny);
          const uint n_count = count_neighbor_bombs(nx, ny);
          revealed.emplace_back(nx, ny, n_count);
          if ( n_count == 0 )
            queue.emplace_back(nx, ny);
        }
      }
    }

    // Always return an array of cells, even when only one cell got revealed --
    // this keeps the response shape uniform (and simplifies both server and
    // client code), instead of special-casing the single-cell result.
    parameter_list_t cells;
    cells.reserve(revealed.size( ));
    for ( const auto& [rx, ry, rc]: revealed ) {
      parameter_map_t cell;
      cell.emplace("x", rx);
      cell.emplace("y", ry);
      cell.emplace("count", rc);
      cells.emplace_back(cell);
    }
    r.add_field("status", "ok").add_field("cells", cells);
    return r.send(200, content_type);
  } catch ( std::out_of_range& e ) {
    return r.send_error(400, content_type, "coordinates out of range");
  }
}

void action_flag_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string id_str  = request->get_query_parameter("id", "");
  const std::string x_str   = request->get_query_parameter("x", "");
  const std::string y_str   = request->get_query_parameter("y", "");
  const std::string format  = request->get_query_parameter("format", "json");

  const auto content_type = to_content_type(format);

  response_t r {session};

  if ( id_str.empty( ) || x_str.empty( ) || y_str.empty( ) ) {
    return r.send_error(400, content_type, "missing mandatory parameter id, x or y");
  }

  std::errc         ec;
  const client_id_t id = wbr::str::num<client_id_t, wbr::str::num_match_t::full>(id_str, ec);
  if ( ec != std::errc { } ) {
    return r.send_error(400, content_type, "invalid parameter id");
  }

  field_t* ptr = api.field_for_client(id);

  if ( ptr == nullptr ) {
    return r.send_error(404, content_type, "client not found");
  }

  field_t& field = *ptr;

  const auto x = wbr::str::num<int, wbr::str::num_match_t::full>(x_str, ec);
  if ( ec != std::errc { } ) {
    return r.send_error(400, content_type, "invalid parameter x");
  }

  const auto y = wbr::str::num<int, wbr::str::num_match_t::full>(y_str, ec);
  if ( ec != std::errc { } ) {
    return r.send_error(400, content_type, "invalid parameter y");
  }

  if ( field.is_revealed(x, y) ) {
    return r.send_error(400, content_type, "can't flag revealed cell");
  }

  if ( !field.is_flag(x, y) && field.bombs_count( ) == 0 ) {
    return r.send_error(400, content_type, "can't flag cell when no bombs left");
  }

  field.toggle_flag(x, y);
  r.add_field("status", "ok").add_field("x", x).add_field("y", y).add_field("flagged", field.is_flag(x, y));
  return r.send(200, content_type);
}

void action_check_handler (SessionPtr session) {
  const auto        request = session->get_request( );
  const std::string id_str  = request->get_query_parameter("id", "");
  const std::string format  = request->get_query_parameter("format", "json");

  const auto content_type = to_content_type(format);

  response_t r {session};

  if ( id_str.empty( ) ) {
    return r.send_error(400, content_type, "missing mandatory parameter id");
  }

  std::errc         ec;
  const client_id_t id = wbr::str::num<client_id_t, wbr::str::num_match_t::full>(id_str, ec);
  if ( ec != std::errc { } ) {
    return r.send_error(400, content_type, "invalid parameter id");
  }

  field_t* ptr = api.field_for_client(id);

  if ( ptr == nullptr ) {
    return r.send_error(404, content_type, "client not found");
  }

  field_t& field = *ptr;

  // Mines are never revealed by normal play (only flagged) -- the only way a mine
  // gets its "revealed" bit set is via a boom. So "fully revealed" (matching
  // field_fully_revealed_handler's own definition) means exactly bombs_total()
  // cells remain unrevealed, not zero.
  if ( field.unrevealed_count( ) != field.bombs_total( ) ) {
    return r.send_error(400, content_type, "can't check field when unrevealed cells are left");
  }
  // A cell is "bad" (loses the game) if it's a mine that either wasn't flagged
  // or got revealed (boomed). A win means none of the mines are bad.
  const bool ok = std::ranges::none_of(field.data_, [] (u_short cell) {
    return field_t::is_boom(cell) && ( !field_t::is_flag(cell) || field_t::is_revealed(cell) );
  });
  if ( ok )
    r.add_field("status", "win");
  else
    r.add_field("status", "lose");
  return r.send(200, content_type);
}
