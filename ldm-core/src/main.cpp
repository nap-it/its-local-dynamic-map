#include <atomic>
#include <chrono>
#include <sstream>
#include <thread>

#include <spdlog/spdlog.h>

#include "../../common/config/config.hpp"
#include "../../common/ldm-model/ldm_store.hpp"
#include "../../common/locator/locator.hpp"
#include "../../common/messaging/transport.hpp"
#include "../../common/metrics/metrics.hpp"

int main() {
    using namespace std::chrono_literals;
    auto cfg = ldm::config::loadConfig("/config.ini");

    spdlog::set_level(cfg.general.debug ? spdlog::level::debug : spdlog::level::info);
    spdlog::info("[LDM-Core] Starting LDM core service");

    ldm::locator::Locator locator(cfg.locator);
    auto [lat, lon] = locator.currentPosition();

    ldm::model::LdmStore store(cfg.ldm_core.aom_radius_m);
    store.setStationReference(lat, lon);

    std::unique_ptr<ldm::metrics::Metrics> metrics;
    ldm::metrics::Gauge* objects_gauge = nullptr;
    ldm::metrics::Counter* served_queries = nullptr;
    if (cfg.general.prometheus) {
        metrics = std::make_unique<ldm::metrics::Metrics>(cfg.general.prometheus_port);
        objects_gauge = &metrics->gauge("ldm_objects_total", "Current number of objects in the LDM");
        served_queries = &metrics->counter("ldm_core_queries_total", "Total queries served by LDM core");
    }

    auto transport = ldm::messaging::createTransport(cfg.messaging);
    auto* transport_ptr = transport.get();
    std::atomic<size_t> object_counter{0};
    const auto ingest_topic = cfg.ldm_core.ingest_topic.empty() ? std::string("ldm/updates") : cfg.ldm_core.ingest_topic;
    const auto query_topic = cfg.ldm_core.query_topic.empty() ? std::string("ldm/query") : cfg.ldm_core.query_topic;
    const auto response_topic = cfg.ldm_core.response_topic.empty() ? std::string("ldm/response") : cfg.ldm_core.response_topic;

    transport->subscribe(ingest_topic, [&, objects_gauge](const std::string& topic, const std::string& payload) {
        auto count = object_counter.fetch_add(1) + 1;
        if (objects_gauge) {
            objects_gauge->Set(static_cast<double>(count));
        }
        spdlog::debug("[LDM-Core] Received update on {} payload={} (total={})", topic, payload, count);
    });

    transport->subscribe(query_topic, [&, transport_ptr, response_topic, served_queries](const std::string& topic,
                                                                                       const std::string& payload) {
        if (served_queries) {
            served_queries->Increment();
        }
        std::ostringstream response;
        response << "objects=" << object_counter.load() << ";request=" << payload;
        spdlog::debug("[LDM-Core] Responding to query payload={} via {}", payload, topic);
        transport_ptr->publish(response_topic, response.str());
    });

    while (true) {
        store.runMaintenance();
        std::this_thread::sleep_for(5s);
    }
    return 0;
}
