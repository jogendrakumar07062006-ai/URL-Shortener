#pragma once
#include "models.hpp"
#include "url_repository.hpp"
#include <chrono>
#include <string>

class UrlService {
public:
    static std::string generate_short_code(std::size_t length);
    std::string create_short_url(const std::string& long_url,
                                  const std::optional<std::chrono::system_clock::time_point>& expiry_time,
                                  UrlRepository& repo) const;
};
