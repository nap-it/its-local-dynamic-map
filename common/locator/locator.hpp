#pragma once

#include "../config/config.hpp"

#include <mutex>
#include <utility>

namespace ldm::locator {

class Locator {
public:
    explicit Locator(const config::LocatorConfig& cfg);

    std::pair<double, double> currentPosition() const;
    void updatePosition(double lat, double lon);

private:
    config::LocatorConfig cfg_;
    mutable std::mutex mutex_;
    double lat_{0.0};
    double lon_{0.0};
};

}  // namespace ldm::locator
