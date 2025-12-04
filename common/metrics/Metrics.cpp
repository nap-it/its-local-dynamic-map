#include "Metrics.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace ldm::metrics {

namespace {
constexpr int kMaxBacklog = 16;
}

void Counter::Increment(double value) {
    if (value <= 0.0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    value_ += value;
}

double Counter::Value() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return value_;
}

void Gauge::Increment(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    value_ += value;
}

void Gauge::Decrement(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    value_ -= value;
}

void Gauge::Set(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    value_ = value;
}

double Gauge::Value() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return value_;
}

Metrics::Metrics(int port) : port_(port) {
    if (port_ <= 0) {
        throw std::runtime_error("Metrics port must be greater than zero");
    }
    startServer();
}

Metrics::~Metrics() {
    stopServer();
}

Counter& Metrics::counter(const std::string& name, const std::string& help) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = counters_.try_emplace(name);
    if (inserted) {
        it->second.metric = std::make_unique<Counter>();
        it->second.help = help.empty() ? name : help;
    }
    return *(it->second.metric);
}

Gauge& Metrics::gauge(const std::string& name, const std::string& help) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto [it, inserted] = gauges_.try_emplace(name);
    if (inserted) {
        it->second.metric = std::make_unique<Gauge>();
        it->second.help = help.empty() ? name : help;
    }
    return *(it->second.metric);
}

void Metrics::startServer() {
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        throw std::runtime_error("Failed to create metrics socket");
    }

    int enable = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        throw std::runtime_error("Failed to bind metrics socket");
    }

    if (::listen(server_fd_, kMaxBacklog) < 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        throw std::runtime_error("Failed to listen on metrics socket");
    }

    running_.store(true);
    server_thread_ = std::thread(&Metrics::runServer, this);
}

void Metrics::stopServer() {
    running_.store(false);
    if (server_fd_ >= 0) {
        ::shutdown(server_fd_, SHUT_RDWR);
        ::close(server_fd_);
        server_fd_ = -1;
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

void Metrics::runServer() {
    while (running_.load()) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
        if (client_fd < 0) {
            if (!running_.load()) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            continue;
        }
        handleClient(client_fd);
    }
}

void Metrics::handleClient(int client_fd) {
    std::string request;
    char buffer[1024];
    while (true) {
        ssize_t bytes = ::recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            break;
        }
        request.append(buffer, static_cast<size_t>(bytes));
        if (request.find("\r\n\r\n") != std::string::npos || request.find("\n\n") != std::string::npos) {
            break;
        }
    }

    const auto body = renderMetrics();
    std::ostringstream oss;
    oss << "HTTP/1.1 200 OK\r\n"
        << "Content-Type: text/plain; version=0.0.4\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    const auto response = oss.str();

    size_t sent = 0;
    while (sent < response.size()) {
        ssize_t bytes = ::send(client_fd, response.data() + sent, response.size() - sent, 0);
        if (bytes <= 0) {
            break;
        }
        sent += static_cast<size_t>(bytes);
    }

    ::close(client_fd);
}

std::string Metrics::renderMetrics() const {
    std::ostringstream stream;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [name, entry] : counters_) {
        if (!entry.metric) {
            continue;
        }
        const std::string& help = entry.help.empty() ? name : entry.help;
        stream << "# HELP " << name << ' ' << sanitizeHelp(help) << '\n';
        stream << "# TYPE " << name << " counter\n";
        stream << name << ' ' << entry.metric->Value() << '\n';
    }

    for (const auto& [name, entry] : gauges_) {
        if (!entry.metric) {
            continue;
        }
        const std::string& help = entry.help.empty() ? name : entry.help;
        stream << "# HELP " << name << ' ' << sanitizeHelp(help) << '\n';
        stream << "# TYPE " << name << " gauge\n";
        stream << name << ' ' << entry.metric->Value() << '\n';
    }

    return stream.str();
}

std::string Metrics::sanitizeHelp(const std::string& text) {
    std::string cleaned;
    cleaned.reserve(text.size());
    for (char ch : text) {
        if (ch == '\n' || ch == '\r') {
            cleaned.push_back(' ');
        } else {
            cleaned.push_back(ch);
        }
    }
    return cleaned;
}

}  // namespace ldm::metrics
