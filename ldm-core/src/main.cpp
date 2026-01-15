#include <atomic>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

#include "../../common/config/config.hpp"
#include "../../common/ldm-model/ldm_store.hpp"
#include "../../common/locator/locator.hpp"
#include "../../common/messaging/transport.hpp"
#include "../../common/metrics/metrics.hpp"

namespace {

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

std::string extractMapemJson(const std::string& payload) {
    const auto trimmed = trim(payload);
    if (trimmed.empty()) {
        return {};
    }
    const auto first_brace = trimmed.find('{');
    const auto last_brace = trimmed.rfind('}');
    if (first_brace == std::string::npos || last_brace == std::string::npos || last_brace <= first_brace) {
        return {};
    }
    return trim(trimmed.substr(first_brace, last_brace - first_brace + 1));
}

}  // namespace

int main() {
    using namespace std::chrono_literals;
    auto cfg = ldm::config::loadConfig("/config.ini");

    spdlog::set_level(cfg.general.debug ? spdlog::level::debug : spdlog::level::info);
    spdlog::info("[LDM-Core] Starting LDM core service");

    ldm::locator::Locator locator(cfg.locator);
    locator.run();
    auto [lat, lon] = locator.currentPosition();

    ldm::model::LdmStore store(cfg.ldm_core.aom_radius_m);
    store.setStationReference(lat, lon);

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
    const auto ingest_topic = cfg.ldm_core.ingest_topic.empty() ? std::string("ldm/updates") : cfg.ldm_core.ingest_topic;
    const auto query_topic = cfg.ldm_core.query_topic.empty() ? std::string("ldm/query") : cfg.ldm_core.query_topic;
    const auto response_topic = cfg.ldm_core.response_topic.empty() ? std::string("ldm/response") : cfg.ldm_core.response_topic;

    transport->subscribe(ingest_topic, [&, objects_gauge](const std::string& topic, const std::string& payload) {
        auto count = object_counter.fetch_add(1) + 1;
        if (objects_gauge) {
            objects_gauge->Set(static_cast<double>(count));
        }
        const auto mapem_json = extractMapemJson(payload);
        if (!mapem_json.empty()) {
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
        std::ostringstream response;
        response << "{\"has_mapem\":" << (has_mapem ? "true" : "false");
        if (has_mapem) {
            response << ",\"mapem\":" << mapem_snapshot;
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

    while (true) {
        store.runMaintenance();
        std::this_thread::sleep_for(5s);
    }
    return 0;
}
