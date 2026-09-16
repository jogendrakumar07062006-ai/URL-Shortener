#pragma once
#include "cache.hpp"
#include "database.hpp"
#include "models.hpp"
#include <optional>
#include <string>

class UrlRepository {
public:
    explicit UrlRepository(Database& database) : database_(database), cache_(10000, std::chrono::minutes(10)) {}

    std::optional<std::string> insert_if_not_exists(const std::string& short_code,
                                                     const std::string& long_url,
                                                     const std::optional<std::chrono::system_clock::time_point>& expires_at);
    std::optional<UrlRecord> find_by_short_code(const std::string& short_code);
    std::optional<std::string> find_by_long_url(const std::string& long_url);
    bool increment_click_count(const std::string& short_code);

private:
    Database& database_;
    AccessTtlCache<UrlRecord> cache_;
};
