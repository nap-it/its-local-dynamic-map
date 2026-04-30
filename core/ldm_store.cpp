#include "ldm_store.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include <spdlog/spdlog.h>

namespace {

constexpr double kEarthRadiusMeters = 6378137.0;
constexpr double kCadenceEwmaAlpha = 0.35;

inline double deg2rad(double deg) {
    return deg * M_PI / 180.0;
}

inline double haversineMeters(double lat1, double lon1, double lat2, double lon2) {
    const double dlat = deg2rad(lat2 - lat1);
    const double dlon = deg2rad(lon2 - lon1);
    const double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
                     std::cos(deg2rad(lat1)) * std::cos(deg2rad(lat2)) *
                         std::sin(dlon / 2.0) * std::sin(dlon / 2.0);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return kEarthRadiusMeters * c;
}

inline bool nearlyEqual(double a, double b, double eps = 1e-7) {
    return std::abs(a - b) <= eps;
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

LdmStore::LdmStore(double aom_radius_m)
    : LdmStore(aom_radius_m, CpmConfig{}) {}

LdmStore::LdmStore(double aom_radius_m, const CpmConfig& cpm_config)
    : aom_radius_m_(aom_radius_m), cpm_config_(cpm_config) {}

void LdmStore::setStationReference(double lat, double lon) {
    std::lock_guard<std::mutex> lock(mutex_);
    station_lat_ = lat;
    station_lon_ = lon;
}

bool LdmStore::upsertObject(const LdmDataObject& obj) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!obj.geom.lats.empty() && !obj.geom.lons.empty()) {
        const double dist = haversineMeters(station_lat_, station_lon_, obj.geom.lats.front(), obj.geom.lons.front());
        if (dist > aom_radius_m_) {
            spdlog::debug("[LDM-Core] Dropping {} outside AoM id={}", typeName(obj.type), obj.id.object_id);
            return false;
        }
    }

    const auto existing = objects_.find(obj.id);
    if (existing != objects_.end() && obj.timestamp <= existing->second.timestamp) {
        spdlog::debug("[LDM-Core] Ignoring stale {} update id={}", typeName(obj.type), obj.id.object_id);
        return false;
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
    return true;
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

    const bool has_point_aoi =
        areaOfInterest.type == Geometry::Type::POINT && !areaOfInterest.lats.empty() && !areaOfInterest.lons.empty();
    const double radius_m = (has_point_aoi && areaOfInterest.radius_m > 0.0) ? areaOfInterest.radius_m : aom_radius_m_;

    for (const auto& [id, obj] : objects_) {
        if (obj.time_validity.count() > 0 && now - obj.timestamp > obj.time_validity) {
            continue;
        }
        if (filter && !filter(obj)) {
            continue;
        }
        if (has_point_aoi && !obj.geom.lats.empty() && !obj.geom.lons.empty()) {
            const double dist = haversineMeters(areaOfInterest.lats.front(),
                                                areaOfInterest.lons.front(),
                                                obj.geom.lats.front(),
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

std::size_t LdmStore::mapemCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (const auto& entry : objects_) {
        if (entry.second.type == LdmDataType::MAP_INTERSECTION) {
            ++count;
        }
    }
    return count;
}

std::size_t LdmStore::vamCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (const auto& entry : objects_) {
        if (entry.second.type == LdmDataType::VAM) {
            ++count;
        }
    }
    return count;
}

std::vector<LdmLane> LdmStore::findLanesNearPosition(double lat, double lon, double radius_m) const {
    std::vector<LdmLane> result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [laneId, lane] : lanes_) {
        if (!lane.centerline.lats.empty() && !lane.centerline.lons.empty()) {
            const double dist = haversineMeters(lat, lon, lane.centerline.lats.front(), lane.centerline.lons.front());
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
        if (!lane.centerline.lats.empty() && !lane.centerline.lons.empty()) {
            const double dist = haversineMeters(lat, lon, lane.centerline.lats.front(), lane.centerline.lons.front());
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
    const auto it = intersections_.find(intersection_id);
    if (it != intersections_.end()) {
        return it->second;
    }
    return std::nullopt;
}

LdmStore::MaintenanceResult LdmStore::runMaintenance() {
    std::lock_guard<std::mutex> lock(mutex_);
    MaintenanceResult result;
    std::size_t before_mapem = 0;
    std::size_t before_vam = 0;
    for (const auto& entry : objects_) {
        if (entry.second.type == LdmDataType::MAP_INTERSECTION) {
            ++before_mapem;
        } else if (entry.second.type == LdmDataType::VAM) {
            ++before_vam;
        }
    }

    const auto now = std::chrono::system_clock::now();
    for (auto it = objects_.begin(); it != objects_.end();) {
        const auto& obj = it->second;
        if (obj.time_validity.count() > 0 && now - obj.timestamp > obj.time_validity) {
            spdlog::debug("[LDM-Core] Expiring {} id={}", typeName(obj.type), obj.id.object_id);
            it = objects_.erase(it);
            result.changed = true;
            continue;
        }
        if (!obj.geom.lats.empty() && !obj.geom.lons.empty()) {
            const double dist = haversineMeters(station_lat_, station_lon_, obj.geom.lats.front(), obj.geom.lons.front());
            if (dist > aom_radius_m_) {
                spdlog::debug("[LDM-Core] Dropping {} outside AoM id={}", typeName(obj.type), obj.id.object_id);
                it = objects_.erase(it);
                result.changed = true;
                continue;
            }
        }
        ++it;
    }

    for (const auto& entry : objects_) {
        if (entry.second.type == LdmDataType::MAP_INTERSECTION) {
            ++result.mapem_count;
        } else if (entry.second.type == LdmDataType::VAM) {
            ++result.vam_count;
        }
    }
    result.mapem_changed = before_mapem != result.mapem_count;
    result.vam_changed = before_vam != result.vam_count;
    return result;
}

std::string LdmStore::hashEntries(const std::vector<std::string>& entries) {
    uint64_t hash = 1469598103934665603ULL;
    for (const auto& entry : entries) {
        for (unsigned char c : entry) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ULL;
        }
        hash ^= 0x1fULL;
        hash *= 1099511628211ULL;
    }
    return std::to_string(hash);
}

bool LdmStore::ingestCpm(const ldm::cpm::ParsedFrame& frame) {
    if (frame.station_id == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const double update_ts = frame.timestamp_unix_s > 0.0 ? frame.timestamp_unix_s : 0.0;
    const double receive_ts = frame.receive_timestamp_unix_s > 0.0 ? frame.receive_timestamp_unix_s : update_ts;

    auto station_it = cpm_stations_.find(frame.station_id);
    if (station_it == cpm_stations_.end()) {
        if (cpm_config_.max_stations > 0 && static_cast<int>(cpm_stations_.size()) >= cpm_config_.max_stations) {
            auto oldest = cpm_stations_.begin();
            for (auto it = cpm_stations_.begin(); it != cpm_stations_.end(); ++it) {
                if (it->second.last_cpm_timestamp_unix_s < oldest->second.last_cpm_timestamp_unix_s) {
                    oldest = it;
                }
            }
            cpm_stations_.erase(oldest);
        }
        CpmStationState inserted;
        inserted.station_id = frame.station_id;
        inserted.scope = frame.scope;
        station_it = cpm_stations_.emplace(frame.station_id, std::move(inserted)).first;
    }

    auto& station = station_it->second;
    bool changed = false;

    if (station.scope != frame.scope) {
        station.scope = frame.scope;
        changed = true;
    }

    if (station.station_type != frame.station_type) {
        station.station_type = frame.station_type;
        changed = true;
    }

    if (station.last_receive_timestamp_unix_s > 0.0 && receive_ts > station.last_receive_timestamp_unix_s) {
        const double delta_s = receive_ts - station.last_receive_timestamp_unix_s;
        if (delta_s > 0.0) {
            const double instant_rate_hz = std::min(10.0, 1.0 / delta_s);
            station.observed_cpm_rate_hz =
                (kCadenceEwmaAlpha * instant_rate_hz) + ((1.0 - kCadenceEwmaAlpha) * station.observed_cpm_rate_hz);
        }
    }
    station.last_receive_timestamp_unix_s = receive_ts;

    station.last_cpm_timestamp_unix_s = update_ts;
    if (frame.reference_position.has_position) {
        station.last_reference_position = frame.reference_position;
    }

    if (frame.has_sic) {
        const auto sic_hash = hashEntries(frame.sic_entries);
        if (sic_hash != station.sic_hash) {
            station.sic_payload = frame.sic_entries;
            station.sic_hash = sic_hash;
            changed = true;
        }
        station.sic_last_update_s = update_ts;
    }

    if (frame.has_perception_regions) {
        const auto region_hash = hashEntries(frame.perception_region_entries);
        if (region_hash != station.perception_regions_hash) {
            station.perception_regions_payload = frame.perception_region_entries;
            station.perception_regions_hash = region_hash;
            changed = true;
        }
        station.perception_regions_last_update_s = update_ts;
    }

    if (frame.has_objects) {
        for (const auto& parsed : frame.objects) {
            auto obj_it = station.objects_by_id.find(parsed.object_id);
            if (obj_it == station.objects_by_id.end()) {
                CpmObjectState created;
                created.object_id = parsed.object_id;
                created.last_update_s = parsed.timestamp_unix_s > 0.0 ? parsed.timestamp_unix_s : update_ts;
                created.has_position = parsed.has_position;
                created.latitude = parsed.latitude;
                created.longitude = parsed.longitude;
                station.objects_by_id.emplace(parsed.object_id, created);
                changed = true;
                continue;
            }

            auto& object = obj_it->second;
            bool object_changed = false;
            const double new_ts = parsed.timestamp_unix_s > 0.0 ? parsed.timestamp_unix_s : update_ts;
            if (!nearlyEqual(object.last_update_s, new_ts, 1e-6)) {
                object.last_update_s = new_ts;
                object_changed = true;
            }
            if (object.has_position != parsed.has_position) {
                object.has_position = parsed.has_position;
                object_changed = true;
            }
            if (parsed.has_position &&
                (!nearlyEqual(object.latitude, parsed.latitude) || !nearlyEqual(object.longitude, parsed.longitude))) {
                object.latitude = parsed.latitude;
                object.longitude = parsed.longitude;
                object_changed = true;
            }
            changed = changed || object_changed;
        }

        if (cpm_config_.max_objects_per_station > 0 &&
            static_cast<int>(station.objects_by_id.size()) > cpm_config_.max_objects_per_station) {
            while (static_cast<int>(station.objects_by_id.size()) > cpm_config_.max_objects_per_station) {
                auto oldest = station.objects_by_id.begin();
                for (auto it = station.objects_by_id.begin(); it != station.objects_by_id.end(); ++it) {
                    if (it->second.last_update_s < oldest->second.last_update_s) {
                        oldest = it;
                    }
                }
                station.objects_by_id.erase(oldest);
                changed = true;
            }
        }
    }

    return changed;
}

bool LdmStore::passesCpmAoi(const CpmStationState& station, const ldm::cpm::QueryInput& input) const {
    if (!input.has_aoi) {
        return true;
    }
    if (!station.last_reference_position.has_position) {
        return false;
    }
    const double radius = input.aoi_radius_m > 0.0 ? input.aoi_radius_m : 0.0;
    if (radius <= 0.0) {
        return true;
    }
    const double distance = haversineMeters(input.aoi_lat,
                                            input.aoi_lon,
                                            station.last_reference_position.latitude,
                                            station.last_reference_position.longitude);
    return distance <= radius;
}

bool LdmStore::sweepCpmLocked(double now_unix_s) {
    bool changed = false;
    const double object_ttl_s = cpm_config_.object_ttl_ms > 0 ? static_cast<double>(cpm_config_.object_ttl_ms) / 1000.0 : -1.0;
    const double sic_ttl_s = cpm_config_.sic_ttl_s > 0 ? static_cast<double>(cpm_config_.sic_ttl_s) : -1.0;
    const double region_ttl_s =
        cpm_config_.perception_region_ttl_s > 0 ? static_cast<double>(cpm_config_.perception_region_ttl_s) : -1.0;

    for (auto& station_entry : cpm_stations_) {
        auto& station = station_entry.second;
        bool station_changed = false;

        if (object_ttl_s > 0.0) {
            for (auto it = station.objects_by_id.begin(); it != station.objects_by_id.end();) {
                if ((now_unix_s - it->second.last_update_s) > object_ttl_s) {
                    it = station.objects_by_id.erase(it);
                    station_changed = true;
                    continue;
                }
                ++it;
            }
        }

        if (sic_ttl_s > 0.0 && !station.sic_payload.empty() && station.sic_last_update_s > 0.0 &&
            (now_unix_s - station.sic_last_update_s) > sic_ttl_s) {
            station.sic_payload.clear();
            station.sic_hash.clear();
            station.sic_last_update_s = 0.0;
            station_changed = true;
        }

        if (region_ttl_s > 0.0 && !station.perception_regions_payload.empty() &&
            station.perception_regions_last_update_s > 0.0 &&
            (now_unix_s - station.perception_regions_last_update_s) > region_ttl_s) {
            station.perception_regions_payload.clear();
            station.perception_regions_hash.clear();
            station.perception_regions_last_update_s = 0.0;
            station_changed = true;
        }

        if (station_changed) {
            changed = true;
        }
    }
    return changed;
}

bool LdmStore::sweepCpm(double now_unix_s) {
    std::lock_guard<std::mutex> lock(mutex_);
    return sweepCpmLocked(now_unix_s);
}

ldm::cpm::QueryResult LdmStore::queryCpm(const ldm::cpm::QueryInput& input, double now_unix_s) const {
    std::lock_guard<std::mutex> lock(mutex_);

    ldm::cpm::QueryResult result;

    const bool want_objects = (input.query_type == "cpm_state" || input.query_type == "cpm_objects");
    const bool want_regions = (input.query_type == "cpm_state" || input.query_type == "cpm_perception_regions");
    const bool want_sensors = (input.query_type == "cpm_state");

    const double max_object_age_s =
        input.filter.max_object_age_ms > 0 ? static_cast<double>(input.filter.max_object_age_ms) / 1000.0 : -1.0;
    const double max_sic_age_s = input.filter.max_sic_age_s > 0 ? static_cast<double>(input.filter.max_sic_age_s) : -1.0;
    const double max_region_age_s =
        input.filter.max_perception_region_age_s > 0 ? static_cast<double>(input.filter.max_perception_region_age_s) : -1.0;

    int remaining_objects = input.filter.limit_objects >= 0 ? input.filter.limit_objects : std::numeric_limits<int>::max();
    int remaining_sensors = input.filter.limit_sensors >= 0 ? input.filter.limit_sensors : std::numeric_limits<int>::max();
    int remaining_regions = input.filter.limit_regions >= 0 ? input.filter.limit_regions : std::numeric_limits<int>::max();

    std::unordered_set<uint64_t> station_id_filter;
    station_id_filter.reserve(input.filter.station_ids.size());
    for (const auto station_id : input.filter.station_ids) {
        station_id_filter.insert(station_id);
    }

    for (const auto& station_entry : cpm_stations_) {
        const auto& station = station_entry.second;
        const bool is_own = station.scope == ldm::cpm::Scope::OWN;
        if (is_own && !input.filter.include_own) {
            continue;
        }
        if (!is_own && !input.filter.include_external) {
            continue;
        }
        if (!station_id_filter.empty() && station_id_filter.count(station.station_id) == 0) {
            continue;
        }
        if (!passesCpmAoi(station, input)) {
            continue;
        }

        std::vector<std::string> sensors;
        std::vector<std::string> regions;
        std::vector<uint64_t> objects;

        if (want_sensors && remaining_sensors > 0 && !station.sic_payload.empty()) {
            const double age_s = now_unix_s - station.sic_last_update_s;
            if (max_sic_age_s < 0.0 || age_s <= max_sic_age_s) {
                const auto count = std::min<int>(remaining_sensors, static_cast<int>(station.sic_payload.size()));
                sensors.insert(sensors.end(), station.sic_payload.begin(), station.sic_payload.begin() + count);
                remaining_sensors -= count;
            }
        }

        if (want_regions && remaining_regions > 0 && !station.perception_regions_payload.empty()) {
            const double age_s = now_unix_s - station.perception_regions_last_update_s;
            if (max_region_age_s < 0.0 || age_s <= max_region_age_s) {
                const auto count =
                    std::min<int>(remaining_regions, static_cast<int>(station.perception_regions_payload.size()));
                regions.insert(regions.end(),
                               station.perception_regions_payload.begin(),
                               station.perception_regions_payload.begin() + count);
                remaining_regions -= count;
            }
        }

        if (want_objects && remaining_objects > 0 && !station.objects_by_id.empty()) {
            objects.reserve(station.objects_by_id.size());
            for (const auto& [object_id, object] : station.objects_by_id) {
                const double age_s = now_unix_s - object.last_update_s;
                if (max_object_age_s >= 0.0 && age_s > max_object_age_s) {
                    continue;
                }
                objects.push_back(object_id);
            }
            std::sort(objects.begin(), objects.end());
            if (static_cast<int>(objects.size()) > remaining_objects) {
                objects.resize(static_cast<std::size_t>(remaining_objects));
            }
            remaining_objects -= static_cast<int>(objects.size());
        }

        bool include_station = true;
        if (input.query_type == "cpm_objects") {
            include_station = !objects.empty();
        } else if (input.query_type == "cpm_perception_regions") {
            include_station = !regions.empty();
        }
        if (!include_station) {
            continue;
        }

        ++result.station_count;
        result.object_count += static_cast<int>(objects.size());
        result.sensor_count += static_cast<int>(sensors.size());
        result.perception_region_count += static_cast<int>(regions.size());

        ldm::cpm::StationSnapshot snapshot;
        snapshot.station_id = station.station_id;
        snapshot.station_type = station.station_type;
        snapshot.scope = ldm::cpm::ScopeToString(station.scope);
        snapshot.last_cpm_timestamp_unix_s = station.last_cpm_timestamp_unix_s;
        snapshot.observed_cpm_rate_hz = station.observed_cpm_rate_hz;
        snapshot.sender_reference_position = station.last_reference_position;
        snapshot.sensor_information = std::move(sensors);
        snapshot.perception_regions = std::move(regions);
        snapshot.objects = std::move(objects);
        result.stations.push_back(std::move(snapshot));
    }

    result.has_cpm = result.station_count > 0;
    return result;
}

std::size_t LdmStore::cpmStationCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cpm_stations_.size();
}

std::size_t LdmStore::cpmObjectCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t total = 0;
    for (const auto& station_entry : cpm_stations_) {
        total += station_entry.second.objects_by_id.size();
    }
    return total;
}

std::size_t LdmStore::cpmSensorCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t total = 0;
    for (const auto& station_entry : cpm_stations_) {
        total += station_entry.second.sic_payload.size();
    }
    return total;
}

std::size_t LdmStore::cpmPerceptionRegionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t total = 0;
    for (const auto& station_entry : cpm_stations_) {
        total += station_entry.second.perception_regions_payload.size();
    }
    return total;
}

}  // namespace ldm::model
