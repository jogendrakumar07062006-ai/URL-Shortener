#include "rate_limiter.hpp"

bool RateLimiter::allow(const std::string& ip) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entry = clients_[ip];
    entry.last_seen = now;
    while (!entry.requests.empty() && now - entry.requests.front() >= window_) entry.requests.pop_front();
    if (entry.requests.size() >= max_requests_) return false;
    entry.requests.push_back(now);

    while (clients_.size() > max_clients_) {
        auto victim = clients_.end();
        for (auto it = clients_.begin(); it != clients_.end(); ++it) {
            if (it == clients_.find(ip)) continue;
            if (victim == clients_.end() || it->second.last_seen < victim->second.last_seen) victim = it;
        }
        if (victim == clients_.end()) break;
        clients_.erase(victim);
    }
    return true;
}
