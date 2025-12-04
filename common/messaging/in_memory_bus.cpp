#include "in_memory_bus.hpp"

#include <vector>

namespace ldm::messaging {

InMemoryBus& InMemoryBus::instance() {
    static InMemoryBus bus;
    return bus;
}

int InMemoryBus::subscribe(const std::string& backend, const std::string& topic, MessageHandler handler) {
    (void)backend;
    std::lock_guard<std::mutex> lock(mutex_);
    const int id = next_id_++;
    subscribers_[topic][id] = std::move(handler);
    handles_[id] = topic;
    return id;
}

void InMemoryBus::unsubscribe(int subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto handle_it = handles_.find(subscription_id);
    if (handle_it == handles_.end()) {
        return;
    }
    auto topic_it = subscribers_.find(handle_it->second);
    if (topic_it != subscribers_.end()) {
        topic_it->second.erase(subscription_id);
        if (topic_it->second.empty()) {
            subscribers_.erase(topic_it);
        }
    }
    handles_.erase(handle_it);
}

void InMemoryBus::publish(const std::string& backend, const std::string& topic, const std::string& payload) {
    (void)backend;
    std::vector<MessageHandler> handlers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = subscribers_.find(topic);
        if (it != subscribers_.end()) {
            handlers.reserve(it->second.size());
            for (const auto& [_, handler] : it->second) {
                handlers.push_back(handler);
            }
        }
    }
    for (auto& handler : handlers) {
        handler(topic, payload);
    }
}

}  // namespace ldm::messaging
