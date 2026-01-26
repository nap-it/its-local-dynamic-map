#pragma once

#include <functional>
#include <memory>
#include <string>

namespace ldm::messaging {

struct MessagingConfig {
    std::string backend{"fastdds"};
    int domain_id{0};
    std::string mqtt_host{"127.0.0.1"};
    int mqtt_port{1883};
    std::string mqtt_username;
    std::string mqtt_password;
    std::string zenoh_endpoint{"tcp/127.0.0.1:7447"};
};

using MessageHandler = std::function<void(const std::string& topic, const std::string& payload)>;

class Transport {
public:
    virtual ~Transport() = default;

    virtual void publish(const std::string& topic, const std::string& payload) = 0;
    virtual int subscribe(const std::string& topic, MessageHandler handler) = 0;
    virtual void unsubscribe(int subscription_id) = 0;
    virtual std::string backend() const = 0;
};

std::unique_ptr<Transport> createTransport(const MessagingConfig& cfg);

}  // namespace ldm::messaging
