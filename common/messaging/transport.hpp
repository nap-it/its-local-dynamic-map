#pragma once

#include <functional>
#include <memory>
#include <string>

namespace ldm::config {
struct MessagingConfig;
}

namespace ldm::messaging {

using MessageHandler = std::function<void(const std::string& topic, const std::string& payload)>;

class Transport {
public:
    virtual ~Transport() = default;

    virtual void publish(const std::string& topic, const std::string& payload) = 0;
    virtual int subscribe(const std::string& topic, MessageHandler handler) = 0;
    virtual void unsubscribe(int subscription_id) = 0;
    virtual std::string backend() const = 0;
};

std::unique_ptr<Transport> createTransport(const config::MessagingConfig& cfg);

}  // namespace ldm::messaging

