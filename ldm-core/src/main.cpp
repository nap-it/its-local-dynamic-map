#include <chrono>
#include <thread>

#include <spdlog/spdlog.h>

#include "../../common/config/Config.hpp"
#include "../../common/ldm-model/LdmStore.hpp"
#include "../../common/locator/Locator.hpp"
#include "../../common/metrics/Metrics.hpp"

int main() {
    using namespace std::chrono_literals;
    auto cfg = ldm::config::loadConfig("/config.ini");

    spdlog::set_level(cfg.general.debug ? spdlog::level::debug : spdlog::level::info);
    spdlog::info("[LDM-Core] Starting LDM core service");

    ldm::locator::Locator locator(cfg.locator);
    auto [lat, lon] = locator.currentPosition();

    ldm::model::LdmStore store(cfg.ldm_core.aom_radius_m);
    store.setStationReference(lat, lon);

    std::unique_ptr<ldm::metrics::Metrics> metrics;
    if (cfg.general.prometheus) {
        metrics = std::make_unique<ldm::metrics::Metrics>(cfg.general.prometheus_port);
        metrics->gauge("ldm_objects_total", "Current number of objects in the LDM");
    }

    while (true) {
        store.runMaintenance();
        std::this_thread::sleep_for(5s);
    }
    return 0;
}
