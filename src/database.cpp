#include "database.hpp"
#include <stdexcept>

namespace {
void exec_sql(sqlite3* db, const char* sql) {
    char* error = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        std::string message = error ? error : sqlite3_errmsg(db);
        sqlite3_free(error);
        throw std::runtime_error("SQLite setup failed: " + message);
    }
}
}

Database::Database(const std::string& path, int pool_size) {
    if (pool_size < 1) throw std::runtime_error("DB_POOL_SIZE must be at least 1");
    try {
        for (int i = 0; i < pool_size; ++i) {
            sqlite3* conn = nullptr;
            const int rc = sqlite3_open_v2(path.c_str(), &conn,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
            if (rc != SQLITE_OK || !conn) {
                const std::string error = conn ? sqlite3_errmsg(conn) : "unable to allocate SQLite connection";
                if (conn) sqlite3_close(conn);
                throw std::runtime_error("SQLite connection failed: " + error);
            }
            sqlite3_busy_timeout(conn, 5000);
            exec_sql(conn, "PRAGMA foreign_keys=ON;");
            exec_sql(conn, "PRAGMA journal_mode=WAL;");
            exec_sql(conn, "PRAGMA synchronous=NORMAL;");
            exec_sql(conn, "PRAGMA busy_timeout=5000;");
            exec_sql(conn,
                "CREATE TABLE IF NOT EXISTS urls ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "short_code TEXT NOT NULL UNIQUE,"
                "long_url TEXT NOT NULL UNIQUE,"
                "created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%d %H:%M:%S', 'now')),"
                "expires_at TEXT,"
                "click_count INTEGER NOT NULL DEFAULT 0 CHECK (click_count >= 0));");
            exec_sql(conn, "CREATE INDEX IF NOT EXISTS idx_short_code ON urls(short_code);");
            all_.push_back(conn);
            available_.push(conn);
        }
    } catch (...) {
        for (sqlite3* conn : all_) sqlite3_close(conn);
        all_.clear();
        while (!available_.empty()) available_.pop();
        throw;
    }
}

Database::~Database() {
    for (sqlite3* conn : all_) sqlite3_close(conn);
}

Database::Connection::Connection(Connection&& other) noexcept : db_(other.db_), conn_(other.conn_) {
    other.db_ = nullptr;
    other.conn_ = nullptr;
}

Database::Connection& Database::Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        if (db_ && conn_) db_->release(conn_);
        db_ = other.db_;
        conn_ = other.conn_;
        other.db_ = nullptr;
        other.conn_ = nullptr;
    }
    return *this;
}

Database::Connection::~Connection() {
    if (db_ && conn_) db_->release(conn_);
}

Database::Connection Database::acquire() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !available_.empty(); });
    sqlite3* conn = available_.front();
    available_.pop();
    return Connection(this, conn);
}

void Database::release(sqlite3* conn) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        available_.push(conn);
    }
    cv_.notify_one();
}
