#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

#include <spdlog/spdlog.h>

#include "../../common/config/config.hpp"
#include "../../common/locator/locator.hpp"
#include "../../common/messaging/transport.hpp"
#include "../../common/metrics/metrics.hpp"
#include "../../common/ldm-model/ldm_store.hpp"

int main() {
    using namespace std::chrono_literals;
    auto cfg = ldm::config::loadConfig("/config.ini");
    spdlog::set_level(cfg.general.debug ? spdlog::level::debug : spdlog::level::info);
    spdlog::info("[LDM-Query] Starting query service");

    ldm::locator::Locator locator(cfg.locator);
    locator.run();
    auto [lat, lon] = locator.currentPosition();
    ldm::model::LdmStore store(cfg.ldm_core.aom_radius_m);
    store.setStationReference(lat, lon);

    std::unique_ptr<ldm::metrics::Metrics> metrics;
    ldm::metrics::Counter* issued_queries = nullptr;
    if (cfg.general.prometheus) {
        metrics = std::make_unique<ldm::metrics::Metrics>(cfg.general.prometheus_port);
        issued_queries = &metrics->counter("ldm_query_requests_total", "Total queries published");
    }

    auto transport = ldm::messaging::createTransport(cfg.messaging);
    const auto request_topic = cfg.query.request_topic.empty() ? std::string("ldm/query") : cfg.query.request_topic;
    const auto response_topic = cfg.query.response_topic.empty() ? std::string("ldm/response") : cfg.query.response_topic;

    transport->subscribe(response_topic, [](const std::string& topic, const std::string& payload) {
        spdlog::info("[LDM-Query] Response on {} -> {}", topic, payload);
    });

    const auto interval = std::chrono::milliseconds(std::max(250, cfg.query.request_interval_ms));
    int sequence = 0;
    while (true) {
        if (cfg.query.auto_request) {
            std::ostringstream query_payload;
            query_payload << "request_id=" << sequence << ";lat=" << lat << ";lon=" << lon;
            spdlog::debug("[LDM-Query] Publishing request {} on {}", sequence, request_topic);
            transport->publish(request_topic, query_payload.str());
            if (issued_queries) {
                issued_queries->Increment();
            }
            ++sequence;
        }
        std::this_thread::sleep_for(interval);
    }
    return 0;
}
