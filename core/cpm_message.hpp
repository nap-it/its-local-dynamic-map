#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ldm::cpm {

enum class Scope {
    OWN,
    EXTERNAL
};

struct ReferencePosition {
    bool has_position{false};
    double latitude{0.0};
    double longitude{0.0};
};

struct ParsedObject {
    uint64_t object_id{0};
    double timestamp_unix_s{0.0};
    bool has_position{false};
    double latitude{0.0};
    double longitude{0.0};
};

struct ParsedFrame {
    uint64_t station_id{0};
    Scope scope{Scope::EXTERNAL};
    double timestamp_unix_s{0.0};
    ReferencePosition reference_position;

    bool has_sic{false};
    std::vector<std::string> sic_entries;

    bool has_perception_regions{false};
    std::vector<std::string> perception_region_entries;

    bool has_objects{false};
    std::vector<ParsedObject> objects;
};

enum class ParseError {
    NONE,
    INVALID_JSON,
    MISSING_STATION_ID,
    MISSING_CPM_PAYLOAD
};

bool ParseCpmFrame(const std::string& payload,
                   Scope scope,
                   uint64_t local_station_id,
                   double now_unix_s,
                   ParsedFrame& out,
                   ParseError& err);

const char* ScopeToString(Scope scope);

}  // namespace ldm::cpm

