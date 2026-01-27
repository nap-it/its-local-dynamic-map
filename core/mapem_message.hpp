#pragma once

#include <string>

#include <rapidjson/document.h>

namespace ldm::mapem {

struct Filter {
    bool include_vehicle_lane = true;
    bool include_sidewalk = true;
    bool include_waiting_sidewalk = false;
    bool include_crosswalk = true;
    bool include_bike_lane = true;
    bool include_parking = true;
    bool include_median = true;
    bool include_unknown_lane = true;
    bool include_connections = true;
    bool include_maneuver = true;
    bool include_shared_with = true;
    bool include_signal_groups = true;
    bool include_speed_limits = true;
    bool include_overlays = true;
    bool include_directional_use = true;
};

Filter ParseFilter(const rapidjson::Value& root);

bool ExtractNormalizedMapem(const std::string& payload, std::string& out_json);

bool BuildFilteredMapem(const std::string& mapem_json, const Filter& filter, std::string& out_json);

}  // namespace ldm::mapem
