#include "http.hxx"

#include <curl/curl.h>
#include <fmt/format.h>

#include <inc/logger.hxx>

http_client_t::http_client_t (std::string host, uint16_t port) : host_ {std::move(host)}, port_ {port} {
}

std::string http_client_t::build_url (const std::string& path, const http_params_t& params) const {
  std::string url = fmt::format("http://{}:{}{}", host_, port_, path);
  if ( params.empty( ) )
    return url;

  CURL* curl = curl_easy_init( );
  url += "?";
  bool first = true;
  for ( const auto& [key, value]: params ) {
    if ( !first )
      url += "&";
    first = false;

    char* escaped_key   = curl_easy_escape(curl, key.c_str( ), static_cast<int>(key.size( )));
    char* escaped_value = curl_easy_escape(curl, value.c_str( ), static_cast<int>(value.size( )));
    url += fmt::format("{}={}", escaped_key, escaped_value);
    curl_free(escaped_key);
    curl_free(escaped_value);
  }
  curl_easy_cleanup(curl);
  return url;
}

namespace {
size_t write_callback (char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* body = static_cast<std::string*>(userdata);
  body->append(ptr, size * nmemb);
  return size * nmemb;
}
}  // namespace

struct curl_slist_t {
  curl_slist* list {nullptr};

  curl_slist_t ( ) {
  }

  ~curl_slist_t ( ) {
    if ( list )
      curl_slist_free_all(list);
  }

  void append (const char* header) {
    list = curl_slist_append(list, header);
  }

  operator curl_slist*( ) {
    return list;
  }
};

struct curl_ease_t {
  CURL* curl {nullptr};

  curl_ease_t ( ) noexcept {
    curl = curl_easy_init( );
  }

  ~curl_ease_t ( ) {
    if ( curl )
      curl_easy_cleanup(curl);
  }

  [[nodiscard]] operator bool ( ) const noexcept{
    return curl != nullptr;
  }

  auto setopt (CURLoption option, const auto value) noexcept {
    return curl_easy_setopt(curl, option, value);
  }

  auto perform ( ) const noexcept {
    return curl_easy_perform(curl);
  }

  auto getinfo (CURLINFO info, auto* value) noexcept {
    return curl_easy_getinfo(curl, info, value);
  }

};

http_response_t http_client_t::perform (const std::string& url, bool is_post) const {
  http_response_t response;

  curl_ease_t curl;
  if ( !curl ) {
    SPDLOG_ERROR("Failed to initialize curl handle");
    return response;
  }

  curl.setopt(CURLOPT_URL, url.c_str( ));
  curl.setopt(CURLOPT_WRITEFUNCTION, write_callback);
  curl.setopt(CURLOPT_WRITEDATA, &response.body);
  curl.setopt(CURLOPT_TIMEOUT, 5L);
  if ( is_post ) {
    curl.setopt( CURLOPT_POST, 1L);
    curl.setopt( CURLOPT_POSTFIELDSIZE, 0L);
  }
  curl_slist_t headers;
  headers.append(fmt::format("Authorization: Bearer {}", jwt_token_).c_str(  ));
  curl.setopt( CURLOPT_HTTPHEADER, (curl_slist*)headers);

  const CURLcode res = curl.perform( );
  if ( res != CURLE_OK ) {
    SPDLOG_ERROR("HTTP request to {} failed: {}", url, curl_easy_strerror(res));
    return response;
  }

  curl.getinfo(CURLINFO_RESPONSE_CODE, &response.status);

  response.ok = response.status >= 200 && response.status < 300;
  return response;
}

http_response_t http_client_t::get (const std::string& path, const http_params_t& params) const {
  return perform(build_url(path, params), false);
}

http_response_t http_client_t::post (const std::string& path, const http_params_t& params) const {
  return perform(build_url(path, params), true);
}
