#pragma once
#include "models.hpp"
#include <optional>
#include <string>

std::optional<ShortenRequest> parse_shorten_request(const std::string& body, std::string& error);
std::string json_error(const std::string& message);
std::string json_short_url(const std::string& url);
std::string json_escape(const std::string& value);
