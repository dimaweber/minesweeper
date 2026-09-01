#pragma once

#include <string>
#include <utility>

std::pair<std::string, std::string> rsa_key_pair (const std::filesystem::path rsa_priv_key_path, const std::filesystem::path rsa_pub_key_path);
