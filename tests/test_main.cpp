#include "cache.hpp"
#include "database.hpp"
#include "json_util.hpp"
#include "rate_limiter.hpp"
#include "url_repository.hpp"
#include "url_service.hpp"
#include <sqlite3.h>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <stdexcept>

#define REQUIRE(condition) do { if (!(condition)) { std::cerr << "Test failure: " #condition << "\n"; return 1; } } while (false)

static long long read_click_count(const std::string& path, const std::string& code) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) { sqlite3_close(db); throw std::runtime_error("cannot open test database"); }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT click_count FROM urls WHERE short_code = ?", -1, &stmt, nullptr) != SQLITE_OK) { sqlite3_close(db); throw std::runtime_error("cannot prepare click-count query"); }
    sqlite3_bind_text(stmt, 1, code.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) { sqlite3_finalize(stmt); sqlite3_close(db); throw std::runtime_error("click-count row missing"); }
    const long long count = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count;
}

int main() {
    {
        std::set<std::string> codes;
        for (int i = 0; i < 10000; ++i) codes.insert(UrlService::generate_short_code(7));
        REQUIRE(codes.size() > 9950);
        for (const auto& code : codes) {
            REQUIRE(code.size() == 7);
            for (char c : code) REQUIRE(std::isalnum(static_cast<unsigned char>(c)) != 0);
        }
    }
    {
        AccessTtlCache<UrlRecord> cache(2, std::chrono::minutes(10));
        cache.put("a", UrlRecord{"https://a.example", std::nullopt});
        REQUIRE(cache.get("a")->long_url == "https://a.example");
        cache.put("b", UrlRecord{"https://b.example", std::nullopt});
        REQUIRE(cache.get("a"));
        cache.put("c", UrlRecord{"https://c.example", std::nullopt});
        REQUIRE(!cache.get("b"));
        REQUIRE(cache.get("a"));
        REQUIRE(cache.get("c"));
    }
    {
        std::string error;
        auto req = parse_shorten_request(R"({"url":"https://example.com/a","expiresInSecond":3600})", error);
        REQUIRE(req);
        REQUIRE(req->url == "https://example.com/a");
        REQUIRE(req->expires_in_seconds && *req->expires_in_seconds == 3600);
        auto req2 = parse_shorten_request(R"({"url":"https://example.com"})", error);
        REQUIRE(req2 && !req2->expires_in_seconds);
        REQUIRE(!parse_shorten_request(R"({"expiresInSecond":10})", error));
        REQUIRE(!parse_shorten_request(R"({"url":"https://example.com"} garbage)", error));
        REQUIRE(!parse_shorten_request(R"({"url":"https://example.com","expiresInSecond":01})", error));
        REQUIRE(parse_shorten_request(R"({"url":"https://example.com","extra":{"x":true}})", error));
        REQUIRE(parse_shorten_request(R"({"url":"https://example.com","extra":1.25e2})", error));
        REQUIRE(!parse_shorten_request(R"({} garbage)", error));
    }
    {
        RateLimiter limiter(100, std::chrono::seconds(60));
        for (int i = 0; i < 100; ++i) REQUIRE(limiter.allow("127.0.0.1"));
        REQUIRE(!limiter.allow("127.0.0.1"));
        REQUIRE(limiter.allow("127.0.0.2"));
    }
    {
        RateLimiter limiter(100, std::chrono::seconds(60));
        std::vector<std::thread> threads;
        std::atomic<int> allowed{0};
        for (int i = 0; i < 200; ++i) threads.emplace_back([&] { if (limiter.allow("10.0.0.1")) ++allowed; });
        for (auto& t : threads) t.join();
        REQUIRE(allowed == 100);
    }

    const std::string db_path = "test_urlshortener.db";
    std::remove(db_path.c_str());
    {
        Database database(db_path, 8);
        UrlRepository repo(database);
        UrlService service;

        const std::string first = service.create_short_url("https://example.com/a", std::nullopt, repo);
        REQUIRE(first.size() == 7);
        REQUIRE(repo.find_by_long_url("https://example.com/a") == first);
        auto record = repo.find_by_short_code(first);
        REQUIRE(record && record->long_url == "https://example.com/a");

        // Duplicate long URLs must be idempotent and return the existing code.
        const std::string duplicate = service.create_short_url("https://example.com/a", std::nullopt, repo);
        REQUIRE(duplicate == first);

        // Atomic increments must survive concurrent updates.
        constexpr int threads_count = 8;
        constexpr int increments_per_thread = 125;
        std::vector<std::thread> threads;
        for (int i = 0; i < threads_count; ++i) {
            threads.emplace_back([&repo, &first] {
                for (int j = 0; j < increments_per_thread; ++j) {
                    if (!repo.increment_click_count(first)) throw std::runtime_error("click increment unexpectedly failed");
                }
            });
        }
        for (auto& t : threads) t.join();
        REQUIRE(read_click_count(db_path, first) == threads_count * increments_per_thread);

        const auto expiry = std::chrono::system_clock::now() + std::chrono::seconds(3600);
        const std::string expiring = service.create_short_url("https://example.com/expiry", expiry, repo);
        auto exp_record = repo.find_by_short_code(expiring);
        REQUIRE(exp_record && exp_record->expires_at.has_value());

        // A deliberately occupied code is treated as a collision, not as an existing URL.
        const std::string collision_code = "collision";
        REQUIRE(repo.insert_if_not_exists(collision_code, "https://example.com/collision", std::nullopt) == collision_code);
        REQUIRE(!repo.insert_if_not_exists(collision_code, "https://example.com/different", std::nullopt));
    }
    std::remove(db_path.c_str());

    std::cout << "All unit and SQLite integration tests passed\n";
}
