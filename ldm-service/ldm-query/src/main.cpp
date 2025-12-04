#include <chrono>
#include <thread>

#include <iostream>

#include "../../common/config/Config.hpp"
#include "../../common/locator/Locator.hpp"
#include "../../common/metrics/Metrics.hpp"
#include "../../common/ldm-model/LdmStore.hpp"

int main() {
    using namespace std::chrono_literals;
    auto cfg = ldm::config::loadConfig("/config.ini");
    if (cfg.general.debug) {
        std::clog << "[LDM-Query] Debug logging enabled\n";
    }
    std::clog << "[LDM-Query] Starting query service\n";

    ldm::locator::Locator locator(cfg.locator);
    auto [lat, lon] = locator.currentPosition();
    ldm::model::LdmStore store(cfg.ldm_core.aom_radius_m);
    store.setStationReference(lat, lon);

    std::unique_ptr<ldm::metrics::Metrics> metrics;
    if (cfg.general.prometheus) {
        metrics = std::make_unique<ldm::metrics::Metrics>(cfg.general.prometheus_port);
        metrics->counter("ldm_query_count", "Total queries processed");
    }

    // Placeholder for DDS/MQTT query handling loop.
    while (true) {
        std::this_thread::sleep_for(5s);
    }
    return 0;
}
