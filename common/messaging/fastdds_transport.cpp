#include "fastdds_transport.hpp"

#include <spdlog/spdlog.h>

#include "../config/config.hpp"

namespace ldm::messaging {

FastDdsTransport::FastDdsTransport(const config::MessagingConfig& cfg) : cfg_(cfg), bus_(InMemoryBus::instance()) {
    spdlog::info("[Messaging] Using FastDDS transport domain_id={} (simulated)", cfg_.domain_id);
}

void FastDdsTransport::publish(const std::string& topic, const std::string& payload) {
    spdlog::debug("[FastDDS] publish domain={} topic={} size={}", cfg_.domain_id, topic, payload.size());
    bus_.publish("fastdds", topic, payload);
}

int FastDdsTransport::subscribe(const std::string& topic, MessageHandler handler) {
    spdlog::debug("[FastDDS] subscribe domain={} topic={}", cfg_.domain_id, topic);
    return bus_.subscribe("fastdds", topic, std::move(handler));
}

void FastDdsTransport::unsubscribe(int subscription_id) { bus_.unsubscribe(subscription_id); }

}  // namespace ldm::messaging
