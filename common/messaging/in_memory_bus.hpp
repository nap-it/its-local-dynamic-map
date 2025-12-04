#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ldm::messaging {

using MessageHandler = std::function<void(const std::string& topic, const std::string& payload)>;

class InMemoryBus {
public:
    static InMemoryBus& instance();

    int subscribe(const std::string& backend, const std::string& topic, MessageHandler handler);
    void unsubscribe(int subscription_id);
    void publish(const std::string& backend, const std::string& topic, const std::string& payload);

private:
    InMemoryBus() = default;

    std::mutex mutex_;
    int next_id_{1};
    std::unordered_map<std::string, std::unordered_map<int, MessageHandler>> subscribers_;
    std::unordered_map<int, std::string> handles_;
};

}  // namespace ldm::messaging

