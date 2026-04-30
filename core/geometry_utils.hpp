#pragma once

#include "ldm_model.hpp"

namespace ldm::geometry {

// Convert WGS84 lat/lon to ENU meters using equirectangular approximation around ref point.
void toLocalEnu(double ref_lat, double ref_lon, const std::vector<double>& lats, const std::vector<double>& lons,
                std::vector<double>& xs, std::vector<double>& ys);

ldm::model::Geometry makePoint(double lat, double lon);

}  // namespace ldm::geometry
