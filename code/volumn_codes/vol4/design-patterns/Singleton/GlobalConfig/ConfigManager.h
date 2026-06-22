#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
class ConfigManager {
  public:
    static ConfigManager& instance();

    void read_from_file(const std::filesystem::path& path_to_config);
    std::optional<std::string> get_value(const std::string& key);

  private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager(const ConfigManager&&) = delete;
    ConfigManager& operator=(ConfigManager&&) = delete;
    std::unordered_map<std::string, std::string> maps;
    void parse_line(const std::string& line);
};
