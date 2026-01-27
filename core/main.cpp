#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <spdlog/spdlog.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "config_reader.hpp"
#include "locator.hpp"
#include "mapem_message.hpp"
#include "metrics.hpp"
#include "ldm_store.hpp"
#include "common/messaging/transport.hpp"

namespace rj = rapidjson;

namespace {

std::atomic<bool> g_running{true};

void handleSignal(int) {
    g_running.store(false);
}

void installSignalHandlers() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
}

struct GeneralConfig {
    bool debug{true};
    bool prometheus{true};
    int prometheus_port{9200};
};

struct CoreConfig {
    double aom_radius_m{1000.0};
    int static_map_validity_seconds{3600};
    std::string ingest_topic{"ldm/updates"};
    std::string query_topic{"ldm/query"};
    std::string response_topic{"ldm/response"};
};

struct CoreConfigBundle {
    GeneralConfig general;
    ldm::messaging::MessagingConfig messaging;
    ldm::locator::LocatorConfig locator;
    CoreConfig ldm_core;
};

std::string readNonEmpty(const INIReader& reader,
                         const std::string& section,
                         const std::string& key,
                         const std::string& fallback) {
    auto value = reader.Get(section, key, fallback);
    return value.empty() ? fallback : value;
}

CoreConfigBundle readConfig(const std::string& path) {
    CoreConfigBundle cfg;
    INIReader reader(path);
    if (reader.ParseError() < 0) {
        spdlog::warn("[LDM-Core] Could not load {}; using defaults", path);
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

    cfg.locator.station_type = reader.GetInteger("locator", "station_type", cfg.locator.station_type);
    cfg.locator.station_id = reader.GetInteger("locator", "station_id", cfg.locator.station_id);
    cfg.locator.station_latitude = reader.GetReal("locator", "station_latitude", cfg.locator.station_latitude);
    cfg.locator.station_longitude = reader.GetReal("locator", "station_longitude", cfg.locator.station_longitude);
    cfg.locator.location_provider = readNonEmpty(reader, "locator", "location_provider", cfg.locator.location_provider);
    cfg.locator.mqtt_host = reader.Get("locator", "mqtt_host", cfg.locator.mqtt_host);
    cfg.locator.mqtt_port = reader.GetInteger("locator", "mqtt_port", cfg.locator.mqtt_port);
    cfg.locator.mqtt_topic = reader.Get("locator", "mqtt_topic", cfg.locator.mqtt_topic);
    cfg.locator.domain_id = reader.GetInteger("locator", "domain_id", cfg.locator.domain_id);
    cfg.locator.dds_topic = reader.Get("locator", "dds_topic", cfg.locator.dds_topic);
    cfg.locator.update_interval_ms = reader.GetInteger("locator", "locator_update_interval_ms", cfg.locator.update_interval_ms);

    cfg.ldm_core.aom_radius_m = reader.GetReal("ldm-core", "aom_radius_m", cfg.ldm_core.aom_radius_m);
    cfg.ldm_core.static_map_validity_seconds = reader.GetInteger("ldm-core", "static_map_validity_seconds",
                                                                 cfg.ldm_core.static_map_validity_seconds);
    cfg.ldm_core.ingest_topic = readNonEmpty(reader, "ldm-core", "ingest_topic", cfg.ldm_core.ingest_topic);
    cfg.ldm_core.query_topic = readNonEmpty(reader, "ldm-core", "query_topic", cfg.ldm_core.query_topic);
    cfg.ldm_core.response_topic = readNonEmpty(reader, "ldm-core", "response_topic", cfg.ldm_core.response_topic);

    return cfg;
}

std::string trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool isJsonObject(const std::string& s) {
    const auto trimmed = trim(s);
    return !trimmed.empty() && trimmed.front() == '{' && trimmed.back() == '}';
}

std::string jsonEscape(const std::string& input) {
    std::ostringstream out;
    for (unsigned char c : input) {
        switch (c) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (c < 0x20) {
                    out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c) << std::dec;
                } else {
                    out << static_cast<char>(c);
                }
                break;
        }
    }
    return out.str();
}

}  // namespace

int main() {
    using namespace std::chrono_literals;
    installSignalHandlers();
    auto cfg = readConfig("/config.ini");

    spdlog::set_level(cfg.general.debug ? spdlog::level::debug : spdlog::level::info);
    spdlog::info("[LDM-Core] Starting LDM core service");

    ldm::locator::Locator locator(cfg.locator);
    locator.run();
    auto [lat, lon] = locator.currentPosition();

    ldm::model::LdmStore store(cfg.ldm_core.aom_radius_m);
    store.setStationReference(lat, lon);

    std::thread locator_update_thread;
    if (locator.isDynamic() && cfg.locator.update_interval_ms > 0) {
        const auto update_interval = std::chrono::milliseconds(std::max(250, cfg.locator.update_interval_ms));
        locator_update_thread = std::thread([&store, &locator, update_interval]() {
            while (g_running.load()) {
                auto [current_lat, current_lon] = locator.currentPosition();
                store.setStationReference(current_lat, current_lon);
                std::this_thread::sleep_for(update_interval);
            }
        });
    }

    std::unique_ptr<ldm::metrics::Metrics> metrics;
    ldm::metrics::Gauge* objects_gauge = nullptr;
    ldm::metrics::Counter* served_queries = nullptr;
    if (cfg.general.prometheus) {
        metrics = std::make_unique<ldm::metrics::Metrics>(cfg.general.prometheus_port);
        objects_gauge = &metrics->gauge("ldm_objects_total", "Current number of objects in the LDM");
        served_queries = &metrics->counter("ldm_core_queries_total", "Total queries served by LDM core");
    }

    auto transport = ldm::messaging::createTransport(cfg.messaging);
    auto* transport_ptr = transport.get();
    std::atomic<size_t> object_counter{0};
    std::mutex mapem_mutex;
    std::string last_mapem_json;
    auto last_mapem_ts = std::chrono::system_clock::time_point{};
    const auto ingest_topic = cfg.ldm_core.ingest_topic;
    const auto query_topic = cfg.ldm_core.query_topic;
    const auto response_topic = cfg.ldm_core.response_topic;

    transport->subscribe(ingest_topic, [&, objects_gauge](const std::string& topic, const std::string& payload) {
        auto count = object_counter.fetch_add(1) + 1;
        if (objects_gauge) {
            objects_gauge->Set(static_cast<double>(count));
        }
        std::string mapem_json;
        if (ldm::mapem::ExtractNormalizedMapem(payload, mapem_json)) {
            std::lock_guard<std::mutex> lock(mapem_mutex);
            last_mapem_json = mapem_json;
            last_mapem_ts = std::chrono::system_clock::now();
            spdlog::debug("[LDM-Core] Stored MAPEM update ({} bytes)", last_mapem_json.size());
        }
        spdlog::debug("[LDM-Core] Received update on {} payload={} (total={})", topic, payload, count);
    });

    transport->subscribe(query_topic, [&, transport_ptr, response_topic, served_queries](const std::string& topic,
                                                                                       const std::string& payload) {
        if (served_queries) {
            served_queries->Increment();
        }
        std::string mapem_snapshot;
        double mapem_age_s = -1.0;
        {
            std::lock_guard<std::mutex> lock(mapem_mutex);
            mapem_snapshot = last_mapem_json;
            if (!mapem_snapshot.empty() && last_mapem_ts.time_since_epoch().count() > 0) {
                mapem_age_s = std::chrono::duration<double>(std::chrono::system_clock::now() - last_mapem_ts).count();
            }
        }

        const bool has_mapem = !mapem_snapshot.empty();
        ldm::mapem::Filter filter;
        if (isJsonObject(payload)) {
            rj::Document request_doc;
            request_doc.Parse(payload.c_str());
            if (!request_doc.HasParseError() && request_doc.IsObject()) {
                filter = ldm::mapem::ParseFilter(request_doc);
            }
        }
        std::string mapem_payload;
        if (has_mapem) {
            if (!ldm::mapem::BuildFilteredMapem(mapem_snapshot, filter, mapem_payload)) {
                mapem_payload = mapem_snapshot;
            }
        }
        std::ostringstream response;
        response << "{\"has_mapem\":" << (has_mapem ? "true" : "false");
        if (has_mapem) {
            response << ",\"mapem\":" << mapem_payload;
        }
        if (!payload.empty()) {
            if (isJsonObject(payload)) {
                response << ",\"request\":" << trim(payload);
            } else {
                response << ",\"request\":\"" << jsonEscape(payload) << "\"";
            }
        }
        if (mapem_age_s >= 0.0) {
            response << ",\"mapem_age_s\":" << mapem_age_s;
        }
        response << "}";

        spdlog::debug("[LDM-Core] Responding to query payload={} via {}", payload, topic);
        transport_ptr->publish(response_topic, response.str());
    });

    while (g_running.load()) {
        store.runMaintenance();
        std::this_thread::sleep_for(5s);
    }
    if (locator_update_thread.joinable()) {
        locator_update_thread.join();
    }
    return 0;
}
