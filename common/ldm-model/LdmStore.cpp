#include "LdmStore.hpp"

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
    objects_[obj.id] = obj;
    if (obj.type == LdmDataType::MAP_INTERSECTION && obj.attributes) {
        auto it = obj.attributes->FindMember("intersectionId");
        if (it != obj.attributes->MemberEnd() && it->value.IsInt()) {
            LdmIntersection inter{};
            inter.intersection_id = it->value.GetInt();
            inter.ref_lat = !obj.geom.lats.empty() ? obj.geom.lats.front() : 0.0;
            inter.ref_lon = !obj.geom.lons.empty() ? obj.geom.lons.front() : 0.0;
            intersections_[inter.intersection_id] = inter;
        }
    }
    if (obj.type == LdmDataType::MAP_LANE && obj.attributes) {
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
    for (const auto& [id, obj] : objects_) {
        if (filter && !filter(obj)) {
            continue;
        }
        if (areaOfInterest.type == Geometry::Type::POINT && !obj.geom.lats.empty()) {
            double dist = haversine(areaOfInterest.lats.front(), areaOfInterest.lons.front(), obj.geom.lats.front(),
                                    obj.geom.lons.front());
            if (dist > aom_radius_m_) {
                continue;
            }
        }
        result.push_back(obj);
    }
    return result;
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
            spdlog::debug("[LDM-Core] Expiring object station={} local={}", obj.id.station_id, obj.id.local_id);
            it = objects_.erase(it);
            continue;
        }
        if (!obj.geom.lats.empty()) {
            double dist = haversine(station_lat_, station_lon_, obj.geom.lats.front(), obj.geom.lons.front());
            if (dist > aom_radius_m_) {
                spdlog::debug("[LDM-Core] Dropping object outside AoM station={} local={}", obj.id.station_id,
                              obj.id.local_id);
                it = objects_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

}  // namespace ldm::model
