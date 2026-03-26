#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "cpm_message.hpp"

namespace ldm::cpm {

struct CpmFilter {
    bool include_own{true};
    bool include_external{true};
    int max_object_age_ms{5000};
    int max_sic_age_s{1800};
    int max_perception_region_age_s{1800};
    std::vector<uint64_t> station_ids;
    int limit_objects{500};
    int limit_sensors{256};
    int limit_regions{256};
};

struct QueryInput {
    std::string query_type{"cpm_state"};
    bool has_aoi{false};
    double aoi_lat{0.0};
    double aoi_lon{0.0};
    double aoi_radius_m{0.0};
    CpmFilter filter{};
};

struct StationSnapshot {
    uint64_t station_id{0};
    std::string scope;
    double last_cpm_timestamp_unix_s{0.0};
    ReferencePosition sender_reference_position;
    std::vector<std::string> sensor_information;
    std::vector<std::string> perception_regions;
    std::vector<uint64_t> objects;
};

struct QueryResult {
    bool has_cpm{false};
    int station_count{0};
    int object_count{0};
    int sensor_count{0};
    int perception_region_count{0};
    std::vector<StationSnapshot> stations;
};

class CpmStateStore {
public:
    struct Config {
        int object_ttl_ms{5000};
        int sic_ttl_s{1800};
        int perception_region_ttl_s{1800};
        int max_objects_per_station{2000};
        int max_stations{512};
    };

    explicit CpmStateStore(const Config& config);

    bool ingest(const ParsedFrame& frame);
    bool sweep(double now_unix_s);
    QueryResult query(const QueryInput& input, double now_unix_s) const;

    std::size_t stationCount() const;
    std::size_t objectCount() const;

private:
    struct ObjectState {
        uint64_t object_id{0};
        double last_update_s{0.0};
        bool has_position{false};
        double latitude{0.0};
        double longitude{0.0};
    };

    struct StationCpmState {
        uint64_t station_id{0};
        Scope scope{Scope::EXTERNAL};
        double last_cpm_timestamp_unix_s{0.0};
        ReferencePosition last_reference_position;

        std::vector<std::string> sic_payload;
        std::string sic_hash;
        double sic_last_update_s{0.0};

        std::vector<std::string> perception_regions_payload;
        std::string perception_regions_hash;
        double perception_regions_last_update_s{0.0};

        std::unordered_map<uint64_t, ObjectState> objects_by_id;
    };

    bool sweepLocked(double now_unix_s);
    bool passesAoi(const StationCpmState& station, const QueryInput& input) const;
    std::string hashEntries(const std::vector<std::string>& entries) const;

    Config config_;
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, StationCpmState> stations_;
};

}  // namespace ldm::cpm
