#include "ConfigManager.h"
#include <fstream>
#include <optional>
#include <print>
#include <string>

ConfigManager& ConfigManager::instance() {
    static ConfigManager configManager;
    return configManager;
}

void ConfigManager::read_from_file(const std::filesystem::path& path_to_config) {

    std::ifstream file(path_to_config);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + path_to_config.string());
    }
    std::string line_buffer;
    while (std::getline(file, line_buffer)) {
        parse_line(line_buffer);
    }
}

void ConfigManager::parse_line(const std::string& line) {
    if (line.empty() || line[0] == '#')
        return;

    ssize_t splity_symbol_pos = line.find_first_of('=');

    if (splity_symbol_pos == std::string::npos)
        return; // invalid kv

    std::string key = line.substr(0, splity_symbol_pos);
    std::string value = line.substr(splity_symbol_pos + 1);
    key.erase(key.find_last_not_of(" \t") + 1);
    value.erase(0, value.find_first_not_of(" \t"));

    maps.insert({key, value});
}

std::optional<std::string> ConfigManager::get_value(const std::string& key) {
    auto result = maps.find(key);
    if (result != maps.end()) {
        return {result->second};
    } else {
        return std::nullopt;
    }
}
