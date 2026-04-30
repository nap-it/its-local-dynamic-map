#include "mqtt_transport.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#include <spdlog/spdlog.h>

namespace ldm::messaging {

MqttTransport::MqttTransport(const MessagingConfig& cfg) : cfg_(cfg) {
    data_mqtt_server mqtt_cfg;
    mqtt_cfg.address = "tcp://" + cfg_.mqtt_host + ":" + std::to_string(cfg_.mqtt_port);
    mqtt_cfg.client_id = "LDM-MQTT-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    mqtt_cfg.user_name = cfg_.mqtt_username;
    mqtt_cfg.password = cfg_.mqtt_password;

    client_ = std::make_unique<MqttWrapper>(mqtt_cfg, [this](const std::string& topic, const std::string& message) {
        this->dispatch(topic, message);
    });
    ensureConnected();
    spdlog::info("[Messaging] Using MQTT transport {}:{}", cfg_.mqtt_host, cfg_.mqtt_port);
}

void MqttTransport::ensureConnected() {
    while (client_ && !client_->is_connected()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        spdlog::debug("[MQTT] Waiting for broker {}:{}", cfg_.mqtt_host, cfg_.mqtt_port);
    }
}

void MqttTransport::publish(const std::string& topic, const std::string& payload) {
    ensureConnected();
    spdlog::debug("[MQTT] publish {}:{} topic={} size={}", cfg_.mqtt_host, cfg_.mqtt_port, topic, payload.size());
    client_->publish(topic, payload);
}

int MqttTransport::subscribe(const std::string& topic, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensureConnected();
    const int id = next_id_++;
    handlers_[topic].push_back({id, std::move(handler)});
    id_to_topic_[id] = topic;
    if (!subscribed_topics_.count(topic)) {
        spdlog::debug("[MQTT] subscribe {}:{} topic={}", cfg_.mqtt_host, cfg_.mqtt_port, topic);
        client_->client_.subscribe(topic, 0);
        subscribed_topics_.insert(topic);
    }
    return id;
}

void MqttTransport::unsubscribe(int subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto topic_it = id_to_topic_.find(subscription_id);
    if (topic_it == id_to_topic_.end()) {
        return;
    }
    const std::string topic = topic_it->second;
    auto& vec = handlers_[topic];
    vec.erase(std::remove_if(vec.begin(), vec.end(), [subscription_id](const auto& pair) { return pair.first == subscription_id; }), vec.end());
    if (vec.empty()) {
        handlers_.erase(topic);
    }
    id_to_topic_.erase(topic_it);
}

void MqttTransport::dispatch(const std::string& topic, const std::string& payload) {
    std::vector<MessageHandler> callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handlers_.find(topic);
        if (it != handlers_.end()) {
            callbacks.reserve(it->second.size());
            for (const auto& pair : it->second) {
                callbacks.push_back(pair.second);
            }
        }
    }
    for (auto& cb : callbacks) {
        cb(topic, payload);
    }
}

}  // namespace ldm::messaging
