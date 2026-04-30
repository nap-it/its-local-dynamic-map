#include "vam_message.hpp"

#include <string>

#include <rapidjson/document.h>

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

bool readStationId(const rj::Value& obj, uint64_t& station_id) {
    if (obj.HasMember("stationID") && obj["stationID"].IsUint64()) {
        station_id = obj["stationID"].GetUint64();
        return true;
    }
    if (obj.HasMember("stationID") && obj["stationID"].IsInt()) {
        station_id = static_cast<uint64_t>(obj["stationID"].GetInt());
        return true;
    }
    if (obj.HasMember("stationId") && obj["stationId"].IsUint64()) {
        station_id = obj["stationId"].GetUint64();
        return true;
    }
    if (obj.HasMember("stationId") && obj["stationId"].IsInt()) {
        station_id = static_cast<uint64_t>(obj["stationId"].GetInt());
        return true;
    }
    return false;
}

bool extractStationId(const rj::Value& root, uint64_t& station_id) {
    if (readStationId(root, station_id)) {
        return true;
    }
    if (root.HasMember("header") && root["header"].IsObject()) {
        if (readStationId(root["header"], station_id)) {
            return true;
        }
    }
    if (root.HasMember("fields") && root["fields"].IsObject()) {
        const auto& fields = root["fields"];
        if (fields.HasMember("header") && fields["header"].IsObject()) {
            if (readStationId(fields["header"], station_id)) {
                return true;
            }
        }
    }
    return false;
}

const rj::Value* findVamRoot(const rj::Value& root) {
    if (root.HasMember("vamParameters") && root["vamParameters"].IsObject()) {
        return &root;
    }
    if (root.HasMember("vam") && root["vam"].IsObject()) {
        return &root["vam"];
    }
    if (root.HasMember("fields") && root["fields"].IsObject()) {
        const auto& fields = root["fields"];
        if (fields.HasMember("vam") && fields["vam"].IsObject()) {
            return &fields["vam"];
        }
    }
    return nullptr;
}

bool extractLatLon(const rj::Value& root, double& lat, double& lon) {
    const auto* vam_root = findVamRoot(root);
    if (!vam_root || !vam_root->IsObject()) {
        return false;
    }
    if (!vam_root->HasMember("vamParameters") || !(*vam_root)["vamParameters"].IsObject()) {
        return false;
    }
    const auto& params = (*vam_root)["vamParameters"];
    if (!params.HasMember("basicContainer") || !params["basicContainer"].IsObject()) {
        return false;
    }
    const auto& basic = params["basicContainer"];
    if (!basic.HasMember("referencePosition") || !basic["referencePosition"].IsObject()) {
        return false;
    }
    const auto& ref = basic["referencePosition"];
    if (!ref.HasMember("latitude") || !ref["latitude"].IsNumber()) {
        return false;
    }
    lat = ref["latitude"].GetDouble();
    if (ref.HasMember("longitude") && ref["longitude"].IsNumber()) {
        lon = ref["longitude"].GetDouble();
        return true;
    }
    if (ref.HasMember("lon") && ref["lon"].IsNumber()) {
        lon = ref["lon"].GetDouble();
        return true;
    }
    if (ref.HasMember("long") && ref["long"].IsNumber()) {
        lon = ref["long"].GetDouble();
        return true;
    }
    return false;
}

bool extractTimestamp(const rj::Value& root, double& timestamp_s) {
    if (root.HasMember("timestamp") && root["timestamp"].IsNumber()) {
        timestamp_s = root["timestamp"].GetDouble();
        return true;
    }
    if (root.HasMember("test") && root["test"].IsObject()) {
        const auto& test = root["test"];
        if (test.HasMember("json_timestamp") && test["json_timestamp"].IsNumber()) {
            timestamp_s = test["json_timestamp"].GetDouble();
            return true;
        }
    }
    return false;
}

bool extractOrientation(const rj::Value& root, double& orientation) {
    const auto* vam_root = findVamRoot(root);
    if (!vam_root || !vam_root->IsObject()) {
        return false;
    }
    if (!vam_root->HasMember("vamParameters") || !(*vam_root)["vamParameters"].IsObject()) {
        return false;
    }
    const auto& params = (*vam_root)["vamParameters"];
    if (!params.HasMember("vruHighFrequencyContainer") || !params["vruHighFrequencyContainer"].IsObject()) {
        return false;
    }
    const auto& hfc = params["vruHighFrequencyContainer"];
    if (!hfc.HasMember("orientation") || !hfc["orientation"].IsObject()) {
        return false;
    }
    const auto& orientation_obj = hfc["orientation"];
    if (orientation_obj.HasMember("headingValue") && orientation_obj["headingValue"].IsNumber()) {
        orientation = orientation_obj["headingValue"].GetDouble();
        return true;
    }
    if (orientation_obj.IsNumber()) {
        orientation = orientation_obj.GetDouble();
        return true;
    }
    return false;
}

}  // namespace

namespace ldm::vam {

bool ExtractVamSummary(const std::string& payload, VamSummary& out) {
    out = VamSummary{};
    const auto trimmed = trim(payload);
    if (trimmed.empty()) {
        return false;
    }

    rj::Document doc;
    doc.Parse(trimmed.c_str());
    if (doc.HasParseError() || !doc.IsObject()) {
        return false;
    }

    uint64_t station_id = 0;
    if (!extractStationId(doc, station_id) || station_id == 0) {
        return false;
    }

    double lat = 0.0;
    double lon = 0.0;
    if (!extractLatLon(doc, lat, lon)) {
        return false;
    }

    double timestamp_s = 0.0;
    extractTimestamp(doc, timestamp_s);

    double orientation = 0.0;
    bool has_orientation = extractOrientation(doc, orientation);

    out.station_id = station_id;
    out.timestamp_s = timestamp_s;
    out.lat = lat;
    out.lon = lon;
    out.has_orientation = has_orientation;
    out.orientation = orientation;
    return true;
}

}  // namespace ldm::vam
