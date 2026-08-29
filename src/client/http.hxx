#pragma once

#include <cstdint>
#include <string>
#include <vector>

using http_params_t = std::vector<std::pair<std::string, std::string>>;

struct http_response_t {
  long        status {0};
  std::string body;
  bool        ok {false};
};

class http_client_t {
public:
  http_client_t (std::string host, uint16_t port);

  [[nodiscard]] http_response_t get (const std::string& path, const http_params_t& params = {}) const;
  [[nodiscard]] http_response_t post (const std::string& path, const http_params_t& params = {}) const;

private:
  std::string host_;
  uint16_t    port_;

  [[nodiscard]] std::string build_url (const std::string& path, const http_params_t& params) const;
  [[nodiscard]] http_response_t perform (const std::string& url, bool is_post) const;
};
