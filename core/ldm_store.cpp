#include "ldm_store.hpp"

#include <cmath>
#include <limits>
#include <spdlog/spdlog.h>

namespace {

constexpr double kEarthRadiusMeters = 6378137.0;

inline double deg2rad(double deg) { return deg * M_PI / 180.0; }

// Simple haversine distance in meters
inline double haversine(double lat1, double lon1, double lat2, double lon2) {
    double dlat = deg2rad(lat2 - lat1);
    double dlon = deg2rad(lon2 - lon1);
    double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(deg2rad(lat1)) * std::cos(deg2rad(lat2)) * std::sin(dlon / 2) * std::sin(dlon / 2);
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    return kEarthRadiusMeters * c;
}

const char* typeName(ldm::model::LdmDataType type) {
    switch (type) {
        case ldm::model::LdmDataType::MAP_INTERSECTION:
            return "map_intersection";
        case ldm::model::LdmDataType::MAP_LANE:
            return "map_lane";
        case ldm::model::LdmDataType::MAP_SPEED_LIMIT:
            return "map_speed_limit";
        case ldm::model::LdmDataType::MAP_SIGNAL_GROUP:
            return "map_signal_group";
        case ldm::model::LdmDataType::VAM:
            return "vam";
    }
    return "unknown";
}

}  // namespace

namespace ldm::model {

LdmStore::LdmStore(double aom_radius_m) : aom_radius_m_(aom_radius_m) {}

void LdmStore::setStationReference(double lat, double lon) {
    std::lock_guard<std::mutex> lock(mutex_);
    station_lat_ = lat;
    station_lon_ = lon;
}

void LdmStore::upsertObject(const LdmDataObject& obj) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!obj.geom.lats.empty() && !obj.geom.lons.empty()) {
        double dist = haversine(station_lat_, station_lon_, obj.geom.lats.front(), obj.geom.lons.front());
        if (dist > aom_radius_m_) {
            spdlog::debug("[LDM-Core] Dropping {} outside AoM id={}", typeName(obj.type), obj.id.object_id);
            return;
        }
    }

    auto existing = objects_.find(obj.id);
    if (existing != objects_.end() && obj.timestamp <= existing->second.timestamp) {
        spdlog::debug("[LDM-Core] Ignoring stale {} update id={}", typeName(obj.type), obj.id.object_id);
        return;
    }

    objects_[obj.id] = obj;
    if (obj.type == LdmDataType::MAP_INTERSECTION) {
        LdmIntersection inter{};
        inter.intersection_id = static_cast<int>(obj.id.object_id);
        inter.ref_lat = !obj.geom.lats.empty() ? obj.geom.lats.front() : 0.0;
        inter.ref_lon = !obj.geom.lons.empty() ? obj.geom.lons.front() : 0.0;
        intersections_[inter.intersection_id] = inter;
    } else if (obj.type == LdmDataType::MAP_LANE && obj.attributes) {
        auto laneId = obj.attributes->FindMember("laneId");
        auto intersectionId = obj.attributes->FindMember("intersectionId");
        if (laneId != obj.attributes->MemberEnd() && intersectionId != obj.attributes->MemberEnd() &&
            laneId->value.IsInt() && intersectionId->value.IsInt()) {
            LdmLane lane{};
            lane.lane_id = laneId->value.GetInt();
            lane.intersection_id = intersectionId->value.GetInt();
            lane.centerline = obj.geom;
            lanes_[lane.lane_id] = lane;
        }
    }
}

void LdmStore::removeObject(const LdmDataObjectId& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    objects_.erase(id);
}

std::vector<LdmDataObject> LdmStore::queryByAreaAndFilter(
    const Geometry& areaOfInterest, const std::function<bool(const LdmDataObject&)>& filter) {
    std::vector<LdmDataObject> result;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::system_clock::now();
    const double radius_m = (areaOfInterest.type == Geometry::Type::POINT && areaOfInterest.radius_m > 0.0)
                                ? areaOfInterest.radius_m
                                : aom_radius_m_;
    for (const auto& [id, obj] : objects_) {
        if (obj.time_validity.count() > 0 && now - obj.timestamp > obj.time_validity) {
            continue;
        }
        if (filter && !filter(obj)) {
            continue;
        }
        if (areaOfInterest.type == Geometry::Type::POINT && !obj.geom.lats.empty() && !obj.geom.lons.empty()) {
            double dist = haversine(areaOfInterest.lats.front(), areaOfInterest.lons.front(), obj.geom.lats.front(),
                                    obj.geom.lons.front());
            if (dist > radius_m) {
                continue;
            }
        }
        result.push_back(obj);
    }
    return result;
}

std::size_t LdmStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return objects_.size();
}

std::vector<LdmLane> LdmStore::findLanesNearPosition(double lat, double lon, double radius_m) const {
    std::vector<LdmLane> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [laneId, lane] : lanes_) {
        if (!lane.centerline.lats.empty()) {
            double dist = haversine(lat, lon, lane.centerline.lats.front(), lane.centerline.lons.front());
            if (dist <= radius_m) {
                result.push_back(lane);
            }
        }
    }
    return result;
}

std::optional<LdmLane> LdmStore::findClosestLane(double lat, double lon) const {
    std::lock_guard<std::mutex> lock(mutex_);
    double best = std::numeric_limits<double>::max();
    std::optional<LdmLane> bestLane;
    for (const auto& [laneId, lane] : lanes_) {
        if (!lane.centerline.lats.empty()) {
            double dist = haversine(lat, lon, lane.centerline.lats.front(), lane.centerline.lons.front());
            if (dist < best) {
                best = dist;
                bestLane = lane;
            }
        }
    }
    return bestLane;
}

std::optional<LdmIntersection> LdmStore::findIntersectionById(int intersection_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = intersections_.find(intersection_id);
    if (it != intersections_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void LdmStore::runMaintenance() {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::system_clock::now();
    for (auto it = objects_.begin(); it != objects_.end();) {
        const auto& obj = it->second;
        if (obj.time_validity.count() > 0 && now - obj.timestamp > obj.time_validity) {
            spdlog::debug("[LDM-Core] Expiring {} id={}", typeName(obj.type), obj.id.object_id);
            it = objects_.erase(it);
            continue;
        }
        if (!obj.geom.lats.empty() && !obj.geom.lons.empty()) {
            double dist = haversine(station_lat_, station_lon_, obj.geom.lats.front(), obj.geom.lons.front());
            if (dist > aom_radius_m_) {
                spdlog::debug("[LDM-Core] Dropping {} outside AoM id={}", typeName(obj.type), obj.id.object_id);
                it = objects_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

}  // namespace ldm::model
