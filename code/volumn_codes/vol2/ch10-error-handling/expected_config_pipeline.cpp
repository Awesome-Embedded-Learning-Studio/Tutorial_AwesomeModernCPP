#include <expected>
#include <string>
#include <iostream>
#include <charconv>
#include <system_error>

enum class ConfigError {
    kFileNotFound,
    kParseError,
    kValidationError,
};

struct ServerConfig {
    std::string host;
    int port;
};

std::expected<std::string, ConfigError> read_file(
    const std::string& path) {
    // 简化：假设总是成功
    return "host=192.168.1.1\nport=8080\n";
}

std::expected<ServerConfig, ConfigError> parse_config(
    const std::string& content) {
    ServerConfig cfg;
    cfg.host = "localhost";
    cfg.port = 8080;
    // 简化：实际解析内容
    return cfg;
}

std::expected<ServerConfig, ConfigError> validate_config(
    ServerConfig cfg) {
    if (cfg.port < 1 || cfg.port > 65535) {
        return std::unexpected(ConfigError::kValidationError);
    }
    return cfg;
}

int main() {
    auto result = read_file("server.cfg")
        .and_then(parse_config)
        .and_then(validate_config)
        .transform([](const ServerConfig& cfg) -> std::string {
            return cfg.host + ":" + std::to_string(cfg.port);
        })
        .transform_error([](ConfigError e) -> std::string {
            switch (e) {
                case ConfigError::kFileNotFound:
                    return "Config file not found";
                case ConfigError::kParseError:
                    return "Config parse error";
                case ConfigError::kValidationError:
                    return "Config validation failed";
            }
            return "Unknown error";
        });

    if (result) {
        std::cout << "Server: " << *result << "\n";
    } else {
        std::cerr << "Failed: " << result.error() << "\n";
    }
}
