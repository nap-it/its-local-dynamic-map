#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#include "config_reader.hpp"
#include "metrics.hpp"
#include "common/messaging/transport.hpp"

namespace {

struct GeneralConfig {
    bool debug{true};
    bool prometheus{true};
    int prometheus_port{9200};
};

struct IngestionConfig {
    int station_type{15};
    int station_id{0};
    bool performance_logs{false};
};

struct CategoryConfig {
    std::string name;
    std::string label;
    bool enable{false};
    std::vector<std::string> input_topics;
    std::vector<std::string> output_topics;
    bool simulate_input{false};
    int simulation_interval_ms{2000};
    std::string file_path;
};

struct IngestionConfigBundle {
    GeneralConfig general;
    ldm::messaging::MessagingConfig messaging;
    IngestionConfig ingestion;
    CategoryConfig mapem;
    CategoryConfig vam;
};

struct CategoryState {
    CategoryConfig config;
    std::string simulated_payload;
    ldm::metrics::Counter* received_counter{nullptr};
};

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string readNonEmpty(const INIReader& reader,
                         const std::string& section,
                         const std::string& key,
                         const std::string& fallback) {
    auto value = reader.Get(section, key, fallback);
    return value.empty() ? fallback : value;
}

std::vector<std::string> splitTopics(const std::string& value) {
    std::string normalized = value;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::istringstream stream(normalized);
    std::vector<std::string> topics;
    std::string token;
    while (stream >> token) {
        topics.push_back(token);
    }
    return topics;
}

IngestionConfigBundle readConfig(const std::string& path) {
    IngestionConfigBundle cfg;
    INIReader reader(path);
    if (reader.ParseError() < 0) {
        spdlog::warn("[LDM-Ingestion] Could not load {}; using defaults", path);
    }

    cfg.general.debug = reader.GetBoolean("general", "debug", cfg.general.debug);
    cfg.general.prometheus = reader.GetBoolean("general", "prometheus", cfg.general.prometheus);
    cfg.general.prometheus_port = reader.GetInteger("general", "prometheus_port", cfg.general.prometheus_port);

    cfg.messaging.backend = readNonEmpty(reader, "messaging", "backend", cfg.messaging.backend);
    cfg.messaging.domain_id = reader.GetInteger("messaging", "domain_id", cfg.messaging.domain_id);
    cfg.messaging.mqtt_host = readNonEmpty(reader, "messaging", "mqtt_host", cfg.messaging.mqtt_host);
    cfg.messaging.mqtt_port = reader.GetInteger("messaging", "mqtt_port", cfg.messaging.mqtt_port);
    cfg.messaging.mqtt_username = reader.Get("messaging", "mqtt_username", cfg.messaging.mqtt_username);
    cfg.messaging.mqtt_password = reader.Get("messaging", "mqtt_password", cfg.messaging.mqtt_password);
    cfg.messaging.zenoh_endpoint = readNonEmpty(reader, "messaging", "zenoh_endpoint", cfg.messaging.zenoh_endpoint);

    cfg.ingestion.station_type = reader.GetInteger("ingestion", "station_type", cfg.ingestion.station_type);
    cfg.ingestion.station_id = reader.GetInteger("ingestion", "station_id", cfg.ingestion.station_id);
    cfg.ingestion.performance_logs = reader.GetBoolean("ingestion", "performance_logs", cfg.ingestion.performance_logs);

    cfg.mapem.name = "mapem";
    cfg.mapem.label = "MAPEM";
    cfg.mapem.enable = reader.GetBoolean("mapem", "enable", true);
    cfg.mapem.input_topics = splitTopics(readNonEmpty(reader, "mapem", "input_topics", "generation/mapem"));
    cfg.mapem.output_topics = splitTopics(readNonEmpty(reader, "mapem", "output_topics", "ldm/updates"));
    cfg.mapem.simulate_input = reader.GetBoolean("mapem", "simulate_input", true);
    cfg.mapem.simulation_interval_ms = reader.GetInteger("mapem", "simulation_interval_ms", 2000);
    cfg.mapem.file_path = reader.Get("mapem", "file_path", "");

    cfg.vam.name = "vam";
    cfg.vam.label = "VAM";
    cfg.vam.enable = reader.GetBoolean("vam", "enable", false);
    cfg.vam.input_topics = splitTopics(readNonEmpty(reader, "vam", "input_topics", "generation/vam"));
    cfg.vam.output_topics = splitTopics(readNonEmpty(reader, "vam", "output_topics", "ldm/updates"));
    cfg.vam.simulate_input = reader.GetBoolean("vam", "simulate_input", false);
    cfg.vam.simulation_interval_ms = reader.GetInteger("vam", "simulation_interval_ms", 2000);
    cfg.vam.file_path = reader.Get("vam", "file_path", "");

    return cfg;
}

std::string buildSyntheticPayload(const CategoryConfig& cfg, int sequence, int station_id) {
    std::ostringstream sample;
    sample << cfg.name << "_seq=" << sequence << ";station=" << station_id;
    if (cfg.name == "mapem") {
        sample << ";objects=" << ((sequence % 4) + 1);
    }
    return sample.str();
}

void publishUpdate(const CategoryConfig& cfg,
                   const std::string& payload,
                   const std::string& source_topic,
                   ldm::messaging::Transport* transport,
                   int station_id,
                   ldm::metrics::Counter* received_counter,
                   ldm::metrics::Counter* forwarded_counter) {
    if (received_counter) {
        received_counter->Increment();
    }
    if (cfg.output_topics.empty()) {
        spdlog::warn("[LDM-Ingestion] {} output topics are empty", cfg.label);
        return;
    }

    std::ostringstream enriched;
    enriched << "ldm.update=" << payload << ";station=" << station_id;
    const auto message = enriched.str();

    for (const auto& output_topic : cfg.output_topics) {
        spdlog::debug("[LDM-Ingestion] {} payload on {} -> {}", cfg.label, source_topic, output_topic);
        transport->publish(output_topic, message);
        if (forwarded_counter) {
            forwarded_counter->Increment();
        }
    }
}

void runSimulation(const CategoryConfig& cfg,
                   const std::string& simulated_payload,
                   ldm::messaging::Transport* transport,
                   int station_id,
                   ldm::metrics::Counter* forwarded_counter) {
    if (cfg.output_topics.empty()) {
        spdlog::warn("[LDM-Ingestion] {} simulation skipped, no output topics", cfg.label);
        return;
    }

    const auto interval = std::chrono::milliseconds(std::max(100, cfg.simulation_interval_ms));
    int sequence = 0;
    while (true) {
        std::string payload;
        if (!simulated_payload.empty()) {
            payload = simulated_payload;
            spdlog::debug("[LDM-Ingestion] Simulating {} from file '{}' seq={}", cfg.label, cfg.file_path, sequence);
        } else {
            payload = buildSyntheticPayload(cfg, sequence, station_id);
            spdlog::debug("[LDM-Ingestion] Simulating {} seq={}", cfg.label, sequence);
        }

        std::ostringstream enriched;
        enriched << "ldm.update=" << payload << ";station=" << station_id;
        const auto message = enriched.str();
        for (const auto& output_topic : cfg.output_topics) {
            transport->publish(output_topic, message);
            spdlog::debug("[LDM-Ingestion] Published simulated {} update to {}", cfg.label, output_topic);
            if (forwarded_counter) {
                forwarded_counter->Increment();
            }
        }
        ++sequence;
        std::this_thread::sleep_for(interval);
    }
}

}  // namespace

int main() {
    auto cfg = readConfig("/config.ini");
    spdlog::set_level(cfg.general.debug ? spdlog::level::debug : spdlog::level::info);
    spdlog::info("[LDM-Ingestion] Starting ingestion service");

    std::unique_ptr<ldm::metrics::Metrics> metrics;
    ldm::metrics::Counter* mapem_counter = nullptr;
    ldm::metrics::Counter* vam_counter = nullptr;
    ldm::metrics::Counter* forwarded_counter = nullptr;
    if (cfg.general.prometheus) {
        metrics = std::make_unique<ldm::metrics::Metrics>(cfg.general.prometheus_port);
        mapem_counter = &metrics->counter("ldm_ingestion_mapem_total", "Total MAPEM messages ingested");
        vam_counter = &metrics->counter("ldm_ingestion_vam_total", "Total VAM messages ingested");
        forwarded_counter = &metrics->counter("ldm_ingestion_updates_total", "Total LDM updates published");
    }

    auto transport = ldm::messaging::createTransport(cfg.messaging);
    auto* transport_ptr = transport.get();

    std::vector<CategoryState> categories;
    categories.push_back(CategoryState{cfg.mapem, "", mapem_counter});
    categories.push_back(CategoryState{cfg.vam, "", vam_counter});

    for (auto& category : categories) {
        if (!category.config.enable) {
            continue;
        }

        if (category.config.simulate_input && !category.config.file_path.empty()) {
            category.simulated_payload = readFile(category.config.file_path);
            if (category.simulated_payload.empty()) {
                spdlog::warn("[LDM-Ingestion] Could not read {} file '{}'; using synthetic payloads",
                             category.config.label, category.config.file_path);
            } else {
                spdlog::info("[LDM-Ingestion] Loaded {} file '{}'", category.config.label, category.config.file_path);
            }
        }

        for (const auto& input_topic : category.config.input_topics) {
            if (input_topic.empty()) {
                continue;
            }
            CategoryState* category_ptr = &category;
            transport->subscribe(input_topic, [category_ptr, transport_ptr, &cfg, forwarded_counter](
                                                   const std::string& topic, const std::string& payload) {
                publishUpdate(category_ptr->config, payload, topic, transport_ptr,
                              cfg.ingestion.station_id, category_ptr->received_counter, forwarded_counter);
            });
        }

        if (category.config.simulate_input) {
            std::thread([category, transport_ptr, station_id = cfg.ingestion.station_id, forwarded_counter]() {
                runSimulation(category.config, category.simulated_payload, transport_ptr, station_id, forwarded_counter);
            }).detach();
        }
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
