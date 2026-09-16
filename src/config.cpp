#include "config.hpp"
#include <cstdlib>
#include <stdexcept>

namespace config {
std::string env_or_default(const char* key, const std::string& fallback) {
    const char* value = std::getenv(key);
    return value && *value ? std::string(value) : fallback;
}

std::string base_url() {
    std::string value = env_or_default("BASE_URL", "http://localhost:7070");
    while (value.size() > 1 && value.back() == '/') value.pop_back();
    return value;
}

std::string db_path() { return env_or_default("DB_PATH", "urlshortener.db"); }

unsigned short port() {
    const std::string p = env_or_default("PORT", "7070");
    try {
        const unsigned long value = std::stoul(p);
        if (value > 65535) throw std::out_of_range("port");
        return static_cast<unsigned short>(value);
    } catch (...) {
        throw std::runtime_error("PORT must be an integer between 0 and 65535");
    }
}

int pool_size() {
    const std::string p = env_or_default("DB_POOL_SIZE", "10");
    try {
        const int value = std::stoi(p);
        if (value < 1 || value > 100) throw std::out_of_range("pool size");
        return value;
    } catch (...) {
        throw std::runtime_error("DB_POOL_SIZE must be between 1 and 100");
    }
}
}
