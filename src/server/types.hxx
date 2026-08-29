#pragma once

enum class http_header_t { content_type, content_length };

template<typename T = std::string_view>
[[nodiscard]] constexpr T to_string (http_header_t header, T defValue = T { }) noexcept {
  switch ( header ) {
    case http_header_t::content_type:   return "Content-Type";
    case http_header_t::content_length: return "Content-Length";
  }
  return defValue;
}

enum class content_type_t { json, yaml, xml };

template<typename T = std::string_view>
[[nodiscard]] constexpr T to_string (content_type_t type, T defValue = T { }) noexcept {
  switch ( type ) {
    case content_type_t::json: return "application/json";
    case content_type_t::yaml: return "application/x-yaml";
    case content_type_t::xml:  return "application/xml";
  }
  return defValue;
}

[[nodiscard]] constexpr content_type_t to_content_type (std::string_view type, content_type_t defValue = content_type_t::json) noexcept {
  if ( type == "application/json" || type == "json" )
    return content_type_t::json;
  if ( type == "application/x-yaml" || type == "yaml" )
    return content_type_t::yaml;
  if ( type == "application/xml" || type == "xml" )
    return content_type_t::xml;
  return defValue;
}

enum class http_methods_t {
  GET,
  POST,
  DELETE,
  PUT,
};

template<typename T = std::string_view>
[[nodiscard]] constexpr T to_string (http_methods_t method, T defValue = T { }) noexcept {
  switch ( method ) {
    case http_methods_t::GET:    return "GET";
    case http_methods_t::POST:   return "POST";
    case http_methods_t::DELETE: return "DELETE";
    case http_methods_t::PUT:    return "PUT";
  }
  return defValue;
}
