#pragma once
#include "rate_limiter.hpp"
#include "url_repository.hpp"
#include "url_service.hpp"
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <mutex>

class HttpServer {
public:
    HttpServer(unsigned short port, UrlRepository& repo, UrlService& service, RateLimiter& limiter, std::string base_url);
    void run();
    void stop();
private:
    void handle_client(int client_fd, const std::string& peer_ip);
    unsigned short port_;
    UrlRepository& repo_;
    UrlService& service_;
    RateLimiter& limiter_;
    std::string base_url_;
    std::atomic<bool> running_{true};
    int listen_fd_{-1};
    std::mutex client_mutex_;
    std::vector<std::thread> client_threads_;
};
