#pragma once
#include <sqlite3.h>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

class Database {
public:
    Database(const std::string& path, int pool_size);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    class Connection {
    public:
        Connection(Database* db, sqlite3* conn) : db_(db), conn_(conn) {}
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&& other) noexcept;
        Connection& operator=(Connection&& other) noexcept;
        ~Connection();
        sqlite3* get() const { return conn_; }
    private:
        Database* db_;
        sqlite3* conn_;
    };

    Connection acquire();

private:
    friend class Connection;
    void release(sqlite3* conn);
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<sqlite3*> available_;
    std::vector<sqlite3*> all_;
};
