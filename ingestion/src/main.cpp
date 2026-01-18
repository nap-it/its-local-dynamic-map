#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

#include <spdlog/spdlog.h>

#include "../../common/config/config.hpp"
#include "../../common/locator/locator.hpp"
#include "../../common/messaging/transport.hpp"
#include "../../common/metrics/metrics.hpp"

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

int main() {
    using namespace std::chrono_literals;
    auto cfg = ldm::config::loadConfig("/config.ini");
    spdlog::set_level(cfg.general.debug ? spdlog::level::debug : spdlog::level::info);
    spdlog::info("[LDM-Ingestion] Starting MAPEM ingestion service");

    ldm::locator::Locator locator(cfg.locator);
    locator.run();
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

    std::string simulated_mapem;
    if (cfg.ingestion.simulate_input && !cfg.ingestion.mapem_file.empty()) {
        simulated_mapem = readFile(cfg.ingestion.mapem_file);
        if (simulated_mapem.empty()) {
            spdlog::warn("[LDM-Ingestion] Could not read MAPEM file '{}'; using synthetic payloads", cfg.ingestion.mapem_file);
        } else {
            spdlog::info("[LDM-Ingestion] Loaded MAPEM file '{}'", cfg.ingestion.mapem_file);
        }
    }

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
            std::string payload;
            if (!simulated_mapem.empty()) {
                payload = simulated_mapem;
                spdlog::debug("[LDM-Ingestion] Simulating MAPEM from file '{}' seq={} topic={}",
                              cfg.ingestion.mapem_file, sequence, input_topic);
            } else {
                std::ostringstream sample;
                sample << "mapem_seq=" << sequence << ";station=" << cfg.ingestion.station_id
                       << ";objects=" << ((sequence % 4) + 1);
                payload = sample.str();
                spdlog::debug("[LDM-Ingestion] Simulating MAPEM seq={} topic={}", sequence, input_topic);
            }
            
            // In simulation mode, publish directly to the output topic
            // since DDS doesn't deliver messages back to the same participant
            std::ostringstream enriched;
            enriched << "ldm.update=" << payload << ";station=" << cfg.ingestion.station_id;
            transport->publish(output_topic, enriched.str());
            spdlog::debug("[LDM-Ingestion] Published simulated update to {}", output_topic);
            if (forwarded_counter) {
                forwarded_counter->Increment();
            }
            ++sequence;
        }
        std::this_thread::sleep_for(interval);
    }
    return 0;
}
