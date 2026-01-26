#pragma once

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

struct LocatorConfig {
    int station_type{15};
    int station_id{0};
    double station_latitude{0.0};
    double station_longitude{0.0};
    std::string location_provider{"static"};
    std::string mqtt_host;
    int mqtt_port{0};
    std::string mqtt_topic;
    int domain_id{0};
    std::string dds_topic;
    int update_interval_ms{1000};
};

class Locator {
public:
    explicit Locator(const LocatorConfig& cfg);
    ~Locator();

    void run();
    std::pair<double, double> currentPosition() const;
    float heading() const;
    int stationType() const;
    int stationId() const;
    bool isDynamic() const;
    void updatePosition(double lat, double lon);

private:
    void runLoop();
    void onMessageMqtt(const std::string& topic, const std::string& message);
    void onMessageDds(const std::string& topic, const std::string& message);
    void parseAndUpdateLocation(const std::string& topic, const std::string& message);
    void stop();

    LocatorConfig cfg_;
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
