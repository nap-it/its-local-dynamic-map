#pragma once

#include <string>
#include <unordered_map>

namespace ldm::config {

struct GeneralConfig {
    bool debug{true};
    bool prometheus{true};
    int prometheus_port{9200};
};

struct LocatorConfig {
    int station_type{15};
    int station_id{0};
    double station_latitude{0.0};
    double station_longitude{0.0};
    std::string location_provider{"static"};
    std::string mqtt_host;
    int mqtt_port{0};
    std::string mqtt_topic;
    int domain_id{0};
    std::string dds_topic;
};

struct MessagingConfig {
    std::string backend{"fastdds"};
    int domain_id{0};
    std::string mqtt_host{"127.0.0.1"};
    int mqtt_port{1883};
    std::string zenoh_endpoint{"tcp/127.0.0.1:7447"};
};

struct LdmCoreConfig {
    double aom_radius_m{1000.0};
    int static_map_validity_seconds{3600};
    std::string ingest_topic{"ldm/updates"};
    std::string query_topic{"ldm/query"};
    std::string response_topic{"ldm/response"};
};

struct IngestionConfig {
    int station_type{15};
    int station_id{0};
    bool performance_logs{false};
    std::string input_topic{"generation/mapem"};
    std::string output_topic{"ldm/updates"};
    bool simulate_input{true};
    int simulation_interval_ms{2000};
};

struct QueryConfig {
    int station_type{15};
    int station_id{0};
    bool performance_logs{false};
    std::string request_topic{"ldm/query"};
    std::string response_topic{"ldm/response"};
    bool auto_request{true};
    int request_interval_ms{4000};
};

struct ConfigBundle {
    GeneralConfig general;
    LocatorConfig locator;
    MessagingConfig messaging;
    LdmCoreConfig ldm_core;
    IngestionConfig ingestion;
    QueryConfig query;
};

ConfigBundle loadConfig(const std::string& path);

}  // namespace ldm::config
