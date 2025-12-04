#include "Locator.hpp"

namespace ldm::locator {

Locator::Locator(const config::LocatorConfig& cfg) : cfg_(cfg), lat_(cfg.station_latitude), lon_(cfg.station_longitude) {}

std::pair<double, double> Locator::currentPosition() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {lat_, lon_};
}

void Locator::updatePosition(double lat, double lon) {
    std::lock_guard<std::mutex> lock(mutex_);
    lat_ = lat;
    lon_ = lon;
}

}  // namespace ldm::locator
