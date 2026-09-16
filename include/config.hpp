#pragma once
#include <string>

namespace config {
std::string env_or_default(const char* key, const std::string& fallback);
std::string base_url();
std::string db_path();
unsigned short port();
int pool_size();
}
