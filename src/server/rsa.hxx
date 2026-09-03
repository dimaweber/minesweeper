#pragma once

#include <string>
#include <utility>

std::pair<std::string, std::string> rsa_key_pair(const std::filesystem::path rsa_priv_key_path, const std::filesystem::path rsa_pub_key_path);
bool                                create_self_signed_ssl_cert(const std::filesystem::path ssl_cert_path, const std::filesystem::path ssl_dh_path, const std::filesystem::path rsa_priv_key_path);
