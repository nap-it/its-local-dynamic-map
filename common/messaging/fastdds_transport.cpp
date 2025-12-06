#include "fastdds_transport.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

namespace ldm::messaging {

FastDdsTransport::FastDdsTransport(const config::MessagingConfig& cfg) : cfg_(cfg) {
    dds_ = std::make_unique<Dds>("LDM-Transport", cfg_.domain_id, [this](const std::string& topic, const std::string& message) {
        this->dispatch(topic, message);
    });
    spdlog::info("[Messaging] Using FastDDS transport domain_id={}", cfg_.domain_id);
}

void FastDdsTransport::publish(const std::string& topic, const std::string& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!provisioned_publishers_.count(topic)) {
        dds_->provision_publisher(topic);
        provisioned_publishers_.insert(topic);
    }
    spdlog::debug("[FastDDS] publish domain={} topic={} size={}", cfg_.domain_id, topic, payload.size());
    dds_->publish(topic, payload);
}

int FastDdsTransport::subscribe(const std::string& topic, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int id = next_id_++;
    handlers_[topic].push_back({id, std::move(handler)});
    id_to_topic_[id] = topic;
    if (handlers_[topic].size() == 1) {
        dds_->subscribe(topic);
        spdlog::debug("[FastDDS] subscribe domain={} topic={}", cfg_.domain_id, topic);
    }
    return id;
}

void FastDdsTransport::unsubscribe(int subscription_id) {
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

void FastDdsTransport::dispatch(const std::string& topic, const std::string& payload) {
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
