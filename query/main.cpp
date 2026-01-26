#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>

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

struct QueryConfig {
    int station_type{15};
    int station_id{0};
    bool performance_logs{false};
    std::string request_topic{"ldm/query"};
    std::string response_topic{"ldm/response"};
    bool auto_request{true};
    int request_interval_ms{4000};
    double station_latitude{0.0};
    double station_longitude{0.0};
};

struct QueryConfigBundle {
    GeneralConfig general;
    ldm::messaging::MessagingConfig messaging;
    QueryConfig query;
};

std::string readNonEmpty(const INIReader& reader,
                         const std::string& section,
                         const std::string& key,
                         const std::string& fallback) {
    auto value = reader.Get(section, key, fallback);
    return value.empty() ? fallback : value;
}

QueryConfigBundle readConfig(const std::string& path) {
    QueryConfigBundle cfg;
    INIReader reader(path);
    if (reader.ParseError() < 0) {
        spdlog::warn("[LDM-Query] Could not load {}; using defaults", path);
        return cfg;
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

    cfg.query.station_type = reader.GetInteger("query", "station_type", cfg.query.station_type);
    cfg.query.station_id = reader.GetInteger("query", "station_id", cfg.query.station_id);
    cfg.query.performance_logs = reader.GetBoolean("query", "performance_logs", cfg.query.performance_logs);
    cfg.query.request_topic = readNonEmpty(reader, "query", "request_topic", cfg.query.request_topic);
    cfg.query.response_topic = readNonEmpty(reader, "query", "response_topic", cfg.query.response_topic);
    cfg.query.auto_request = reader.GetBoolean("query", "auto_request", cfg.query.auto_request);
    cfg.query.request_interval_ms = reader.GetInteger("query", "request_interval_ms", cfg.query.request_interval_ms);
    cfg.query.station_latitude = reader.GetReal("query", "station_latitude", cfg.query.station_latitude);
    cfg.query.station_longitude = reader.GetReal("query", "station_longitude", cfg.query.station_longitude);

    return cfg;
}

}  // namespace

int main() {
    auto cfg = readConfig("/config.ini");
    spdlog::set_level(cfg.general.debug ? spdlog::level::debug : spdlog::level::info);
    spdlog::info("[LDM-Query] Starting query service");

    std::unique_ptr<ldm::metrics::Metrics> metrics;
    ldm::metrics::Counter* issued_queries = nullptr;
    if (cfg.general.prometheus) {
        metrics = std::make_unique<ldm::metrics::Metrics>(cfg.general.prometheus_port);
        issued_queries = &metrics->counter("ldm_query_requests_total", "Total queries published");
    }

    auto transport = ldm::messaging::createTransport(cfg.messaging);
    const auto request_topic = cfg.query.request_topic;
    const auto response_topic = cfg.query.response_topic;

    transport->subscribe(response_topic, [](const std::string& topic, const std::string& payload) {
        spdlog::info("[LDM-Query] Response on {} -> {}", topic, payload);
    });

    const auto interval = std::chrono::milliseconds(std::max(250, cfg.query.request_interval_ms));
    int sequence = 0;
    while (true) {
        if (cfg.query.auto_request) {
            std::ostringstream query_payload;
            query_payload << "request_id=" << sequence << ";lat=" << cfg.query.station_latitude
                          << ";lon=" << cfg.query.station_longitude;
            spdlog::debug("[LDM-Query] Publishing request {} on {}", sequence, request_topic);
            transport->publish(request_topic, query_payload.str());
            if (issued_queries) {
                issued_queries->Increment();
            }
            ++sequence;
        }
        std::this_thread::sleep_for(interval);
    }
    return 0;
}
