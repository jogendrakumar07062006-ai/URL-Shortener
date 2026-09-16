#include "config.hpp"
#include "database.hpp"
#include "http_server.hpp"
#include "rate_limiter.hpp"
#include "url_repository.hpp"
#include "url_service.hpp"
#include <csignal>
#include <iostream>

namespace { HttpServer* server = nullptr; void signal_handler(int) { if (server) server->stop(); } }

int main() {
    try {
        Database database(config::db_path(), config::pool_size());
        UrlRepository repo(database);
        UrlService service;
        RateLimiter limiter;
        HttpServer app(config::port(), repo, service, limiter, config::base_url());
        server = &app;
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        app.run();
        server = nullptr;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return 1;
    }
}
