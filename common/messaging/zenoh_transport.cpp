#include "zenoh_transport.hpp"

#include <spdlog/spdlog.h>

namespace ldm::messaging {

ZenohTransport::ZenohTransport(const config::MessagingConfig& cfg) : cfg_(cfg), bus_(InMemoryBus::instance()) {
    spdlog::info("[Messaging] Using Zenoh transport endpoint={} (simulated)", cfg_.zenoh_endpoint);
}

void ZenohTransport::publish(const std::string& topic, const std::string& payload) {
    spdlog::debug("[Zenoh] publish endpoint={} topic={} size={}", cfg_.zenoh_endpoint, topic, payload.size());
    bus_.publish("zenoh", topic, payload);
}

int ZenohTransport::subscribe(const std::string& topic, MessageHandler handler) {
    spdlog::debug("[Zenoh] subscribe endpoint={} topic={}", cfg_.zenoh_endpoint, topic);
    return bus_.subscribe("zenoh", topic, std::move(handler));
}

void ZenohTransport::unsubscribe(int subscription_id) { bus_.unsubscribe(subscription_id); }

}  // namespace ldm::messaging
