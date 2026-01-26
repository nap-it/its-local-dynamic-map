#pragma once

#include "ldm_model.hpp"

#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ldm::model {

class LdmStore {
public:
    explicit LdmStore(double aom_radius_m = 1000.0);

    void upsertObject(const LdmDataObject& obj);
    void removeObject(const LdmDataObjectId& id);

    std::vector<LdmDataObject> queryByAreaAndFilter(
        const Geometry& areaOfInterest,
        const std::function<bool(const LdmDataObject&)>& filter);

    std::vector<LdmLane> findLanesNearPosition(double lat, double lon, double radius_m) const;
    std::optional<LdmLane> findClosestLane(double lat, double lon) const;
    std::optional<LdmIntersection> findIntersectionById(int intersection_id) const;

    void setStationReference(double lat, double lon);
    void runMaintenance();

private:
    double aom_radius_m_;
    double station_lat_{0.0};
    double station_lon_{0.0};

    std::unordered_map<LdmDataObjectId, LdmDataObject> objects_;
    std::unordered_map<int, LdmIntersection> intersections_;
    std::unordered_map<int, LdmLane> lanes_;

    mutable std::mutex mutex_;
};

}  // namespace ldm::model
