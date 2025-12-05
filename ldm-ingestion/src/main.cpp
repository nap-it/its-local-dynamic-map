#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

#include <spdlog/spdlog.h>

#include "../../common/config/config.hpp"
#include "../../common/locator/locator.hpp"
#include "../../common/messaging/transport.hpp"
#include "../../common/metrics/metrics.hpp"

int main() {
    using namespace std::chrono_literals;
    auto cfg = ldm::config::loadConfig("/config.ini");
    spdlog::set_level(cfg.general.debug ? spdlog::level::debug : spdlog::level::info);
    spdlog::info("[LDM-Ingestion] Starting MAPEM ingestion service");

    ldm::locator::Locator locator(cfg.locator);
    std::unique_ptr<ldm::metrics::Metrics> metrics;
    ldm::metrics::Counter* received_counter = nullptr;
    ldm::metrics::Counter* forwarded_counter = nullptr;
    if (cfg.general.prometheus) {
        metrics = std::make_unique<ldm::metrics::Metrics>(cfg.general.prometheus_port);
        received_counter = &metrics->counter("ldm_ingestion_mapem_total", "Total MAPEM messages ingested");
        forwarded_counter = &metrics->counter("ldm_ingestion_updates_total", "Total LDM updates published");
    }

    auto transport = ldm::messaging::createTransport(cfg.messaging);
    auto* transport_ptr = transport.get();
    const auto input_topic = cfg.ingestion.input_topic.empty() ? std::string("generation/mapem") : cfg.ingestion.input_topic;
    const auto output_topic = cfg.ingestion.output_topic.empty() ? std::string("ldm/updates") : cfg.ingestion.output_topic;

    transport->subscribe(input_topic, [&, transport_ptr, output_topic](const std::string& topic, const std::string& payload) {
        if (received_counter) {
            received_counter->Increment();
        }
        spdlog::debug("[LDM-Ingestion] MAPEM payload on {} -> {}", topic, output_topic);
        std::ostringstream enriched;
        enriched << "ldm.update=" << payload << ";station=" << cfg.ingestion.station_id;
        transport_ptr->publish(output_topic, enriched.str());
        if (forwarded_counter) {
            forwarded_counter->Increment();
        }
    });

    const auto interval = std::chrono::milliseconds(std::max(100, cfg.ingestion.simulation_interval_ms));
    int sequence = 0;
    while (true) {
        if (cfg.ingestion.simulate_input) {
            std::ostringstream sample;
            sample << "mapem_seq=" << sequence << ";station=" << cfg.ingestion.station_id
                   << ";objects=" << ((sequence % 4) + 1);
            spdlog::debug("[LDM-Ingestion] Simulating MAPEM seq={} topic={}", sequence, input_topic);
            transport->publish(input_topic, sample.str());
            ++sequence;
        }
        std::this_thread::sleep_for(interval);
    }
    return 0;
}
