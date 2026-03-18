#include "cpm_message.hpp"

#include <cmath>
#include <string>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace rj = rapidjson;

namespace {

constexpr double kEarthRadiusMeters = 6378137.0;

std::string trim(const std::string& s) {
    const auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string toJsonString(const rj::Value& value) {
    rj::StringBuffer buffer;
    rj::Writer<rj::StringBuffer> writer(buffer);
    value.Accept(writer);
    return buffer.GetString();
}

bool tryGetUint64(const rj::Value& value, uint64_t& out) {
    if (value.IsUint64()) {
        out = value.GetUint64();
        return true;
    }
    if (value.IsUint()) {
        out = value.GetUint();
        return true;
    }
    if (value.IsInt64()) {
        const auto raw = value.GetInt64();
        if (raw >= 0) {
            out = static_cast<uint64_t>(raw);
            return true;
        }
    }
    if (value.IsInt()) {
        const auto raw = value.GetInt();
        if (raw >= 0) {
            out = static_cast<uint64_t>(raw);
            return true;
        }
    }
    if (value.IsDouble()) {
        const auto raw = value.GetDouble();
        if (raw >= 0.0) {
            out = static_cast<uint64_t>(raw);
            return true;
        }
    }
    return false;
}

bool tryGetInt(const rj::Value& value, int& out) {
    if (value.IsInt()) {
        out = value.GetInt();
        return true;
    }
    if (value.IsUint()) {
        out = static_cast<int>(value.GetUint());
        return true;
    }
    if (value.IsInt64()) {
        out = static_cast<int>(value.GetInt64());
        return true;
    }
    if (value.IsUint64()) {
        out = static_cast<int>(value.GetUint64());
        return true;
    }
    if (value.IsDouble()) {
        out = static_cast<int>(value.GetDouble());
        return true;
    }
    return false;
}

bool tryGetDouble(const rj::Value& value, double& out) {
    if (value.IsDouble()) {
        out = value.GetDouble();
        return true;
    }
    if (value.IsFloat()) {
        out = value.GetFloat();
        return true;
    }
    if (value.IsInt()) {
        out = static_cast<double>(value.GetInt());
        return true;
    }
    if (value.IsUint()) {
        out = static_cast<double>(value.GetUint());
        return true;
    }
    if (value.IsInt64()) {
        out = static_cast<double>(value.GetInt64());
        return true;
    }
    if (value.IsUint64()) {
        out = static_cast<double>(value.GetUint64());
        return true;
    }
    return false;
}

bool readStationId(const rj::Value& obj, uint64_t& station_id) {
    if (!obj.IsObject()) {
        return false;
    }
    if (obj.HasMember("stationId") && tryGetUint64(obj["stationId"], station_id)) {
        return true;
    }
    if (obj.HasMember("stationID") && tryGetUint64(obj["stationID"], station_id)) {
        return true;
    }
    return false;
}

bool extractExternalStationId(const rj::Value& root, uint64_t& station_id) {
    if (readStationId(root, station_id)) {
        return true;
    }
    if (root.HasMember("header") && readStationId(root["header"], station_id)) {
        return true;
    }
    if (root.HasMember("fields") && root["fields"].IsObject()) {
        const auto& fields = root["fields"];
        if (fields.HasMember("header") && readStationId(fields["header"], station_id)) {
            return true;
        }
    }
    return false;
}

double extractTimestamp(const rj::Value& root, double fallback_now_s) {
    if (root.HasMember("timestamp") && root["timestamp"].IsNumber()) {
        return root["timestamp"].GetDouble();
    }
    if (root.HasMember("test") && root["test"].IsObject()) {
        const auto& test = root["test"];
        if (test.HasMember("json_timestamp") && test["json_timestamp"].IsNumber()) {
            return test["json_timestamp"].GetDouble();
        }
    }
    return fallback_now_s;
}

const rj::Value* findCpmPayload(const rj::Value& root) {
    if (!root.IsObject()) {
        return nullptr;
    }
    if (root.HasMember("managementContainer") && root.HasMember("cpmContainers") &&
        root["cpmContainers"].IsArray()) {
        return &root;
    }
    if (root.HasMember("payload") && root["payload"].IsObject()) {
        const auto& payload = root["payload"];
        if (payload.HasMember("managementContainer") && payload.HasMember("cpmContainers") &&
            payload["cpmContainers"].IsArray()) {
            return &payload;
        }
    }
    if (root.HasMember("fields") && root["fields"].IsObject()) {
        const auto& fields = root["fields"];
        if (fields.HasMember("payload") && fields["payload"].IsObject()) {
            const auto& payload = fields["payload"];
            if (payload.HasMember("managementContainer") && payload.HasMember("cpmContainers") &&
                payload["cpmContainers"].IsArray()) {
                return &payload;
            }
        }
    }
    return nullptr;
}

bool parseReferencePosition(const rj::Value& payload_obj, ldm::cpm::ReferencePosition& out) {
    if (!payload_obj.HasMember("managementContainer") || !payload_obj["managementContainer"].IsObject()) {
        return false;
    }
    const auto& management = payload_obj["managementContainer"];
    if (!management.HasMember("referencePosition") || !management["referencePosition"].IsObject()) {
        return false;
    }
    const auto& ref = management["referencePosition"];

    const rj::Value* lat_value = nullptr;
    const rj::Value* lon_value = nullptr;
    if (ref.HasMember("latitude") && ref["latitude"].IsNumber()) {
        lat_value = &ref["latitude"];
    } else if (ref.HasMember("lat") && ref["lat"].IsNumber()) {
        lat_value = &ref["lat"];
    }

    if (ref.HasMember("longitude") && ref["longitude"].IsNumber()) {
        lon_value = &ref["longitude"];
    } else if (ref.HasMember("lon") && ref["lon"].IsNumber()) {
        lon_value = &ref["lon"];
    } else if (ref.HasMember("long") && ref["long"].IsNumber()) {
        lon_value = &ref["long"];
    }

    if (!lat_value || !lon_value) {
        return false;
    }

    out.has_position = true;
    out.latitude = lat_value->GetDouble();
    out.longitude = lon_value->GetDouble();
    return true;
}

void appendSerializedArray(const rj::Value& value, std::vector<std::string>& out) {
    if (!value.IsArray()) {
        return;
    }
    out.reserve(out.size() + value.Size());
    for (const auto& entry : value.GetArray()) {
        out.push_back(toJsonString(entry));
    }
}

void parseSicEntries(const rj::Value& container_data, std::vector<std::string>& out) {
    if (!container_data.IsObject()) {
        appendSerializedArray(container_data, out);
        if (out.empty() && !container_data.IsNull()) {
            out.push_back(toJsonString(container_data));
        }
        return;
    }

    if (container_data.HasMember("sensorInformation") && container_data["sensorInformation"].IsArray()) {
        appendSerializedArray(container_data["sensorInformation"], out);
    } else if (container_data.HasMember("sensorInformationList") && container_data["sensorInformationList"].IsArray()) {
        appendSerializedArray(container_data["sensorInformationList"], out);
    } else if (container_data.HasMember("sensors") && container_data["sensors"].IsArray()) {
        appendSerializedArray(container_data["sensors"], out);
    } else {
        out.push_back(toJsonString(container_data));
    }
}

void parsePerceptionRegionEntries(const rj::Value& container_data, std::vector<std::string>& out) {
    if (!container_data.IsObject()) {
        appendSerializedArray(container_data, out);
        if (out.empty() && !container_data.IsNull()) {
            out.push_back(toJsonString(container_data));
        }
        return;
    }

    if (container_data.HasMember("perceptionRegions") && container_data["perceptionRegions"].IsArray()) {
        appendSerializedArray(container_data["perceptionRegions"], out);
    } else if (container_data.HasMember("regions") && container_data["regions"].IsArray()) {
        appendSerializedArray(container_data["regions"], out);
    } else {
        out.push_back(toJsonString(container_data));
    }
}

bool parseCoordinateValue(const rj::Value& value, double& out) {
    if (value.IsNumber()) {
        out = value.GetDouble();
        return true;
    }
    if (value.IsObject()) {
        if (value.HasMember("value") && value["value"].IsNumber()) {
            out = value["value"].GetDouble();
            return true;
        }
        if (value.HasMember("x") && value["x"].IsNumber()) {
            out = value["x"].GetDouble();
            return true;
        }
        if (value.HasMember("y") && value["y"].IsNumber()) {
            out = value["y"].GetDouble();
            return true;
        }
    }
    return false;
}

bool parseObjectPosition(const rj::Value& object_entry,
                         const ldm::cpm::ReferencePosition& reference_position,
                         double& out_lat,
                         double& out_lon) {
    if (!reference_position.has_position || !object_entry.IsObject()) {
        return false;
    }
    if (!object_entry.HasMember("position") || !object_entry["position"].IsObject()) {
        return false;
    }
    const auto& position = object_entry["position"];

    double x_m = 0.0;
    double y_m = 0.0;
    bool has_x = false;
    bool has_y = false;

    if (position.HasMember("xCoordinate")) {
        has_x = parseCoordinateValue(position["xCoordinate"], x_m);
    }
    if (position.HasMember("yCoordinate")) {
        has_y = parseCoordinateValue(position["yCoordinate"], y_m);
    }
    if (!has_x || !has_y) {
        return false;
    }

    const double ref_lat_rad = reference_position.latitude * M_PI / 180.0;
    out_lat = reference_position.latitude + (y_m / kEarthRadiusMeters) * 180.0 / M_PI;
    const double cos_lat = std::cos(ref_lat_rad);
    if (std::abs(cos_lat) < 1e-9) {
        out_lon = reference_position.longitude;
    } else {
        out_lon = reference_position.longitude + (x_m / (kEarthRadiusMeters * cos_lat)) * 180.0 / M_PI;
    }
    return true;
}

void parseObjects(const rj::Value& container_data,
                  const ldm::cpm::ReferencePosition& reference_position,
                  double frame_timestamp_s,
                  std::vector<ldm::cpm::ParsedObject>& out) {
    if (!container_data.IsObject()) {
        return;
    }
    if (!container_data.HasMember("perceivedObjects") || !container_data["perceivedObjects"].IsArray()) {
        return;
    }

    const auto& perceived = container_data["perceivedObjects"];
    out.reserve(out.size() + perceived.Size());
    for (const auto& object_entry : perceived.GetArray()) {
        if (!object_entry.IsObject()) {
            continue;
        }
        ldm::cpm::ParsedObject object;
        if (object_entry.HasMember("objectId")) {
            if (!tryGetUint64(object_entry["objectId"], object.object_id)) {
                continue;
            }
        } else if (object_entry.HasMember("id")) {
            if (!tryGetUint64(object_entry["id"], object.object_id)) {
                continue;
            }
        } else {
            continue;
        }

        object.timestamp_unix_s = frame_timestamp_s;
        if (object_entry.HasMember("measurementDeltaTime")) {
            double delta_ms = 0.0;
            if (tryGetDouble(object_entry["measurementDeltaTime"], delta_ms) && delta_ms > 0.0) {
                object.timestamp_unix_s = frame_timestamp_s - (delta_ms / 1000.0);
            }
        }

        if (parseObjectPosition(object_entry, reference_position, object.latitude, object.longitude)) {
            object.has_position = true;
        }

        out.push_back(object);
    }
}

}  // namespace

namespace ldm::cpm {

const char* ScopeToString(Scope scope) {
    return scope == Scope::OWN ? "own" : "external";
}

bool ParseCpmFrame(const std::string& payload,
                   Scope scope,
                   uint64_t local_station_id,
                   double now_unix_s,
                   ParsedFrame& out,
                   ParseError& err) {
    out = ParsedFrame{};
    err = ParseError::NONE;

    const auto trimmed = trim(payload);
    if (trimmed.empty()) {
        err = ParseError::INVALID_JSON;
        return false;
    }

    rj::Document doc;
    doc.Parse(trimmed.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        err = ParseError::INVALID_JSON;
        return false;
    }

    out.scope = scope;
    out.timestamp_unix_s = extractTimestamp(doc, now_unix_s);

    if (scope == Scope::OWN) {
        out.station_id = local_station_id;
    } else {
        if (!extractExternalStationId(doc, out.station_id) || out.station_id == 0) {
            err = ParseError::MISSING_STATION_ID;
            return false;
        }
    }

    const auto* cpm_payload = findCpmPayload(doc);
    if (!cpm_payload || !cpm_payload->IsObject()) {
        err = ParseError::MISSING_CPM_PAYLOAD;
        return false;
    }

    parseReferencePosition(*cpm_payload, out.reference_position);

    const auto& containers = (*cpm_payload)["cpmContainers"];
    for (const auto& container : containers.GetArray()) {
        if (!container.IsObject()) {
            continue;
        }
        int container_id = -1;
        if (container.HasMember("containerId")) {
            tryGetInt(container["containerId"], container_id);
        }
        const rj::Value* container_data = nullptr;
        if (container.HasMember("containerData")) {
            container_data = &container["containerData"];
        }
        if (!container_data) {
            continue;
        }

        if (container_id == 3) {
            out.has_sic = true;
            parseSicEntries(*container_data, out.sic_entries);
        } else if (container_id == 4) {
            out.has_perception_regions = true;
            parsePerceptionRegionEntries(*container_data, out.perception_region_entries);
        } else if (container_id == 5) {
            out.has_objects = true;
            parseObjects(*container_data, out.reference_position, out.timestamp_unix_s, out.objects);
        }
    }

    return true;
}

}  // namespace ldm::cpm
