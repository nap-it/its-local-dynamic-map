#include "Config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace {

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t");
    auto end = s.find_last_not_of(" \t");
    if (start == std::string::npos || end == std::string::npos) {
        return "";
    }
    return s.substr(start, end - start + 1);
}

std::string toLower(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v;
}

}  // namespace

namespace ldm::config {

ConfigBundle loadConfig(const std::string& path) {
    ConfigBundle cfg;
    std::ifstream in(path);
    if (!in.is_open()) {
        return cfg;
    }
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            section = toLower(trim(section));
            continue;
        }
        auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = toLower(trim(line.substr(0, pos)));
        std::string value = trim(line.substr(pos + 1));

        if (section == "general") {
            if (key == "debug") cfg.general.debug = (value == "true" || value == "1" || value == "yes");
            if (key == "prometheus") cfg.general.prometheus = (value == "true" || value == "1" || value == "yes");
            if (key == "prometheus_port") cfg.general.prometheus_port = std::stoi(value);
        } else if (section == "locator") {
            if (key == "station_type") cfg.locator.station_type = std::stoi(value);
            if (key == "station_id") cfg.locator.station_id = std::stoi(value);
            if (key == "station_latitude") cfg.locator.station_latitude = std::stod(value);
            if (key == "station_longitude") cfg.locator.station_longitude = std::stod(value);
            if (key == "location_provider") cfg.locator.location_provider = value;
            if (key == "mqtt_host") cfg.locator.mqtt_host = value;
            if (key == "mqtt_port") cfg.locator.mqtt_port = value.empty() ? 0 : std::stoi(value);
            if (key == "mqtt_topic") cfg.locator.mqtt_topic = value;
            if (key == "domain_id") cfg.locator.domain_id = value.empty() ? 0 : std::stoi(value);
            if (key == "dds_topic") cfg.locator.dds_topic = value;
        } else if (section == "ldm-core") {
            if (key == "aom_radius_m") cfg.ldm_core.aom_radius_m = std::stod(value);
            if (key == "static_map_validity_seconds") cfg.ldm_core.static_map_validity_seconds = std::stoi(value);
        } else if (section == "ingestion") {
            if (key == "domain_id") cfg.ingestion.domain_id = std::stoi(value);
            if (key == "station_type") cfg.ingestion.station_type = std::stoi(value);
            if (key == "station_id") cfg.ingestion.station_id = std::stoi(value);
            if (key == "performance_logs") cfg.ingestion.performance_logs = (value == "true" || value == "1" || value == "yes");
            if (key == "dds_input_topic_mapem") cfg.ingestion.dds_input_topic_mapem = value;
            if (key == "dds_output_topic_ldm") cfg.ingestion.dds_output_topic_ldm = value;
            if (key == "local_mqtt_enable_publisher") cfg.ingestion.local_mqtt_enable_publisher = (value == "true" || value == "1" || value == "yes");
        } else if (section == "query") {
            if (key == "domain_id") cfg.query.domain_id = std::stoi(value);
            if (key == "station_type") cfg.query.station_type = std::stoi(value);
            if (key == "station_id") cfg.query.station_id = std::stoi(value);
            if (key == "performance_logs") cfg.query.performance_logs = (value == "true" || value == "1" || value == "yes");
            if (key == "dds_query_topic") cfg.query.dds_query_topic = value;
            if (key == "dds_response_topic") cfg.query.dds_response_topic = value;
            if (key == "local_mqtt_enable") cfg.query.local_mqtt_enable = (value == "true" || value == "1" || value == "yes");
            if (key == "local_mqtt_host") cfg.query.local_mqtt_host = value;
            if (key == "local_mqtt_port") cfg.query.local_mqtt_port = value.empty() ? 1883 : std::stoi(value);
            if (key == "local_mqtt_query_topic") cfg.query.local_mqtt_query_topic = value;
            if (key == "local_mqtt_response_topic") cfg.query.local_mqtt_response_topic = value;
        }
    }
    return cfg;
}

}  // namespace ldm::config
