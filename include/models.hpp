#pragma once
#include <chrono>
#include <optional>
#include <string>

struct UrlRecord {
    std::string long_url;
    std::optional<std::chrono::system_clock::time_point> expires_at;
};

struct ShortenRequest {
    std::string url;
    std::optional<long long> expires_in_seconds;
};
