#pragma once
#include <chrono>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

template <typename T>
class AccessTtlCache {
public:
    using Clock = std::chrono::steady_clock;
    AccessTtlCache(std::size_t max_size, std::chrono::minutes ttl) : max_size_(max_size), ttl_(ttl) {}

    std::optional<T> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(key);
        if (it == entries_.end()) return std::nullopt;
        if (Clock::now() - it->second.last_access >= ttl_) {
            order_.erase(it->second.order_it);
            entries_.erase(it);
            return std::nullopt;
        }
        it->second.last_access = Clock::now();
        order_.splice(order_.end(), order_, it->second.order_it);
        it->second.order_it = std::prev(order_.end());
        return it->second.value;
    }

    void put(const std::string& key, const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = Clock::now();
        auto it = entries_.find(key);
        if (it != entries_.end()) {
            it->second.value = value;
            it->second.last_access = now;
            order_.splice(order_.end(), order_, it->second.order_it);
            it->second.order_it = std::prev(order_.end());
            return;
        }
        if (entries_.size() >= max_size_) {
            const std::string& oldest = order_.front();
            entries_.erase(oldest);
            order_.pop_front();
        }
        order_.push_back(key);
        auto order_it = std::prev(order_.end());
        entries_.emplace(key, Entry{value, now, order_it});
    }

private:
    struct Entry {
        T value;
        Clock::time_point last_access;
        typename std::list<std::string>::iterator order_it;
    };
    std::size_t max_size_;
    std::chrono::minutes ttl_;
    std::mutex mutex_;
    std::list<std::string> order_;
    std::unordered_map<std::string, Entry> entries_;
};
