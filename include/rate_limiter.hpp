#pragma once
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

class RateLimiter {
public:
    RateLimiter(std::size_t max_requests = 100, std::chrono::seconds window = std::chrono::seconds(60), std::size_t max_clients = 100000)
        : max_requests_(max_requests), window_(window), max_clients_(max_clients) {}
    bool allow(const std::string& ip);

private:
    struct Entry {
        std::deque<std::chrono::steady_clock::time_point> requests;
        std::chrono::steady_clock::time_point last_seen{};
    };
    std::size_t max_requests_;
    std::chrono::seconds window_;
    std::size_t max_clients_;
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> clients_;
};
