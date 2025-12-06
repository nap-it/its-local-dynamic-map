#include "zenoh_transport.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

namespace ldm::messaging {

ZenohTransport::ZenohTransport(const config::MessagingConfig& cfg) : cfg_(cfg) {
    zenoh::ZResult* err = nullptr;
    zenoh::Config zconfig = zenoh::Config::from_file("./zenoh_config.json5", err);
    if (!cfg_.zenoh_endpoint.empty()) {
        zconfig.insert_json5("connect/endpoints", "[\"" + cfg_.zenoh_endpoint + "\"]", err);
    }
    if (err) {
        spdlog::error("[Messaging] Zenoh config error: {}", static_cast<const void*>(err));
    }
    session_ = std::make_unique<zenoh::Session>(std::move(zenoh::Session::open(std::move(zconfig))));
    spdlog::info("[Messaging] Using Zenoh transport endpoint={}", cfg_.zenoh_endpoint);
}

void ZenohTransport::publish(const std::string& topic, const std::string& payload) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!publishers_.count(topic)) {
            session_->declare_publisher(topic);
            publishers_.insert(topic);
        }
    }
    spdlog::debug("[Zenoh] publish endpoint={} topic={} size={}", cfg_.zenoh_endpoint, topic, payload.size());
    session_->put(topic, payload);
}

int ZenohTransport::subscribe(const std::string& topic, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int id = next_id_++;
    handlers_[topic].push_back({id, std::move(handler)});
    id_to_topic_[id] = topic;
    if (handlers_[topic].size() == 1) {
        spdlog::debug("[Zenoh] subscribe endpoint={} topic={}", cfg_.zenoh_endpoint, topic);
        session_->declare_background_subscriber(topic, [this](zenoh::Sample& sample) {
            auto key = sample.get_keyexpr().as_string_view();
            auto payload = sample.get_payload().as_string();
            this->dispatch(std::string(key), payload);
        }, zenoh::closures::none);
    }
    return id;
}

void ZenohTransport::unsubscribe(int subscription_id) {
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

void ZenohTransport::dispatch(const std::string& topic, const std::string& payload) {
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
