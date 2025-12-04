#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

namespace ldm::metrics {

// Lightweight stand-ins for prometheus-cpp counters/gauges so the skeleton
// builds without the external dependency. They intentionally mirror the
// interface shape that the rest of the project expects.
class Counter {
public:
    void Increment(double amount = 1.0);
    double Value() const;

private:
    mutable std::mutex mutex_;
    double value_{0.0};
};

class Gauge {
public:
    void Set(double value);
    double Value() const;

private:
    mutable std::mutex mutex_;
    double value_{0.0};
};

class Metrics {
public:
    explicit Metrics(int /*port*/);

    Counter& counter(const std::string& name, const std::string& help = "");
    Gauge& gauge(const std::string& name, const std::string& help = "");

private:
    std::mutex mutex_;
    std::unordered_map<std::string, Counter> counters_;
    std::unordered_map<std::string, Gauge> gauges_;
};

}  // namespace ldm::metrics

