#pragma once

#include "in_memory_bus.hpp"
#include "transport.hpp"

#include "../config/config.hpp"

namespace ldm::messaging {

class ZenohTransport : public Transport {
public:
    explicit ZenohTransport(const config::MessagingConfig& cfg);

    void publish(const std::string& topic, const std::string& payload) override;
    int subscribe(const std::string& topic, MessageHandler handler) override;
    void unsubscribe(int subscription_id) override;
    std::string backend() const override { return "Zenoh"; }

private:
    config::MessagingConfig cfg_;
    InMemoryBus& bus_;
};

}  // namespace ldm::messaging
