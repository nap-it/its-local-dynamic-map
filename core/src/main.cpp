#include <atomic>
#include <chrono>
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

#include "../../common/config/config.hpp"
#include "../../common/ldm-model/ldm_store.hpp"
#include "../../common/locator/locator.hpp"
#include "../../common/messaging/transport.hpp"
#include "../../common/metrics/metrics.hpp"

namespace rj = rapidjson;

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

struct MapemFilter {
    bool include_vehicle_lane = true;
    bool include_sidewalk = true;
    bool include_waiting_sidewalk = false;
    bool include_crosswalk = true;
    bool include_bike_lane = true;
    bool include_parking = true;
    bool include_median = true;
    bool include_unknown_lane = true;
    bool include_connections = true;
    bool include_maneuver = true;
    bool include_shared_with = true;
    bool include_signal_groups = true;
    bool include_speed_limits = true;
    bool include_overlays = true;
    bool include_directional_use = true;
};

void applyBool(const rj::Value& obj, const char* key, bool& target) {
    if (obj.HasMember(key) && obj[key].IsBool()) {
        target = obj[key].GetBool();
    }
}

MapemFilter parseMapemFilter(const rj::Value& root) {
    MapemFilter filter;
    const rj::Value* filter_obj = nullptr;
    if (root.HasMember("mapem_filter") && root["mapem_filter"].IsObject()) {
        filter_obj = &root["mapem_filter"];
    }
    const rj::Value& source = filter_obj ? *filter_obj : root;
    applyBool(source, "vehicleLane", filter.include_vehicle_lane);
    applyBool(source, "vehicle", filter.include_vehicle_lane);
    applyBool(source, "sidewalk", filter.include_sidewalk);
    applyBool(source, "waitingSidewalk", filter.include_waiting_sidewalk);
    applyBool(source, "crosswalk", filter.include_crosswalk);
    applyBool(source, "bikeLane", filter.include_bike_lane);
    applyBool(source, "parking", filter.include_parking);
    applyBool(source, "median", filter.include_median);
    applyBool(source, "unknownLane", filter.include_unknown_lane);
    applyBool(source, "connections", filter.include_connections);
    applyBool(source, "maneuverConnection", filter.include_maneuver);
    applyBool(source, "maneuver", filter.include_maneuver);
    applyBool(source, "sharedWith", filter.include_shared_with);
    applyBool(source, "signalGroups", filter.include_signal_groups);
    applyBool(source, "signalGroup", filter.include_signal_groups);
    applyBool(source, "speedLimits", filter.include_speed_limits);
    applyBool(source, "overlays", filter.include_overlays);
    applyBool(source, "directionalUse", filter.include_directional_use);
    return filter;
}

bool isFullMapemRequest(const MapemFilter& filter) {
    return filter.include_vehicle_lane &&
           filter.include_sidewalk &&
           filter.include_waiting_sidewalk &&
           filter.include_crosswalk &&
           filter.include_bike_lane &&
           filter.include_parking &&
           filter.include_median &&
           filter.include_unknown_lane &&
           filter.include_connections &&
           filter.include_maneuver &&
           filter.include_shared_with &&
           filter.include_signal_groups &&
           filter.include_speed_limits &&
           filter.include_overlays &&
           filter.include_directional_use;
}

std::string laneTypeFromLane(const rj::Value& lane) {
    if (!lane.IsObject() || !lane.HasMember("laneAttributes") || !lane["laneAttributes"].IsObject()) {
        return "unknown";
    }
    const auto& attrs = lane["laneAttributes"];
    if (!attrs.HasMember("laneType") || !attrs["laneType"].IsObject()) {
        return "unknown";
    }
    const auto& lane_type = attrs["laneType"];
    constexpr const char* kTypes[] = {
        "vehicle",
        "crosswalk",
        "bikeLane",
        "sidewalk",
        "median",
        "parking",
    };
    for (const auto* key : kTypes) {
        if (lane_type.HasMember(key)) {
            return key;
        }
    }
    return "unknown";
}

bool allowLaneType(const std::string& lane_type, const MapemFilter& filter) {
    if (lane_type == "vehicle") return filter.include_vehicle_lane;
    if (lane_type == "sidewalk") return filter.include_sidewalk;
    if (lane_type == "waitingSidewalk") return filter.include_waiting_sidewalk;
    if (lane_type == "crosswalk") return filter.include_crosswalk;
    if (lane_type == "bikeLane") return filter.include_bike_lane;
    if (lane_type == "parking") return filter.include_parking;
    if (lane_type == "median") return filter.include_median;
    return filter.include_unknown_lane;
}

void removeRevisions(rj::Value& mapem) {
    if (!mapem.IsObject()) {
        return;
    }
    mapem.RemoveMember("msgIssueRevision");
    if (mapem.HasMember("intersections") && mapem["intersections"].IsArray()) {
        for (auto& inter : mapem["intersections"].GetArray()) {
            if (inter.IsObject()) {
                inter.RemoveMember("revision");
            }
        }
    }
}

std::string toJsonString(const rj::Value& value) {
    rj::StringBuffer buffer;
    rj::Writer<rj::StringBuffer> writer(buffer);
    value.Accept(writer);
    return buffer.GetString();
}

bool buildFilteredMapem(const std::string& mapem_json, const MapemFilter& filter, std::string& out_json) {
    if (mapem_json.empty()) {
        return false;
    }

    rj::Document src;
    src.Parse(mapem_json.c_str());
    if (src.HasParseError() || !src.IsObject()) {
        return false;
    }

    if (isFullMapemRequest(filter)) {
        removeRevisions(src);
        out_json = toJsonString(src);
        return true;
    }

    rj::Document filtered;
    filtered.SetObject();
    auto& alloc = filtered.GetAllocator();
    rj::Value out_intersections(rj::kArrayType);

    if (src.HasMember("intersections") && src["intersections"].IsArray()) {
        for (const auto& inter : src["intersections"].GetArray()) {
            if (!inter.IsObject()) {
                continue;
            }
            rj::Value out_inter(rj::kObjectType);
            if (inter.HasMember("id")) {
                out_inter.AddMember("id", rj::Value(inter["id"], alloc), alloc);
            }
            if (inter.HasMember("refPoint")) {
                out_inter.AddMember("refPoint", rj::Value(inter["refPoint"], alloc), alloc);
            }
            if (inter.HasMember("laneWidth")) {
                out_inter.AddMember("laneWidth", rj::Value(inter["laneWidth"], alloc), alloc);
            }
            if (filter.include_speed_limits && inter.HasMember("speedLimits")) {
                out_inter.AddMember("speedLimits", rj::Value(inter["speedLimits"], alloc), alloc);
            }

            rj::Value out_lane_set(rj::kArrayType);
            if (inter.HasMember("laneSet") && inter["laneSet"].IsArray()) {
                const auto& lane_set = inter["laneSet"];
                std::unordered_map<int, std::string> lane_types;
                std::unordered_set<int> waiting_sidewalks;

                for (const auto& lane : lane_set.GetArray()) {
                    if (!lane.IsObject()) {
                        continue;
                    }
                    if (!lane.HasMember("laneID") || !lane["laneID"].IsInt()) {
                        continue;
                    }
                    lane_types[lane["laneID"].GetInt()] = laneTypeFromLane(lane);
                }

                for (const auto& lane : lane_set.GetArray()) {
                    if (!lane.IsObject()) {
                        continue;
                    }
                    if (!lane.HasMember("laneID") || !lane["laneID"].IsInt()) {
                        continue;
                    }
                    int lane_id = lane["laneID"].GetInt();
                    auto type_it = lane_types.find(lane_id);
                    if (type_it == lane_types.end() || type_it->second != "sidewalk") {
                        continue;
                    }
                    if (!lane.HasMember("connectsTo") || !lane["connectsTo"].IsArray()) {
                        continue;
                    }
                    for (const auto& conn : lane["connectsTo"].GetArray()) {
                        if (!conn.IsObject()) {
                            continue;
                        }
                        if (!conn.HasMember("connectingLane") || !conn["connectingLane"].IsObject()) {
                            continue;
                        }
                        const auto& connecting_lane = conn["connectingLane"];
                        if (!connecting_lane.HasMember("lane") || !connecting_lane["lane"].IsInt()) {
                            continue;
                        }
                        int target_lane = connecting_lane["lane"].GetInt();
                        auto target_it = lane_types.find(target_lane);
                        if (target_it != lane_types.end() && target_it->second == "crosswalk") {
                            waiting_sidewalks.insert(lane_id);
                            break;
                        }
                    }
                }

                for (const auto& lane : lane_set.GetArray()) {
                    if (!lane.IsObject()) {
                        continue;
                    }
                    int lane_id = 0;
                    if (lane.HasMember("laneID") && lane["laneID"].IsInt()) {
                        lane_id = lane["laneID"].GetInt();
                    }
                    std::string lane_type = laneTypeFromLane(lane);
                    if (waiting_sidewalks.count(lane_id)) {
                        lane_type = "waitingSidewalk";
                    }
                    if (!allowLaneType(lane_type, filter)) {
                        continue;
                    }
                    rj::Value out_lane(rj::kObjectType);
                    if (lane.HasMember("laneID")) {
                        out_lane.AddMember("laneID", rj::Value(lane["laneID"], alloc), alloc);
                    }
                    if (lane.HasMember("nodeList")) {
                        out_lane.AddMember("nodeList", rj::Value(lane["nodeList"], alloc), alloc);
                    }
                    if (lane.HasMember("laneAttributes") && lane["laneAttributes"].IsObject()) {
                        const auto& attrs = lane["laneAttributes"];
                        rj::Value out_attrs(rj::kObjectType);
                        if (lane_type == "waitingSidewalk") {
                            rj::Value lane_type_obj(rj::kObjectType);
                            lane_type_obj.AddMember("waitingSidewalk", rj::Value(rj::kObjectType), alloc);
                            out_attrs.AddMember("laneType", lane_type_obj, alloc);
                        } else if (attrs.HasMember("laneType")) {
                            out_attrs.AddMember("laneType", rj::Value(attrs["laneType"], alloc), alloc);
                        }
                        if (filter.include_directional_use && attrs.HasMember("directionalUse")) {
                            out_attrs.AddMember("directionalUse", rj::Value(attrs["directionalUse"], alloc), alloc);
                        }
                        if (filter.include_shared_with && attrs.HasMember("sharedWith")) {
                            out_attrs.AddMember("sharedWith", rj::Value(attrs["sharedWith"], alloc), alloc);
                        }
                        if (!out_attrs.ObjectEmpty()) {
                            out_lane.AddMember("laneAttributes", out_attrs, alloc);
                        }
                    }

                    if (filter.include_connections && lane.HasMember("connectsTo") && lane["connectsTo"].IsArray()) {
                        rj::Value out_connects(rj::kArrayType);
                        for (const auto& conn : lane["connectsTo"].GetArray()) {
                            if (!conn.IsObject()) {
                                continue;
                            }
                            rj::Value out_conn(conn, alloc);
                            if (!filter.include_signal_groups) {
                                out_conn.RemoveMember("signalGroup");
                            }
                            if (!filter.include_maneuver) {
                                if (out_conn.HasMember("connectingLane") && out_conn["connectingLane"].IsObject()) {
                                    out_conn["connectingLane"].RemoveMember("maneuver");
                                }
                            }
                            out_connects.PushBack(out_conn, alloc);
                        }
                        if (!out_connects.Empty()) {
                            out_lane.AddMember("connectsTo", out_connects, alloc);
                        }
                    }

                    if (filter.include_overlays && lane.HasMember("overlays")) {
                        out_lane.AddMember("overlays", rj::Value(lane["overlays"], alloc), alloc);
                    }

                    out_lane_set.PushBack(out_lane, alloc);
                }
            }

            out_inter.AddMember("laneSet", out_lane_set, alloc);
            out_intersections.PushBack(out_inter, alloc);
        }
    }

    filtered.AddMember("intersections", out_intersections, alloc);
    out_json = toJsonString(filtered);
    return true;
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
        MapemFilter filter;
        if (isJsonObject(payload)) {
            rj::Document request_doc;
            request_doc.Parse(payload.c_str());
            if (!request_doc.HasParseError() && request_doc.IsObject()) {
                filter = parseMapemFilter(request_doc);
            }
        }
        std::string mapem_payload;
        if (has_mapem) {
            if (!buildFilteredMapem(mapem_snapshot, filter, mapem_payload)) {
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

    while (true) {
        store.runMaintenance();
        std::this_thread::sleep_for(5s);
    }
    return 0;
}
