#pragma once

#include <cstdint>
#include <string>

namespace ldm::vam {

struct VamSummary {
    uint64_t station_id{0};
    double timestamp_s{0.0};
    double lat{0.0};
    double lon{0.0};
    bool has_orientation{false};
    double orientation{0.0};
};

bool ExtractVamSummary(const std::string& payload, VamSummary& out);

}  // namespace ldm::vam
