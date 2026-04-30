#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace ldm::metrics {

class Counter {
public:
    void Increment(double value = 1.0);
    double Value() const;

private:
    mutable std::mutex mutex_;
    double value_{0.0};
};

class Gauge {
public:
    void Increment(double value = 1.0);
    void Decrement(double value = 1.0);
    void Set(double value);
    double Value() const;

private:
    mutable std::mutex mutex_;
    double value_{0.0};
};

class Metrics {
public:
    explicit Metrics(int port);
    ~Metrics();

    Counter& counter(const std::string& name, const std::string& help = "");
    Gauge& gauge(const std::string& name, const std::string& help = "");

private:
    struct CounterEntry {
        std::unique_ptr<Counter> metric;
        std::string help;
    };

    struct GaugeEntry {
        std::unique_ptr<Gauge> metric;
        std::string help;
    };

    void startServer();
    void stopServer();
    void runServer();
    void handleClient(int client_fd);
    [[nodiscard]] std::string renderMetrics() const;
    static std::string sanitizeHelp(const std::string& text);

    int port_;
    int server_fd_{-1};
    std::atomic<bool> running_{false};
    std::thread server_thread_;
    mutable std::mutex mutex_;
    std::map<std::string, CounterEntry> counters_;
    std::map<std::string, GaugeEntry> gauges_;
};

}  // namespace ldm::metrics
