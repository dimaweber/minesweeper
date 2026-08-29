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
}// namespace

http_response_t http_client_t::perform (const std::string& url, bool is_post) const {
  http_response_t response;

  CURL* curl = curl_easy_init( );
  if ( !curl ) {
    SPDLOG_ERROR("Failed to initialize curl handle");
    return response;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str( ));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
  if ( is_post ) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
  }

  const CURLcode res = curl_easy_perform(curl);
  if ( res != CURLE_OK ) {
    SPDLOG_ERROR("HTTP request to {} failed: {}", url, curl_easy_strerror(res));
    curl_easy_cleanup(curl);
    return response;
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
  curl_easy_cleanup(curl);

  response.ok = response.status >= 200 && response.status < 300;
  return response;
}

http_response_t http_client_t::get (const std::string& path, const http_params_t& params) const {
  return perform(build_url(path, params), false);
}

http_response_t http_client_t::post (const std::string& path, const http_params_t& params) const {
  return perform(build_url(path, params), true);
}
