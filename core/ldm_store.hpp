#pragma once

#include "cpm_message.hpp"
#include "ldm_model.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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
    int station_type{0};
    std::string scope;
    double last_cpm_timestamp_unix_s{0.0};
    double observed_cpm_rate_hz{1.0};
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

}  // namespace ldm::cpm

namespace ldm::model {

class LdmStore {
public:
    struct MaintenanceResult {
        bool changed{false};
        bool mapem_changed{false};
        bool vam_changed{false};
        std::size_t mapem_count{0};
        std::size_t vam_count{0};
    };

    struct CpmConfig {
        int object_ttl_ms{5000};
        int sic_ttl_s{1800};
        int perception_region_ttl_s{1800};
        int max_objects_per_station{2000};
        int max_stations{512};
    };

    explicit LdmStore(double aom_radius_m = 1000.0);
    LdmStore(double aom_radius_m, const CpmConfig& cpm_config);

    bool upsertObject(const LdmDataObject& obj);
    void removeObject(const LdmDataObjectId& id);

    std::vector<LdmDataObject> queryByAreaAndFilter(
        const Geometry& areaOfInterest,
        const std::function<bool(const LdmDataObject&)>& filter);

    std::size_t size() const;
    std::size_t mapemCount() const;
    std::size_t vamCount() const;

    std::vector<LdmLane> findLanesNearPosition(double lat, double lon, double radius_m) const;
    std::optional<LdmLane> findClosestLane(double lat, double lon) const;
    std::optional<LdmIntersection> findIntersectionById(int intersection_id) const;

    void setStationReference(double lat, double lon);
    MaintenanceResult runMaintenance();

    bool ingestCpm(const ldm::cpm::ParsedFrame& frame);
    bool sweepCpm(double now_unix_s);
    ldm::cpm::QueryResult queryCpm(const ldm::cpm::QueryInput& input, double now_unix_s) const;
    std::size_t cpmStationCount() const;
    std::size_t cpmObjectCount() const;
    std::size_t cpmSensorCount() const;
    std::size_t cpmPerceptionRegionCount() const;

private:
    struct CpmObjectState {
        uint64_t object_id{0};
        double last_update_s{0.0};
        bool has_position{false};
        double latitude{0.0};
        double longitude{0.0};
    };

    struct CpmStationState {
        uint64_t station_id{0};
        int station_type{0};
        ldm::cpm::Scope scope{ldm::cpm::Scope::EXTERNAL};
        double last_cpm_timestamp_unix_s{0.0};
        double last_receive_timestamp_unix_s{0.0};
        double observed_cpm_rate_hz{1.0};
        ldm::cpm::ReferencePosition last_reference_position;

        std::vector<std::string> sic_payload;
        std::string sic_hash;
        double sic_last_update_s{0.0};

        std::vector<std::string> perception_regions_payload;
        std::string perception_regions_hash;
        double perception_regions_last_update_s{0.0};

        std::unordered_map<uint64_t, CpmObjectState> objects_by_id;
    };

    bool sweepCpmLocked(double now_unix_s);
    bool passesCpmAoi(const CpmStationState& station, const ldm::cpm::QueryInput& input) const;
    static std::string hashEntries(const std::vector<std::string>& entries);

    double aom_radius_m_;
    double station_lat_{0.0};
    double station_lon_{0.0};

    std::unordered_map<LdmDataObjectId, LdmDataObject> objects_;
    std::unordered_map<int, LdmIntersection> intersections_;
    std::unordered_map<int, LdmLane> lanes_;

    CpmConfig cpm_config_;
    std::unordered_map<uint64_t, CpmStationState> cpm_stations_;

    mutable std::mutex mutex_;
};

}  // namespace ldm::model
