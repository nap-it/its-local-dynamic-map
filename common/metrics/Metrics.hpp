#pragma once

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/gauge.h>
#include <prometheus/registry.h>

#include <memory>
#include <string>

namespace ldm::metrics {

class Metrics {
public:
    explicit Metrics(int port);

    prometheus::Counter& counter(const std::string& name, const std::string& help = "");
    prometheus::Gauge& gauge(const std::string& name, const std::string& help = "");

private:
    std::shared_ptr<prometheus::Registry> registry_;
    std::unique_ptr<prometheus::Exposer> exposer_;
};

}  // namespace ldm::metrics
