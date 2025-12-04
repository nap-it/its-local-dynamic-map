#include "Metrics.hpp"

namespace ldm::metrics {

Metrics::Metrics(int port) {
    registry_ = std::make_shared<prometheus::Registry>();
    exposer_ = std::make_unique<prometheus::Exposer>("0.0.0.0:" + std::to_string(port));
    exposer_->RegisterCollectable(registry_);
}

prometheus::Counter& Metrics::counter(const std::string& name, const std::string& help) {
    auto& family = prometheus::BuildCounter()
                       .Name(name)
                       .Help(help.empty() ? name : help)
                       .Register(*registry_);
    return family.Add({});
}

prometheus::Gauge& Metrics::gauge(const std::string& name, const std::string& help) {
    auto& family = prometheus::BuildGauge()
                       .Name(name)
                       .Help(help.empty() ? name : help)
                       .Register(*registry_);
    return family.Add({});
}

}  // namespace ldm::metrics
