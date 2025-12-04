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

struct LdmCoreConfig {
    double aom_radius_m{1000.0};
    int static_map_validity_seconds{3600};
};

struct IngestionConfig {
    int domain_id{0};
    int station_type{15};
    int station_id{0};
    bool performance_logs{false};
    std::string dds_input_topic_mapem{"vanetza/in/mapem"};
    std::string dds_output_topic_ldm{"ldm/in/mapem"};
    bool local_mqtt_enable_publisher{false};
};

struct QueryConfig {
    int domain_id{0};
    int station_type{15};
    int station_id{0};
    bool performance_logs{false};
    std::string dds_query_topic{"ldm/query"};
    std::string dds_response_topic{"ldm/response"};
    bool local_mqtt_enable{true};
    std::string local_mqtt_host{"127.0.0.1"};
    int local_mqtt_port{1883};
    std::string local_mqtt_query_topic{"ldm/query"};
    std::string local_mqtt_response_topic{"ldm/response"};
};

struct ConfigBundle {
    GeneralConfig general;
    LocatorConfig locator;
    LdmCoreConfig ldm_core;
    IngestionConfig ingestion;
    QueryConfig query;
};

ConfigBundle loadConfig(const std::string& path);

}  // namespace ldm::config
