#include "locator.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace rj = rapidjson;

namespace {

std::string toLower(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v;
}

ldm::locator::ProviderType parseProvider(const std::string& provider) {
    const auto lowered = toLower(provider);
    if (lowered == "mqtt") {
        return ldm::locator::ProviderType::MQTT;
    }
    if (lowered == "dds") {
        return ldm::locator::ProviderType::DDS;
    }
    return ldm::locator::ProviderType::STATIC;
}

}  // namespace

namespace ldm::locator {

Locator::Locator(const LocatorConfig& cfg)
    : cfg_(cfg),
      provider_(parseProvider(cfg.location_provider)),
      lat_(cfg.station_latitude),
      lon_(cfg.station_longitude),
      heading_(0.0f),
      station_type_(cfg.station_type),
      station_id_(cfg.station_id),
      mqtt_topic_(cfg.mqtt_topic.empty() ? "vanetza/in/cam" : cfg.mqtt_topic),
      dds_topic_(cfg.dds_topic.empty() ? "vanetza/in/cam" : cfg.dds_topic) {
    spdlog::info("[Locator] Initializing provider={} station_type={} station_id={} lat={} lon={}",
                 cfg.location_provider, station_type_, station_id_, lat_, lon_);

    if (provider_ == ProviderType::STATIC) {
        spdlog::info("[Locator] Using STATIC provider. Fixed location: lat {:.6f}, lon {:.6f}", lat_, lon_);
        return;
    }

    if (provider_ == ProviderType::MQTT) {
        data_mqtt_server mqtt_cfg;
        const int port = cfg_.mqtt_port == 0 ? 1883 : cfg_.mqtt_port;
        const std::string host = cfg_.mqtt_host.empty() ? "127.0.0.1" : cfg_.mqtt_host;
        mqtt_cfg.address = "tcp://" + host + ":" + std::to_string(port);
        mqtt_cfg.client_id = "LDM-Locator-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        mqtt_cfg.subscription_topic.clear();

        std::unordered_set<std::string> topics = {mqtt_topic_, "vanetza/in/cam_full", "vanetza/in/vam", "vanetza/own/cam"};
        topics.erase("");  // drop empty topics if any
        for (const auto& t : topics) {
            mqtt_cfg.subscription_topic.push_back(t);
        }

        mqtt_ = std::make_unique<MqttWrapper>(mqtt_cfg, [this](const std::string& topic, const std::string& message) {
            this->onMessageMqtt(topic, message);
        });

        while (mqtt_ && !mqtt_->is_connected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        spdlog::info("[Locator] MQTT client connected to {} subscribing to {} topics", mqtt_cfg.address, mqtt_cfg.subscription_topic.size());
    } else if (provider_ == ProviderType::DDS) {
        dds_ = std::make_unique<Dds>("Locator", cfg_.domain_id, [this](const std::string& topic, const std::string& message) {
            this->onMessageDds(topic, message);
        });
        dds_->subscribe(dds_topic_);
        spdlog::info("[Locator] DDS client subscribed to topic {} on domain {}", dds_topic_, cfg_.domain_id);
    } else {
        throw std::runtime_error("[Locator] Unknown provider type");
    }
}

Locator::~Locator() {
    stop();
    if (mqtt_) {
        mqtt_.reset();
    }
    if (dds_) {
        dds_.reset();
    }
}

void Locator::run() {
    if (running_.exchange(true)) {
        return;
    }
    locator_thread_ = std::thread(&Locator::runLoop, this);
    spdlog::info("[Locator] run loop started.");
}

std::pair<double, double> Locator::currentPosition() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {lat_, lon_};
}

float Locator::heading() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (provider_ == ProviderType::STATIC) {
        return 0.0f;
    }
    if (heading_ < 0.0f || heading_ > 360.0f) {
        return 0.0f;
    }
    return heading_;
}

int Locator::stationType() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return station_type_;
}

int Locator::stationId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return station_id_;
}

bool Locator::isDynamic() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return provider_ != ProviderType::STATIC;
}

void Locator::updatePosition(double lat, double lon) {
    std::lock_guard<std::mutex> lock(mutex_);
    lat_ = lat;
    lon_ = lon;
}

void Locator::runLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void Locator::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (locator_thread_.joinable()) {
        locator_thread_.join();
    }
}

void Locator::onMessageMqtt(const std::string& topic, const std::string& message) {
    spdlog::debug("[Locator] MQTT message received on topic '{}'", topic);
    parseAndUpdateLocation(topic, message);
}

void Locator::onMessageDds(const std::string& topic, const std::string& message) {
    spdlog::debug("[Locator] DDS message received on topic '{}'", topic);
    parseAndUpdateLocation(topic, message);
}

void Locator::parseAndUpdateLocation(const std::string& topic, const std::string& message) {
    try {
        rj::Document doc;
        doc.Parse(message.c_str());
        if (doc.HasParseError()) {
            spdlog::error("[Locator] Parse error in message");
            return;
        }

        double lat = lat_;
        double lon = lon_;
        float heading = 0.0f;

        if (topic == "vanetza/in/cam" || topic == "vanetza/own/cam") {
            lat = (doc.HasMember("latitude") && doc["latitude"].IsDouble()) ? doc["latitude"].GetDouble() : lat_;
            lon = (doc.HasMember("longitude") && doc["longitude"].IsDouble()) ? doc["longitude"].GetDouble() : lon_;
            heading = (doc.HasMember("heading") && doc["heading"].IsFloat()) ? doc["heading"].GetFloat() : 0.0f;
        } else if (topic == "vanetza/in/cam_full") {
            if (doc.HasMember("camParameters") && doc["camParameters"].IsObject()) {
                const rj::Value& camParameters = doc["camParameters"];
                if (camParameters.HasMember("basicContainer") && camParameters["basicContainer"].IsObject()) {
                    const rj::Value& basicContainer = camParameters["basicContainer"];
                    if (basicContainer.HasMember("referencePosition") && basicContainer["referencePosition"].IsObject()) {
                        const rj::Value& referencePosition = basicContainer["referencePosition"];
                        lat = (referencePosition.HasMember("latitude") && referencePosition["latitude"].IsDouble())
                                  ? referencePosition["latitude"].GetDouble()
                                  : lat_;
                        lon = (referencePosition.HasMember("longitude") && referencePosition["longitude"].IsDouble())
                                  ? referencePosition["longitude"].GetDouble()
                                  : lon_;
                    }
                }
                if (camParameters.HasMember("highFrequencyContainer") && camParameters["highFrequencyContainer"].IsObject()) {
                    const rj::Value& hfContainer = camParameters["highFrequencyContainer"];
                    if (hfContainer.HasMember("basicVehicleContainerHighFrequency") && hfContainer["basicVehicleContainerHighFrequency"].IsObject()) {
                        const rj::Value& basicVehicle = hfContainer["basicVehicleContainerHighFrequency"];
                        if (basicVehicle.HasMember("heading") && basicVehicle["heading"].IsObject()) {
                            const rj::Value& headingObj = basicVehicle["heading"];
                            heading = (headingObj.HasMember("headingValue") && headingObj["headingValue"].IsFloat())
                                          ? headingObj["headingValue"].GetFloat()
                                          : 0.0f;
                        }
                    }
                }
            }
        } else if (topic == "vanetza/in/vam") {
            if (doc.HasMember("vamParameters") && doc["vamParameters"].IsObject()) {
                const rj::Value& vamParameters = doc["vamParameters"];
                if (vamParameters.HasMember("basicContainer") && vamParameters["basicContainer"].IsObject()) {
                    const rj::Value& basicContainer = vamParameters["basicContainer"];
                    if (basicContainer.HasMember("referencePosition") && basicContainer["referencePosition"].IsObject()) {
                        const rj::Value& referencePosition = basicContainer["referencePosition"];
                        lat = (referencePosition.HasMember("latitude") && referencePosition["latitude"].IsNumber())
                                  ? referencePosition["latitude"].GetDouble()
                                  : lat_;
                        lon = (referencePosition.HasMember("longitude") && referencePosition["longitude"].IsNumber())
                                  ? referencePosition["longitude"].GetDouble()
                                  : lon_;
                    }
                }
            }
        } else {
            spdlog::warn("[Locator] Unknown topic: {}", topic);
            throw std::runtime_error("[Locator] Unknown topic");
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            lat_ = lat;
            lon_ = lon;
            heading_ = heading;
        }

        spdlog::debug("[Locator] Updated dynamic location: lat {:.6f}, lon {:.6f}, heading {:.2f}", lat, lon, heading);
    } catch (const std::exception& e) {
        spdlog::error("[Locator] Error parsing location message: {}", e.what());
    }
}

}  // namespace ldm::locator
