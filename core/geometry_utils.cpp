#include "geometry_utils.hpp"

#include <cmath>

namespace ldm::geometry {

void toLocalEnu(double ref_lat, double ref_lon, const std::vector<double>& lats, const std::vector<double>& lons,
                std::vector<double>& xs, std::vector<double>& ys) {
    constexpr double kEarthRadiusMeters = 6378137.0;
    double refLatRad = ref_lat * M_PI / 180.0;
    for (size_t i = 0; i < lats.size() && i < lons.size(); ++i) {
        double dLat = (lats[i] - ref_lat) * M_PI / 180.0;
        double dLon = (lons[i] - ref_lon) * M_PI / 180.0;
        double x = dLon * std::cos(refLatRad) * kEarthRadiusMeters;
        double y = dLat * kEarthRadiusMeters;
        xs.push_back(x);
        ys.push_back(y);
    }
}

ldm::model::Geometry makePoint(double lat, double lon) {
    ldm::model::Geometry g;
    g.type = ldm::model::Geometry::Type::POINT;
    g.lats.push_back(lat);
    g.lons.push_back(lon);
    toLocalEnu(lat, lon, g.lats, g.lons, g.xs, g.ys);
    return g;
}

}  // namespace ldm::geometry
