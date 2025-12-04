#include "mqtt_transport.hpp"

#include <spdlog/spdlog.h>

namespace ldm::messaging {

MqttTransport::MqttTransport(const config::MessagingConfig& cfg) : cfg_(cfg), bus_(InMemoryBus::instance()) {
    spdlog::info("[Messaging] Using MQTT transport {}:{} (simulated)", cfg_.mqtt_host, cfg_.mqtt_port);
}

void MqttTransport::publish(const std::string& topic, const std::string& payload) {
    spdlog::debug("[MQTT] publish {}:{} topic={} size={}", cfg_.mqtt_host, cfg_.mqtt_port, topic, payload.size());
    bus_.publish("mqtt", topic, payload);
}

int MqttTransport::subscribe(const std::string& topic, MessageHandler handler) {
    spdlog::debug("[MQTT] subscribe {}:{} topic={}", cfg_.mqtt_host, cfg_.mqtt_port, topic);
    return bus_.subscribe("mqtt", topic, std::move(handler));
}

void MqttTransport::unsubscribe(int subscription_id) { bus_.unsubscribe(subscription_id); }

}  // namespace ldm::messaging
