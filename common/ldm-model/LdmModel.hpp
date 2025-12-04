#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rapidjson {
class Document;
}

namespace ldm::model {

struct LdmDataObjectId {
    uint64_t station_id{0};
    uint64_t local_id{0};

    bool operator==(const LdmDataObjectId& other) const {
        return station_id == other.station_id && local_id == other.local_id;
    }
};

enum class LdmDataType {
    MAP_INTERSECTION,
    MAP_LANE,
    MAP_SPEED_LIMIT,
    MAP_SIGNAL_GROUP,
};

struct Geometry {
    enum class Type { POINT, LINESTRING, POLYGON };
    Type type{Type::POINT};
    std::vector<double> lats;
    std::vector<double> lons;
    std::vector<double> xs;
    std::vector<double> ys;
};

struct LdmDataObject {
    LdmDataObjectId id;
    LdmDataType type{LdmDataType::MAP_INTERSECTION};
    Geometry geom;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
    std::chrono::milliseconds time_validity{std::chrono::milliseconds(0)};
    double max_distance_from_station_m{0.0};
    std::shared_ptr<rapidjson::Document> attributes;  // shared to avoid deep copies
    uint32_t source_station_id{0};
};

struct LdmIntersection {
    int intersection_id{0};
    double ref_lat{0.0};
    double ref_lon{0.0};
    double lane_width_m{0.0};
    uint8_t msg_revision{0};
    std::vector<int> lane_ids;
};

struct LdmLane {
    int intersection_id{0};
    int lane_id{0};
    bool ingress{false};
    bool egress{false};
    std::vector<int> connects_to_lane_ids;
    std::vector<int> connects_to_signal_groups;
    Geometry centerline;
};

}  // namespace ldm::model

namespace std {
template <>
struct hash<ldm::model::LdmDataObjectId> {
    std::size_t operator()(const ldm::model::LdmDataObjectId& id) const noexcept {
        return (std::hash<uint64_t>{}(id.station_id) << 1) ^ std::hash<uint64_t>{}(id.local_id);
    }
};
}  // namespace std
