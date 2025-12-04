#include <chrono>
#include <thread>

#include <spdlog/spdlog.h>

#include "../../common/config/Config.hpp"
#include "../../common/locator/Locator.hpp"
#include "../../common/metrics/Metrics.hpp"

int main() {
    using namespace std::chrono_literals;
    auto cfg = ldm::config::loadConfig("/config.ini");
    spdlog::set_level(cfg.general.debug ? spdlog::level::debug : spdlog::level::info);
    spdlog::info("[LDM-Ingestion] Starting MAPEM ingestion service");

    ldm::locator::Locator locator(cfg.locator);
    std::unique_ptr<ldm::metrics::Metrics> metrics;
    if (cfg.general.prometheus) {
        metrics = std::make_unique<ldm::metrics::Metrics>(cfg.general.prometheus_port);
        metrics->counter("ldm_ingestion_mapem_total", "Total MAPEM messages ingested");
    }

    // Placeholder: DDS/MQTT subscription would go here.
    while (true) {
        std::this_thread::sleep_for(5s);
    }
    return 0;
}
