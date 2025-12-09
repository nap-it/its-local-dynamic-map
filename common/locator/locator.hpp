#pragma once

#include "../config/config.hpp"

#include "fastdds-cpp-wrapper/dds.hpp"
#include "mqttwrapper.h"
#include "rapidjson/document.h"
#include "spdlog/spdlog.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace ldm::locator {

enum class ProviderType { STATIC, MQTT, DDS };

class Locator {
public:
    explicit Locator(const config::LocatorConfig& cfg);
    ~Locator();

    void run();
    std::pair<double, double> currentPosition() const;
    float heading() const;
    int stationType() const;
    int stationId() const;
    void updatePosition(double lat, double lon);

private:
    void runLoop();
    void onMessageMqtt(const std::string& topic, const std::string& message);
    void onMessageDds(const std::string& topic, const std::string& message);
    void parseAndUpdateLocation(const std::string& topic, const std::string& message);
    void stop();

    config::LocatorConfig cfg_;
    ProviderType provider_{ProviderType::STATIC};
    mutable std::mutex mutex_;

    double lat_{0.0};
    double lon_{0.0};
    float heading_{0.0f};
    int station_type_{15};
    int station_id_{0};

    std::unique_ptr<MqttWrapper> mqtt_;
    std::unique_ptr<Dds> dds_;
    std::string mqtt_topic_;
    std::string dds_topic_;

    std::thread locator_thread_;
    std::atomic<bool> running_{false};
};

}  // namespace ldm::locator
