#include "url_service.hpp"
#include <openssl/rand.h>
#include <stdexcept>
#include <vector>
#include <string_view>

namespace {
constexpr std::string_view ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
}

std::string UrlService::generate_short_code(std::size_t length) {
    std::string result(length, 'a');
    if (length == 0) return result;
    std::vector<unsigned char> random_bytes(length);
    if (RAND_bytes(random_bytes.data(), static_cast<int>(random_bytes.size())) != 1) {
        throw std::runtime_error("Cryptographically secure random generation failed");
    }
    for (std::size_t i = 0; i < length; ++i) result[i] = ALPHABET[random_bytes[i] % ALPHABET.size()];
    return result;
}

std::string UrlService::create_short_url(const std::string& long_url,
                                          const std::optional<std::chrono::system_clock::time_point>& expiry_time,
                                          UrlRepository& repo) const {
    constexpr int max_attempts = 5;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        const std::string code = generate_short_code(7);
        auto inserted = repo.insert_if_not_exists(code, long_url, expiry_time);
        if (inserted) return *inserted;
    }
    throw std::runtime_error("Failed to generate a unique short code after 5 attempts");
}
