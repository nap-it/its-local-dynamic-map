#include "transport.hpp"

#include <algorithm>
#include <cctype>

#include "fastdds_transport.hpp"
#include "mqtt_transport.hpp"
#include "zenoh_transport.hpp"

#include "../config/config.hpp"

namespace ldm::messaging {

namespace {

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

}  // namespace

std::unique_ptr<Transport> createTransport(const config::MessagingConfig& cfg) {
    auto backend = toLower(cfg.backend);
    if (backend == "mqtt") {
        return std::make_unique<MqttTransport>(cfg);
    }
    if (backend == "zenoh") {
        return std::make_unique<ZenohTransport>(cfg);
    }
    return std::make_unique<FastDdsTransport>(cfg);
}

}  // namespace ldm::messaging
