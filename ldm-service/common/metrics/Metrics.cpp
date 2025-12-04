#include "Metrics.hpp"

#include <utility>

namespace ldm::metrics {

void Counter::Increment(double amount) {
    std::lock_guard<std::mutex> lock(mutex_);
    value_ += amount;
}

double Counter::Value() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return value_;
}

void Gauge::Set(double value) {
    std::lock_guard<std::mutex> lock(mutex_);
    value_ = value;
}

double Gauge::Value() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return value_;
}

Metrics::Metrics(int /*port*/) {
    // In a full deployment this would construct a Prometheus exposer bound to
    // the provided port. The skeleton retains the interface while avoiding an
    // external dependency for local builds.
}

Counter& Metrics::counter(const std::string& name, const std::string& /*help*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    return counters_[name];
}

Gauge& Metrics::gauge(const std::string& name, const std::string& /*help*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    return gauges_[name];
}

}  // namespace ldm::metrics

