#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mqttwrapper.h"

#include "transport.hpp"

namespace ldm::messaging {

class MqttTransport : public Transport {
public:
    explicit MqttTransport(const MessagingConfig& cfg);

    void publish(const std::string& topic, const std::string& payload) override;
    int subscribe(const std::string& topic, MessageHandler handler) override;
    void unsubscribe(int subscription_id) override;
    std::string backend() const override { return "MQTT"; }

private:
    void ensureConnected();
    void dispatch(const std::string& topic, const std::string& payload);

    MessagingConfig cfg_;
    std::unique_ptr<MqttWrapper> client_;
    std::mutex mutex_;
    int next_id_{1};
    std::unordered_map<std::string, std::vector<std::pair<int, MessageHandler>>> handlers_;
    std::unordered_map<int, std::string> id_to_topic_;
    std::unordered_set<std::string> subscribed_topics_;
};

}  // namespace ldm::messaging
