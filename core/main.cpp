#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cctype>
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
#include "vam_message.hpp"
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
    int map_validity_seconds{3600};
    int vam_validity_seconds{5};
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

struct QueryRequest {
    std::string query_type{"mapem"};
    uint64_t request_id{0};
    bool has_request_id{false};
    bool has_aoi{false};
    double aoi_lat{0.0};
    double aoi_lon{0.0};
    double aoi_radius_m{0.0};
    bool has_mapem_detail{false};
    ldm::mapem::Filter mapem_filter{};
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
    auto map_validity_raw = reader.Get("ldm-core", "map_validity_seconds", "");
    if (!map_validity_raw.empty()) {
        try {
            cfg.ldm_core.map_validity_seconds = std::stoi(map_validity_raw);
        } catch (...) {
        }
    } else {
        cfg.ldm_core.map_validity_seconds = reader.GetInteger("ldm-core", "static_map_validity_seconds",
                                                              cfg.ldm_core.map_validity_seconds);
    }
    cfg.ldm_core.vam_validity_seconds = reader.GetInteger("ldm-core", "vam_validity_seconds",
                                                          cfg.ldm_core.vam_validity_seconds);
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

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string extractUpdatePayload(const std::string& message) {
    const std::string marker = "ldm.update=";
    auto pos = message.find(marker);
    if (pos == std::string::npos) {
        return trim(message);
    }
    auto start = pos + marker.size();
    auto end = message.find(";station=", start);
    if (end == std::string::npos) {
        return trim(message.substr(start));
    }
    return trim(message.substr(start, end - start));
}

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::chrono::system_clock::time_point timePointFromSeconds(double timestamp_s) {
    auto duration = std::chrono::duration<double>(timestamp_s);
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(duration));
}

bool parseTimestampSeconds(const std::string& payload, double& timestamp_s) {
    if (!isJsonObject(payload)) {
        return false;
    }
    rj::Document doc;
    doc.Parse(payload.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        return false;
    }
    if (doc.HasMember("timestamp") && doc["timestamp"].IsNumber()) {
        timestamp_s = doc["timestamp"].GetDouble();
        return true;
    }
    if (doc.HasMember("test") && doc["test"].IsObject()) {
        const auto& test = doc["test"];
        if (test.HasMember("json_timestamp") && test["json_timestamp"].IsNumber()) {
            timestamp_s = test["json_timestamp"].GetDouble();
            return true;
        }
    }
    return false;
}

std::string toJsonString(const rj::Value& value) {
    rj::StringBuffer buffer;
    rj::Writer<rj::StringBuffer> writer(buffer);
    value.Accept(writer);
    return buffer.GetString();
}

bool extractIntersectionId(const rj::Value& inter, int& intersection_id) {
    if (inter.HasMember("id")) {
        const auto& id_val = inter["id"];
        if (id_val.IsInt()) {
            intersection_id = id_val.GetInt();
            return true;
        }
        if (id_val.IsObject() && id_val.HasMember("id") && id_val["id"].IsInt()) {
            intersection_id = id_val["id"].GetInt();
            return true;
        }
    }
    if (inter.HasMember("intersectionId") && inter["intersectionId"].IsInt()) {
        intersection_id = inter["intersectionId"].GetInt();
        return true;
    }
    return false;
}

bool extractRefPoint(const rj::Value& inter, double& lat, double& lon) {
    if (!inter.HasMember("refPoint") || !inter["refPoint"].IsObject()) {
        return false;
    }
    const auto& ref = inter["refPoint"];
    if (!ref.HasMember("lat") || !ref["lat"].IsNumber()) {
        return false;
    }
    lat = ref["lat"].GetDouble();
    if (ref.HasMember("long") && ref["long"].IsNumber()) {
        lon = ref["long"].GetDouble();
        return true;
    }
    if (ref.HasMember("lon") && ref["lon"].IsNumber()) {
        lon = ref["lon"].GetDouble();
        return true;
    }
    return false;
}

bool parseLegacyQuery(const std::string& payload, QueryRequest& out) {
    std::istringstream stream(payload);
    std::string token;
    while (std::getline(stream, token, ';')) {
        auto pos = token.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        auto key = trim(token.substr(0, pos));
        auto value = trim(token.substr(pos + 1));
        if (key == "query_type") {
            out.query_type = toLower(value);
        } else if (key == "request_id") {
            try {
                out.request_id = static_cast<uint64_t>(std::stoull(value));
                out.has_request_id = true;
            } catch (...) {
            }
        } else if (key == "lat") {
            try {
                out.aoi_lat = std::stod(value);
                out.has_aoi = true;
            } catch (...) {
            }
        } else if (key == "lon") {
            try {
                out.aoi_lon = std::stod(value);
                out.has_aoi = true;
            } catch (...) {
            }
        } else if (key == "radius" || key == "radius_m") {
            try {
                out.aoi_radius_m = std::stod(value);
            } catch (...) {
            }
        }
    }
    return out.has_aoi || out.has_request_id || !out.query_type.empty();
}

bool parseQueryRequest(const std::string& payload, QueryRequest& out) {
    out = QueryRequest{};
    const auto trimmed = trim(payload);
    if (trimmed.empty()) {
        return false;
    }
    if (!isJsonObject(trimmed)) {
        return parseLegacyQuery(trimmed, out);
    }

    rj::Document doc;
    doc.Parse(trimmed.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        return false;
    }
    if (doc.HasMember("query_type") && doc["query_type"].IsString()) {
        out.query_type = toLower(doc["query_type"].GetString());
    }
    if (doc.HasMember("request_id")) {
        const auto& id_val = doc["request_id"];
        if (id_val.IsUint64()) {
            out.request_id = id_val.GetUint64();
            out.has_request_id = true;
        } else if (id_val.IsInt64()) {
            out.request_id = static_cast<uint64_t>(id_val.GetInt64());
            out.has_request_id = true;
        }
    }
    if (doc.HasMember("aoi") && doc["aoi"].IsObject()) {
        const auto& aoi = doc["aoi"];
        if (aoi.HasMember("lat") && aoi["lat"].IsNumber()) {
            out.aoi_lat = aoi["lat"].GetDouble();
            out.has_aoi = true;
        }
        if (aoi.HasMember("lon") && aoi["lon"].IsNumber()) {
            out.aoi_lon = aoi["lon"].GetDouble();
            out.has_aoi = true;
        }
        if (aoi.HasMember("radius_m") && aoi["radius_m"].IsNumber()) {
            out.aoi_radius_m = aoi["radius_m"].GetDouble();
        }
    } else {
        if (doc.HasMember("lat") && doc["lat"].IsNumber()) {
            out.aoi_lat = doc["lat"].GetDouble();
            out.has_aoi = true;
        }
        if (doc.HasMember("lon") && doc["lon"].IsNumber()) {
            out.aoi_lon = doc["lon"].GetDouble();
            out.has_aoi = true;
        }
        if (doc.HasMember("radius_m") && doc["radius_m"].IsNumber()) {
            out.aoi_radius_m = doc["radius_m"].GetDouble();
        }
    }
    if ((doc.HasMember("mapem_detail") && doc["mapem_detail"].IsObject()) ||
        (doc.HasMember("mapem_filter") && doc["mapem_filter"].IsObject())) {
        out.mapem_filter = ldm::mapem::ParseFilter(doc);
        out.has_mapem_detail = true;
    }
    return true;
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
    const auto ingest_topic = cfg.ldm_core.ingest_topic;
    const auto query_topic = cfg.ldm_core.query_topic;
    const auto response_topic = cfg.ldm_core.response_topic;

    transport->subscribe(ingest_topic, [&, objects_gauge](const std::string& topic, const std::string& payload) {
        const auto update_payload = extractUpdatePayload(payload);
        std::string mapem_json;
        if (ldm::mapem::ExtractNormalizedMapem(update_payload, mapem_json)) {
            rj::Document mapem_doc;
            mapem_doc.Parse(mapem_json.c_str());
            if (mapem_doc.HasParseError() || !mapem_doc.IsObject()) {
                spdlog::warn("[LDM-Core] MAPEM payload parsed but normalized JSON invalid");
                return;
            }
            double timestamp_s = 0.0;
            bool has_ts = parseTimestampSeconds(update_payload, timestamp_s);
            auto timestamp = (has_ts && timestamp_s > 0.0) ? timePointFromSeconds(timestamp_s)
                                                           : std::chrono::system_clock::now();
            const auto validity = std::chrono::seconds(std::max(0, cfg.ldm_core.map_validity_seconds));
            int stored = 0;
            if (mapem_doc.HasMember("intersections") && mapem_doc["intersections"].IsArray()) {
                for (const auto& inter : mapem_doc["intersections"].GetArray()) {
                    if (!inter.IsObject()) {
                        continue;
                    }
                    int intersection_id = 0;
                    double ref_lat = 0.0;
                    double ref_lon = 0.0;
                    if (!extractIntersectionId(inter, intersection_id)) {
                        continue;
                    }
                    if (!extractRefPoint(inter, ref_lat, ref_lon)) {
                        continue;
                    }
                    ldm::model::LdmDataObject obj;
                    obj.id = {ldm::model::LdmDataType::MAP_INTERSECTION, static_cast<uint64_t>(intersection_id)};
                    obj.type = ldm::model::LdmDataType::MAP_INTERSECTION;
                    obj.geom.type = ldm::model::Geometry::Type::POINT;
                    obj.geom.lats = {ref_lat};
                    obj.geom.lons = {ref_lon};
                    obj.timestamp = timestamp;
                    obj.time_validity = validity;
                    obj.payload = toJsonString(inter);
                    store.upsertObject(obj);
                    ++stored;
                }
            }
            if (objects_gauge) {
                objects_gauge->Set(static_cast<double>(store.size()));
            }
            spdlog::debug("[LDM-Core] Stored MAPEM update intersections={} payload_bytes={}", stored, mapem_json.size());
            return;
        }

        ldm::vam::VamSummary summary;
        if (ldm::vam::ExtractVamSummary(update_payload, summary)) {
            auto timestamp = summary.timestamp_s > 0.0 ? timePointFromSeconds(summary.timestamp_s)
                                                       : std::chrono::system_clock::now();
            ldm::model::LdmDataObject obj;
            obj.id = {ldm::model::LdmDataType::VAM, summary.station_id};
            obj.type = ldm::model::LdmDataType::VAM;
            obj.geom.type = ldm::model::Geometry::Type::POINT;
            obj.geom.lats = {summary.lat};
            obj.geom.lons = {summary.lon};
            obj.timestamp = timestamp;
            obj.time_validity = std::chrono::seconds(std::max(0, cfg.ldm_core.vam_validity_seconds));
            obj.payload = update_payload;
            if (summary.has_orientation) {
                auto attrs = std::make_shared<rj::Document>();
                attrs->SetObject();
                auto& alloc = attrs->GetAllocator();
                attrs->AddMember("orientation", summary.orientation, alloc);
                obj.attributes = attrs;
            }
            store.upsertObject(obj);
            if (objects_gauge) {
                objects_gauge->Set(static_cast<double>(store.size()));
            }
            spdlog::debug("[LDM-Core] Stored VAM update station_id={}", summary.station_id);
            return;
        }

        spdlog::debug("[LDM-Core] Ignored update on {} payload={}", topic, update_payload);
    });

    transport->subscribe(query_topic, [&, transport_ptr, response_topic, served_queries](const std::string& topic,
                                                                                       const std::string& payload) {
        if (served_queries) {
            served_queries->Increment();
        }
        QueryRequest request;
        parseQueryRequest(payload, request);
        if (request.query_type != "vam") {
            request.query_type = "mapem";
        }

        ldm::model::Geometry aoi;
        aoi.type = ldm::model::Geometry::Type::POINT;
        if (request.has_aoi) {
            aoi.lats = {request.aoi_lat};
            aoi.lons = {request.aoi_lon};
            aoi.radius_m = request.aoi_radius_m;
        }

        rj::Document response_doc;
        response_doc.SetObject();
        auto& alloc = response_doc.GetAllocator();
        response_doc.AddMember("response_timestamp", nowSeconds(), alloc);
        response_doc.AddMember("query_type", rj::Value(request.query_type.c_str(), alloc), alloc);
        if (request.has_request_id) {
            response_doc.AddMember("request_id", request.request_id, alloc);
        }

        if (request.query_type == "vam") {
            auto vams = store.queryByAreaAndFilter(aoi, [](const ldm::model::LdmDataObject& obj) {
                return obj.type == ldm::model::LdmDataType::VAM;
            });
            rj::Value vam_list(rj::kArrayType);
            for (const auto& obj : vams) {
                rj::Value entry(rj::kObjectType);
                entry.AddMember("station_id", obj.id.object_id, alloc);
                entry.AddMember("timestamp",
                                std::chrono::duration<double>(obj.timestamp.time_since_epoch()).count(),
                                alloc);
                if (!obj.geom.lats.empty()) {
                    entry.AddMember("lat", obj.geom.lats.front(), alloc);
                }
                if (!obj.geom.lons.empty()) {
                    entry.AddMember("lon", obj.geom.lons.front(), alloc);
                }
                if (obj.attributes) {
                    auto it = obj.attributes->FindMember("orientation");
                    if (it != obj.attributes->MemberEnd() && it->value.IsNumber()) {
                        entry.AddMember("orientation", it->value.GetDouble(), alloc);
                    }
                }
                rj::Document payload_doc;
                payload_doc.Parse(obj.payload.c_str());
                if (!payload_doc.HasParseError() && payload_doc.IsObject()) {
                    entry.AddMember("payload", rj::Value(payload_doc, alloc), alloc);
                } else {
                    entry.AddMember("payload", rj::Value(obj.payload.c_str(), alloc), alloc);
                }
                vam_list.PushBack(entry, alloc);
            }
            response_doc.AddMember("has_vams", !vams.empty(), alloc);
            response_doc.AddMember("vams", vam_list, alloc);
        } else {
            auto mapems = store.queryByAreaAndFilter(aoi, [](const ldm::model::LdmDataObject& obj) {
                return obj.type == ldm::model::LdmDataType::MAP_INTERSECTION;
            });
            const bool has_mapem = !mapems.empty();
            response_doc.AddMember("has_mapem", has_mapem, alloc);
            if (has_mapem) {
                std::unordered_map<int, double> last_update_ts;
                rj::Document aggregated;
                aggregated.SetObject();
                auto& mapem_alloc = aggregated.GetAllocator();
                rj::Value intersections(rj::kArrayType);

                for (const auto& obj : mapems) {
                    int intersection_id = static_cast<int>(obj.id.object_id);
                    last_update_ts[intersection_id] =
                        std::chrono::duration<double>(obj.timestamp.time_since_epoch()).count();

                    rj::Document inter_doc;
                    inter_doc.Parse(obj.payload.c_str());
                    if (inter_doc.HasParseError() || !inter_doc.IsObject()) {
                        continue;
                    }
                    intersections.PushBack(rj::Value(inter_doc, mapem_alloc), mapem_alloc);
                }
                aggregated.AddMember("intersections", intersections, mapem_alloc);

                std::string mapem_payload = toJsonString(aggregated);
                if (request.has_mapem_detail) {
                    std::string filtered_payload;
                    if (ldm::mapem::BuildFilteredMapem(mapem_payload, request.mapem_filter, filtered_payload)) {
                        mapem_payload = filtered_payload;
                    }
                }

                rj::Document mapem_doc;
                mapem_doc.Parse(mapem_payload.c_str());
                if (!mapem_doc.HasParseError() && mapem_doc.IsObject()) {
                    if (mapem_doc.HasMember("intersections") && mapem_doc["intersections"].IsArray()) {
                        auto& inter_list = mapem_doc["intersections"];
                        for (auto& inter : inter_list.GetArray()) {
                            int intersection_id = 0;
                            if (!extractIntersectionId(inter, intersection_id)) {
                                continue;
                            }
                            auto it = last_update_ts.find(intersection_id);
                            if (it == last_update_ts.end()) {
                                continue;
                            }
                            if (inter.HasMember("last_update_ts")) {
                                inter["last_update_ts"].SetDouble(it->second);
                            } else {
                                inter.AddMember("last_update_ts", it->second, mapem_doc.GetAllocator());
                            }
                        }
                    }
                    response_doc.AddMember("mapem", rj::Value(mapem_doc, alloc), alloc);
                }
            }
        }

        rj::StringBuffer buffer;
        rj::Writer<rj::StringBuffer> writer(buffer);
        response_doc.Accept(writer);
        spdlog::debug("[LDM-Core] Responding to query payload={} via {}", payload, topic);
        transport_ptr->publish(response_topic, buffer.GetString());
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
