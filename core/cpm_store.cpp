#include "cpm_store.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

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

}  // namespace

namespace ldm::cpm {

CpmStateStore::CpmStateStore(const Config& config) : config_(config) {}

std::string CpmStateStore::hashEntries(const std::vector<std::string>& entries) const {
    // FNV-1a hash over canonical serialized entries.
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

bool CpmStateStore::ingest(const ParsedFrame& frame) {
    if (frame.station_id == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const double update_ts = frame.timestamp_unix_s > 0.0 ? frame.timestamp_unix_s : 0.0;
    const double receive_ts =
        frame.receive_timestamp_unix_s > 0.0 ? frame.receive_timestamp_unix_s : update_ts;

    auto station_it = stations_.find(frame.station_id);
    if (station_it == stations_.end()) {
        if (config_.max_stations > 0 && static_cast<int>(stations_.size()) >= config_.max_stations) {
            auto oldest = stations_.begin();
            for (auto it = stations_.begin(); it != stations_.end(); ++it) {
                if (it->second.last_cpm_timestamp_unix_s < oldest->second.last_cpm_timestamp_unix_s) {
                    oldest = it;
                }
            }
            stations_.erase(oldest);
        }
        StationCpmState inserted;
        inserted.station_id = frame.station_id;
        inserted.scope = frame.scope;
        station_it = stations_.emplace(frame.station_id, std::move(inserted)).first;
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
                (kCadenceEwmaAlpha * instant_rate_hz) +
                ((1.0 - kCadenceEwmaAlpha) * station.observed_cpm_rate_hz);
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
                ObjectState created;
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

        if (config_.max_objects_per_station > 0 &&
            static_cast<int>(station.objects_by_id.size()) > config_.max_objects_per_station) {
            while (static_cast<int>(station.objects_by_id.size()) > config_.max_objects_per_station) {
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

bool CpmStateStore::passesAoi(const StationCpmState& station, const QueryInput& input) const {
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

bool CpmStateStore::sweepLocked(double now_unix_s) {
    bool changed = false;
    const double object_ttl_s = config_.object_ttl_ms > 0 ? static_cast<double>(config_.object_ttl_ms) / 1000.0 : -1.0;
    const double sic_ttl_s = config_.sic_ttl_s > 0 ? static_cast<double>(config_.sic_ttl_s) : -1.0;
    const double region_ttl_s =
        config_.perception_region_ttl_s > 0 ? static_cast<double>(config_.perception_region_ttl_s) : -1.0;

    for (auto& station_entry : stations_) {
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

bool CpmStateStore::sweep(double now_unix_s) {
    std::lock_guard<std::mutex> lock(mutex_);
    return sweepLocked(now_unix_s);
}

QueryResult CpmStateStore::query(const QueryInput& input, double now_unix_s) const {
    std::lock_guard<std::mutex> lock(mutex_);

    QueryResult result;

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

    for (const auto& station_entry : stations_) {
        const auto& station = station_entry.second;
        const bool is_own = station.scope == Scope::OWN;
        if (is_own && !input.filter.include_own) {
            continue;
        }
        if (!is_own && !input.filter.include_external) {
            continue;
        }
        if (!station_id_filter.empty() && station_id_filter.count(station.station_id) == 0) {
            continue;
        }
        if (!passesAoi(station, input)) {
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

        StationSnapshot snapshot;
        snapshot.station_id = station.station_id;
        snapshot.station_type = station.station_type;
        snapshot.scope = ScopeToString(station.scope);
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

std::size_t CpmStateStore::stationCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stations_.size();
}

std::size_t CpmStateStore::objectCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t total = 0;
    for (const auto& station_entry : stations_) {
        total += station_entry.second.objects_by_id.size();
    }
    return total;
}

}  // namespace ldm::cpm
