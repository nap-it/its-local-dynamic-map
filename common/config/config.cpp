#include "config.hpp"

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
        } else if (section == "messaging") {
            if (key == "backend") cfg.messaging.backend = value;
            if (key == "domain_id") cfg.messaging.domain_id = value.empty() ? 0 : std::stoi(value);
            if (key == "mqtt_host") cfg.messaging.mqtt_host = value;
            if (key == "mqtt_port") cfg.messaging.mqtt_port = value.empty() ? 1883 : std::stoi(value);
            if (key == "mqtt_username") cfg.messaging.mqtt_username = value;
            if (key == "mqtt_password") cfg.messaging.mqtt_password = value;
            if (key == "zenoh_endpoint") cfg.messaging.zenoh_endpoint = value;
        } else if (section == "ldm-core") {
            if (key == "aom_radius_m") cfg.ldm_core.aom_radius_m = std::stod(value);
            if (key == "static_map_validity_seconds") cfg.ldm_core.static_map_validity_seconds = std::stoi(value);
            if (key == "ingest_topic") cfg.ldm_core.ingest_topic = value;
            if (key == "query_topic") cfg.ldm_core.query_topic = value;
            if (key == "response_topic") cfg.ldm_core.response_topic = value;
        } else if (section == "ingestion") {
            if (key == "station_type") cfg.ingestion.station_type = std::stoi(value);
            if (key == "station_id") cfg.ingestion.station_id = std::stoi(value);
            if (key == "performance_logs") cfg.ingestion.performance_logs = (value == "true" || value == "1" || value == "yes");
            if (key == "input_topic") cfg.ingestion.input_topic = value;
            if (key == "output_topic") cfg.ingestion.output_topic = value;
            if (key == "simulate_input") cfg.ingestion.simulate_input = (value == "true" || value == "1" || value == "yes");
            if (key == "simulation_interval_ms") cfg.ingestion.simulation_interval_ms = value.empty() ? 2000 : std::stoi(value);
            if (key == "mapem_file") cfg.ingestion.mapem_file = value;
        } else if (section == "query") {
            if (key == "station_type") cfg.query.station_type = std::stoi(value);
            if (key == "station_id") cfg.query.station_id = std::stoi(value);
            if (key == "performance_logs") cfg.query.performance_logs = (value == "true" || value == "1" || value == "yes");
            if (key == "request_topic") cfg.query.request_topic = value;
            if (key == "response_topic") cfg.query.response_topic = value;
            if (key == "auto_request") cfg.query.auto_request = (value == "true" || value == "1" || value == "yes");
            if (key == "request_interval_ms") cfg.query.request_interval_ms = value.empty() ? 4000 : std::stoi(value);
        }
    }
    return cfg;
}

}  // namespace ldm::config
