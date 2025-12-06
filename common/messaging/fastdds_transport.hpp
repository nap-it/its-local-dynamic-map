#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "fastdds-cpp-wrapper/dds.hpp"

#include "transport.hpp"
#include "../config/config.hpp"

namespace ldm::messaging {

class FastDdsTransport : public Transport {
public:
    explicit FastDdsTransport(const config::MessagingConfig& cfg);

    void publish(const std::string& topic, const std::string& payload) override;
    int subscribe(const std::string& topic, MessageHandler handler) override;
    void unsubscribe(int subscription_id) override;
    std::string backend() const override { return "FastDDS"; }

private:
    void dispatch(const std::string& topic, const std::string& payload);

    config::MessagingConfig cfg_;
    std::unique_ptr<Dds> dds_;
    std::mutex mutex_;
    int next_id_{1};
    std::unordered_map<std::string, std::vector<std::pair<int, MessageHandler>>> handlers_;
    std::unordered_map<int, std::string> id_to_topic_;
    std::unordered_set<std::string> provisioned_publishers_;
};

}  // namespace ldm::messaging
