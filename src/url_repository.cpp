#include "url_repository.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
std::string to_sqlite_timestamp(const std::chrono::system_clock::time_point& tp) {
    const auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return buffer;
}

std::optional<std::chrono::system_clock::time_point> from_sqlite_timestamp(const unsigned char* value) {
    if (!value) return std::nullopt;
    std::tm tm{};
    std::istringstream ss(reinterpret_cast<const char*>(value));
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) throw std::runtime_error("Invalid SQLite timestamp");
    return std::chrono::system_clock::from_time_t(timegm(&tm));
}

class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        const int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
        if (rc != SQLITE_OK) throw std::runtime_error("SQLite prepare failed: " + std::string(sqlite3_errmsg(db_)));
    }
    ~Statement() { if (stmt_) sqlite3_finalize(stmt_); }
    sqlite3_stmt* get() const { return stmt_; }
private:
    sqlite3* db_;
    sqlite3_stmt* stmt_{nullptr};
};

void check_step(sqlite3* db, int rc, const char* operation) {
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw std::runtime_error(std::string("SQLite ") + operation + " failed: " + sqlite3_errmsg(db));
    }
}
}

std::optional<std::string> UrlRepository::insert_if_not_exists(
    const std::string& short_code, const std::string& long_url,
    const std::optional<std::chrono::system_clock::time_point>& expires_at) {
    auto conn = database_.acquire();
    Statement stmt(conn.get(),
        "INSERT OR IGNORE INTO urls (short_code, long_url, expires_at) VALUES (?, ?, ?)");
    sqlite3_bind_text(stmt.get(), 1, short_code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.get(), 2, long_url.c_str(), -1, SQLITE_TRANSIENT);
    if (expires_at) {
        const std::string expiry = to_sqlite_timestamp(*expires_at);
        sqlite3_bind_text(stmt.get(), 3, expiry.c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt.get(), 3);
    }
    check_step(conn.get(), sqlite3_step(stmt.get()), "insert");

    if (sqlite3_changes(conn.get()) == 1) return short_code;

    // INSERT OR IGNORE can mean either a short-code collision or an existing long URL.
    // Querying the unique long URL lets the service distinguish those cases safely.
    Statement lookup(conn.get(), "SELECT short_code FROM urls WHERE long_url = ? LIMIT 1");
    sqlite3_bind_text(lookup.get(), 1, long_url.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(lookup.get());
    if (rc == SQLITE_ROW) {
        const unsigned char* value = sqlite3_column_text(lookup.get(), 0);
        return value ? std::optional<std::string>(reinterpret_cast<const char*>(value)) : std::nullopt;
    }
    if (rc != SQLITE_DONE) check_step(conn.get(), rc, "duplicate lookup");
    return std::nullopt;
}

std::optional<UrlRecord> UrlRepository::find_by_short_code(const std::string& short_code) {
    if (auto cached = cache_.get(short_code)) return cached;

    auto conn = database_.acquire();
    Statement stmt(conn.get(), "SELECT long_url, expires_at FROM urls WHERE short_code = ? LIMIT 1");
    sqlite3_bind_text(stmt.get(), 1, short_code.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    check_step(conn.get(), rc, "short-code lookup");

    UrlRecord record;
    const unsigned char* long_url = sqlite3_column_text(stmt.get(), 0);
    if (!long_url) throw std::runtime_error("SQLite returned a NULL long URL");
    record.long_url = reinterpret_cast<const char*>(long_url);
    if (sqlite3_column_type(stmt.get(), 1) != SQLITE_NULL)
        record.expires_at = from_sqlite_timestamp(sqlite3_column_text(stmt.get(), 1));
    cache_.put(short_code, record);
    return record;
}

std::optional<std::string> UrlRepository::find_by_long_url(const std::string& long_url) {
    auto conn = database_.acquire();
    Statement stmt(conn.get(), "SELECT short_code FROM urls WHERE long_url = ? LIMIT 1");
    sqlite3_bind_text(stmt.get(), 1, long_url.c_str(), -1, SQLITE_TRANSIENT);
    const int rc = sqlite3_step(stmt.get());
    if (rc == SQLITE_DONE) return std::nullopt;
    check_step(conn.get(), rc, "long-URL lookup");
    const unsigned char* value = sqlite3_column_text(stmt.get(), 0);
    return value ? std::optional<std::string>(reinterpret_cast<const char*>(value)) : std::nullopt;
}

bool UrlRepository::increment_click_count(const std::string& short_code) {
    auto conn = database_.acquire();
    Statement stmt(conn.get(), "UPDATE urls SET click_count = click_count + 1 WHERE short_code = ?");
    sqlite3_bind_text(stmt.get(), 1, short_code.c_str(), -1, SQLITE_TRANSIENT);
    check_step(conn.get(), sqlite3_step(stmt.get()), "click-count update");
    return sqlite3_changes(conn.get()) == 1;
}
