#include "api.hxx"

#include <nlohmann/json.hpp>

#include <inc/logger.hxx>

client_api_t::client_api_t (std::string host, uint16_t port) : http_ {std::move(host), port} {
}

std::optional<client_id_t> client_api_t::field_new (std::optional<field_id_t> field_id) {
  http_params_t params;
  if ( field_id ) {
    params.emplace_back("field_id", std::to_string(*field_id));
  }

  const http_response_t response = http_.post("/field/new", params);
  if ( !response.ok ) {
    SPDLOG_ERROR("field/new failed: status {}, body {}", response.status, response.body);
    return std::nullopt;
  }

  const nlohmann::json json = nlohmann::json::parse(response.body, nullptr, false);
  if ( json.is_discarded( ) || !json.contains("client_id") ) {
    return std::nullopt;
  }
  return json.at("client_id").get<client_id_t>( );
}

std::optional<std::pair<std::size_t, std::size_t>> client_api_t::field_size (client_id_t id) {
  const http_response_t response = http_.get("/field/size", {{"id", std::to_string(id)}});
  if ( !response.ok ) {
    SPDLOG_ERROR("field/size failed: status {}, body {}", response.status, response.body);
    return std::nullopt;
  }

  const nlohmann::json json = nlohmann::json::parse(response.body, nullptr, false);
  if ( json.is_discarded( ) || !json.contains("width") || !json.contains("height") ) {
    return std::nullopt;
  }
  return std::make_pair(json.at("width").get<std::size_t>( ), json.at("height").get<std::size_t>( ));
}

bombs_result_t client_api_t::field_bombs (client_id_t id) {
  bombs_result_t         result;
  const http_response_t response = http_.get("/field/bombs", {{"id", std::to_string(id)}});
  if ( !response.ok ) {
    SPDLOG_ERROR("field/bombs failed: status {}, body {}", response.status, response.body);
    return result;
  }

  const nlohmann::json json = nlohmann::json::parse(response.body, nullptr, false);
  if ( json.is_discarded( ) || !json.contains("bombs") || !json.contains("total") ) {
    return result;
  }
  result.ok    = true;
  result.left  = json.at("bombs").get<int>( );
  result.total = json.at("total").get<int>( );
  return result;
}

reveal_result_t client_api_t::action_reveal (client_id_t id, int x, int y) {
  reveal_result_t result;
  const http_response_t response =
      http_.post("/action/reveal", {{"id", std::to_string(id)}, {"x", std::to_string(x)}, {"y", std::to_string(y)}});

  const nlohmann::json json = nlohmann::json::parse(response.body, nullptr, false);
  if ( !response.ok ) {
    result.error = ( !json.is_discarded( ) && json.contains("error") ) ? json.at("error").get<std::string>( ) : response.body;
    return result;
  }

  if ( json.is_discarded( ) || !json.contains("status") ) {
    result.error = "malformed response";
    return result;
  }

  result.ok   = true;
  result.boom = json.at("status").get<std::string>( ) == "boom";
  if ( json.contains("cells") ) {
    for ( const auto& c: json.at("cells") ) {
      revealed_cell_t cell;
      cell.x = c.at("x").get<int>( );
      cell.y = c.at("y").get<int>( );
      if ( !result.boom && c.contains("count") ) {
        cell.count = c.at("count").get<int>( );
      }
      result.cells.push_back(cell);
    }
  }
  return result;
}

flag_result_t client_api_t::action_flag (client_id_t id, int x, int y) {
  flag_result_t result;
  const http_response_t response =
      http_.post("/action/flag", {{"id", std::to_string(id)}, {"x", std::to_string(x)}, {"y", std::to_string(y)}});

  const nlohmann::json json = nlohmann::json::parse(response.body, nullptr, false);
  if ( !response.ok ) {
    result.error = ( !json.is_discarded( ) && json.contains("error") ) ? json.at("error").get<std::string>( ) : response.body;
    return result;
  }

  if ( json.is_discarded( ) || !json.contains("flagged") ) {
    result.error = "malformed response";
    return result;
  }

  result.ok      = true;
  result.flagged = json.at("flagged").get<bool>( );
  return result;
}

std::optional<bool> client_api_t::field_fully_revealed (client_id_t id) {
  const http_response_t response = http_.get("/field/fully_revealed", {{"id", std::to_string(id)}});
  if ( !response.ok ) {
    SPDLOG_ERROR("field/fully_revealed failed: status {}, body {}", response.status, response.body);
    return std::nullopt;
  }

  const nlohmann::json json = nlohmann::json::parse(response.body, nullptr, false);
  if ( json.is_discarded( ) || !json.contains("fully_revealed") ) {
    return std::nullopt;
  }
  return json.at("fully_revealed").get<bool>( );
}

check_result_t client_api_t::action_check (client_id_t id) {
  check_result_t result;
  const http_response_t response = http_.post("/action/check", {{"id", std::to_string(id)}});

  const nlohmann::json json = nlohmann::json::parse(response.body, nullptr, false);
  if ( !response.ok ) {
    result.error = ( !json.is_discarded( ) && json.contains("error") ) ? json.at("error").get<std::string>( ) : response.body;
    return result;
  }

  if ( json.is_discarded( ) || !json.contains("status") ) {
    result.error = "malformed response";
    return result;
  }

  result.ok  = true;
  result.win = json.at("status").get<std::string>( ) == "win";
  return result;
}
