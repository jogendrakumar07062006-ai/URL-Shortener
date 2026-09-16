#include "http_server.hpp"
#include "json_util.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
struct HttpRequest { std::string method, target, body; };

std::string reason(int status) {
    switch (status) {
        case 200: return "OK"; case 302: return "Found"; case 400: return "Bad Request";
        case 404: return "Not Found"; case 410: return "Gone"; case 429: return "Too Many Requests";
        case 500: return "Internal Server Error"; case 405: return "Method Not Allowed";
        default: return "";
    }
}
std::string response(int status, const std::string& body, const std::string& content_type = "text/plain; charset=utf-8", const std::string& location = "") {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << reason(status) << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n";
    if (!location.empty()) out << "Location: " << location << "\r\n";
    out << "\r\n" << body;
    return out.str();
}

bool send_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

std::optional<HttpRequest> read_request(int fd) {
    std::string data;
    char buffer[4096];
    std::size_t header_end = std::string::npos;
    while (data.size() < 1024 * 1024) {
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) return std::nullopt;
        data.append(buffer, static_cast<std::size_t>(n));
        header_end = data.find("\r\n\r\n");
        if (header_end != std::string::npos) break;
    }
    if (header_end == std::string::npos) return std::nullopt;
    std::string headers = data.substr(0, header_end);
    std::istringstream hs(headers);
    HttpRequest req;
    std::string request_line;
    if (!std::getline(hs, request_line)) return std::nullopt;
    if (!request_line.empty() && request_line.back() == '\r') request_line.pop_back();
    std::istringstream rl(request_line);
    std::string version;
    rl >> req.method >> req.target >> version;
    if (req.method.empty() || req.target.empty() || version != "HTTP/1.1") return std::nullopt;

    std::size_t content_length = 0;
    std::string line;
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (key == "content-length") {
            try { content_length = std::stoull(value); } catch (...) { return std::nullopt; }
        }
    }
    if (content_length > 1024 * 1024) return std::nullopt;
    std::size_t body_start = header_end + 4;
    while (data.size() - body_start < content_length) {
        ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) return std::nullopt;
        data.append(buffer, static_cast<std::size_t>(n));
    }
    req.body = data.substr(body_start, content_length);
    return req;
}

std::string url_decode_path(std::string path) {
    // Short codes are intentionally restricted below, so no general percent-decoding is needed.
    return path;
}

bool valid_code(const std::string& code) {
    if (code.empty() || code.size() > 64) return false;
    return std::all_of(code.begin(), code.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '-' || c == '_';
    });
}
}

std::string json_escape(const std::string& value) {
    std::string out;
    for (char raw : value) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"': out += "\\\""; break; case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break; case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break; case '\r': out += "\\r"; break; case '\t': out += "\\t"; break;
            default: if (c < 0x20) { char buf[7]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf; } else out += static_cast<char>(c);
        }
    }
    return out;
}
std::string json_error(const std::string& message) { return "{\"error\":\"" + json_escape(message) + "\"}"; }
std::string json_short_url(const std::string& url) { return "{\"shortUrl\":\"" + json_escape(url) + "\"}"; }

namespace {
class JsonParser {
public:
    explicit JsonParser(const std::string& input) : input_(input) {}

    bool parse_object(std::string& url, bool& has_url, std::optional<long long>& expiry,
                      bool& has_expiry, std::string& error) {
        skip_ws();
        if (!consume('{')) { error = "request body must be a JSON object"; return false; }
        skip_ws();
        if (consume('}')) {
            skip_ws();
            if (!at_end()) { error = "invalid JSON: trailing data"; return false; }
            return true;
        }

        while (true) {
            std::string key;
            if (!parse_string(key)) { error = "invalid JSON object key"; return false; }
            skip_ws();
            if (!consume(':')) { error = "invalid JSON: expected ':'"; return false; }
            skip_ws();

            if (key == "url") {
                std::string value;
                if (!parse_string(value)) { error = "url must be a string"; return false; }
                url = std::move(value);
                has_url = true;
            } else if (key == "expiresInSecond") {
                bool is_null = false;
                long long value = 0;
                if (!parse_integer_or_null(value, is_null)) {
                    error = "expiresInSecond must be an integer or null";
                    return false;
                }
                has_expiry = true;
                expiry = is_null ? std::nullopt : std::optional<long long>(value);
            } else {
                if (!skip_value()) { error = "invalid JSON value"; return false; }
            }

            skip_ws();
            if (consume('}')) break;
            if (!consume(',')) { error = "invalid JSON: expected ',' or '}'"; return false; }
            skip_ws();
        }

        skip_ws();
        if (!at_end()) { error = "invalid JSON: trailing data"; return false; }
        return true;
    }

private:
    const std::string& input_;
    std::size_t pos_{0};

    bool at_end() const { return pos_ == input_.size(); }
    void skip_ws() {
        while (!at_end() && std::isspace(static_cast<unsigned char>(input_[pos_]))) ++pos_;
    }
    bool consume(char c) {
        if (!at_end() && input_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    static bool hex_digit(char c, unsigned& value) {
        if (c >= '0' && c <= '9') { value = static_cast<unsigned>(c - '0'); return true; }
        if (c >= 'a' && c <= 'f') { value = static_cast<unsigned>(c - 'a' + 10); return true; }
        if (c >= 'A' && c <= 'F') { value = static_cast<unsigned>(c - 'A' + 10); return true; }
        return false;
    }

    bool parse_string(std::string& out) {
        if (!consume('"')) return false;
        out.clear();
        while (!at_end()) {
            const unsigned char c = static_cast<unsigned char>(input_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;
            if (c != '\\') { out.push_back(static_cast<char>(c)); continue; }
            if (at_end()) return false;
            const char esc = input_[pos_++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned codepoint = 0;
                    for (int i = 0; i < 4; ++i) {
                        if (at_end()) return false;
                        unsigned nibble = 0;
                        if (!hex_digit(input_[pos_++], nibble)) return false;
                        codepoint = (codepoint << 4U) | nibble;
                    }
                    // Encode the BMP code point as UTF-8. Surrogate pairs are rejected
                    // rather than producing malformed UTF-8.
                    if (codepoint >= 0xD800U && codepoint <= 0xDFFFU) return false;
                    if (codepoint <= 0x7FU) out.push_back(static_cast<char>(codepoint));
                    else if (codepoint <= 0x7FFU) {
                        out.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
                        out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
                    } else {
                        out.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
                        out.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
                        out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
                    }
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    bool parse_integer_or_null(long long& value, bool& is_null) {
        is_null = false;
        if (input_.compare(pos_, 4, "null") == 0) { pos_ += 4; is_null = true; return true; }
        const std::size_t begin = pos_;
        if (!at_end() && input_[pos_] == '-') ++pos_;
        const std::size_t digits = pos_;
        while (!at_end() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        if (pos_ == digits) { pos_ = begin; return false; }
        // JSON numbers do not permit a leading zero followed by more digits.
        if (pos_ - digits > 1 && input_[digits] == '0') { pos_ = begin; return false; }
        try {
            value = std::stoll(input_.substr(begin, pos_ - begin));
        } catch (...) {
            pos_ = begin;
            return false;
        }
        return true;
    }

    bool skip_string() {
        std::string ignored;
        return parse_string(ignored);
    }

    bool skip_value() {
        if (at_end()) return false;
        if (input_[pos_] == '"') return skip_string();
        if (input_[pos_] == '{') {
            ++pos_; skip_ws();
            if (consume('}')) return true;
            while (true) {
                std::string key;
                if (!parse_string(key)) return false;
                skip_ws(); if (!consume(':')) return false; skip_ws();
                if (!skip_value()) return false;
                skip_ws();
                if (consume('}')) return true;
                if (!consume(',')) return false;
                skip_ws();
            }
        }
        if (input_[pos_] == '[') {
            ++pos_; skip_ws();
            if (consume(']')) return true;
            while (true) {
                if (!skip_value()) return false;
                skip_ws();
                if (consume(']')) return true;
                if (!consume(',')) return false;
                skip_ws();
            }
        }
        for (const char* literal : {"true", "false", "null"}) {
            const std::size_t len = std::strlen(literal);
            if (input_.compare(pos_, len, literal) == 0) { pos_ += len; return true; }
        }
        const std::size_t begin = pos_;
        if (!at_end() && input_[pos_] == '-') ++pos_;
        if (at_end()) { pos_ = begin; return false; }
        if (input_[pos_] == '0') {
            ++pos_;
        } else if (input_[pos_] >= '1' && input_[pos_] <= '9') {
            while (!at_end() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
        } else {
            pos_ = begin; return false;
        }
        if (!at_end() && input_[pos_] == '.') {
            ++pos_;
            const std::size_t fraction = pos_;
            while (!at_end() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            if (pos_ == fraction) { pos_ = begin; return false; }
        }
        if (!at_end() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
            ++pos_;
            if (!at_end() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
            const std::size_t exponent = pos_;
            while (!at_end() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) ++pos_;
            if (pos_ == exponent) { pos_ = begin; return false; }
        }
        return pos_ != begin;
    }
};
}

std::optional<ShortenRequest> parse_shorten_request(const std::string& body, std::string& error) {
    JsonParser parser(body);
    std::string url;
    bool has_url = false;
    std::optional<long long> expiry;
    bool has_expiry = false;
    if (!parser.parse_object(url, has_url, expiry, has_expiry, error)) return std::nullopt;
    if (!has_url) { error = "url is required"; return std::nullopt; }
    ShortenRequest req;
    req.url = std::move(url);
    if (has_expiry) req.expires_in_seconds = expiry;
    return req;
}

HttpServer::HttpServer(unsigned short port, UrlRepository& repo, UrlService& service, RateLimiter& limiter, std::string base_url)
    : port_(port), repo_(repo), service_(service), limiter_(limiter), base_url_(std::move(base_url)) {}

void HttpServer::run() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw std::runtime_error("socket() failed");
    int opt = 1; setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_ANY); addr.sin_port = htons(port_);
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { close(listen_fd_); throw std::runtime_error("bind() failed"); }
    if (listen(listen_fd_, 128) < 0) { close(listen_fd_); throw std::runtime_error("listen() failed"); }
    std::cout << "Server listening on port " << port_ << '\n';
    while (running_) {
        sockaddr_in peer{}; socklen_t len = sizeof(peer);
        int client = accept(listen_fd_, reinterpret_cast<sockaddr*>(&peer), &len);
        if (client < 0) { if (running_) continue; break; }
        char ip[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        std::lock_guard<std::mutex> lock(client_mutex_);
        client_threads_.emplace_back(&HttpServer::handle_client, this, client, std::string(ip));
    }
    std::lock_guard<std::mutex> lock(client_mutex_);
    for (auto& thread : client_threads_) {
        if (thread.joinable()) thread.join();
    }
    client_threads_.clear();
}

void HttpServer::stop() {
    running_ = false;
    if (listen_fd_ >= 0) { shutdown(listen_fd_, SHUT_RDWR); close(listen_fd_); listen_fd_ = -1; }
}

void HttpServer::handle_client(int client_fd, const std::string& peer_ip) {
    try {
        auto request = read_request(client_fd);
        if (!request) { send_all(client_fd, response(400, "Bad Request")); close(client_fd); return; }
        if (request->method == "POST" && request->target == "/api/shorten") {
            if (!limiter_.allow(peer_ip)) { send_all(client_fd, response(429, json_error("Too many requests"), "application/json")); close(client_fd); return; }
            std::string parse_error;
            auto parsed = parse_shorten_request(request->body, parse_error);
            if (!parsed) { send_all(client_fd, response(400, json_error(parse_error), "application/json")); close(client_fd); return; }
            if (parsed->url.empty()) { send_all(client_fd, response(400, json_error("url is required"), "application/json")); close(client_fd); return; }
            if (!(parsed->url.starts_with("http://") || parsed->url.starts_with("https://"))) {
                send_all(client_fd, response(400, json_error("not a valid url"), "application/json")); close(client_fd); return;
            }
            std::optional<std::chrono::system_clock::time_point> expiry;
            if (parsed->expires_in_seconds) {
                if (*parsed->expires_in_seconds < 0) { send_all(client_fd, response(400, json_error("expiresInSecond must be non-negative"), "application/json")); close(client_fd); return; }
                expiry = std::chrono::system_clock::now() + std::chrono::seconds(*parsed->expires_in_seconds);
            }
            std::string code = service_.create_short_url(parsed->url, expiry, repo_);
            send_all(client_fd, response(200, json_short_url(base_url_ + "/" + code), "application/json"));
        } else if (request->method == "GET" && request->target == "/") {
            const std::string body = "Hello Sir/Mam, this is our url shortener. If you want to generate a short url of you long url, you can you use our service or if you have already a short url generated by us, just use it!";
            send_all(client_fd, response(200, body));
        } else if (request->method == "GET") {
            std::string code = request->target.substr(1);
            auto q = code.find('?'); if (q != std::string::npos) code.resize(q);
            code = url_decode_path(code);
            if (!valid_code(code)) { send_all(client_fd, response(404, "Not Found")); close(client_fd); return; }
            auto record = repo_.find_by_short_code(code);
            if (!record) send_all(client_fd, response(404, "Not Found"));
            else if (record->expires_at && *record->expires_at < std::chrono::system_clock::now()) send_all(client_fd, response(410, "Gone"));
            else {
                repo_.increment_click_count(code);
                send_all(client_fd, response(302, "", "text/plain", record->long_url));
            }
        } else {
            send_all(client_fd, response(405, "Method Not Allowed"));
        }
    } catch (const std::exception& e) {
        std::cerr << "request error: " << e.what() << '\n';
        send_all(client_fd, response(500, "Internal Server Error"));
    }
    close(client_fd);
}
