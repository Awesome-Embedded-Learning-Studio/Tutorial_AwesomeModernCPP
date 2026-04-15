// Configuration Loading with Functional Error Handling
// Demonstrates a complete config loading pipeline using Result types

#include <iostream>
#include <expected>
#include <string>
#include <sstream>
#include <fstream>

// Error types
struct ConfigError {
    enum Code { FileNotFound, ParseError, ValidationError, ApplyError };
    Code code;
    std::string message;

    static ConfigError file_not_found(const std::string& path) {
        return {FileNotFound, "File not found: " + path};
    }

    static ConfigError parse_error(const std::string& detail) {
        return {ParseError, "Parse error: " + detail};
    }

    static ConfigError validation_error(const std::string& field) {
        return {ValidationError, "Validation failed for: " + field};
    }
};

template<typename T>
using Result = std::expected<T, ConfigError>;

// Configuration structure
struct Config {
    int baudrate = 115200;
    int timeout = 1000;
    std::string port = "/dev/ttyUSB0";

    void print() const {
        std::cout << "Config:" << std::endl;
        std::cout << "  baudrate: " << baudrate << std::endl;
        std::cout << "  timeout: " << timeout << std::endl;
        std::cout << "  port: " << port << std::endl;
    }
};

// Step 1: Read file
Result<std::string> read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        return ConfigError::file_not_found(path);
    }

    std::stringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
}

// Step 2: Parse JSON (simplified)
struct JsonValue {
    std::string raw;
};

Result<JsonValue> parse_json(const std::string& content) {
    if (content.empty() || content[0] != '{') {
        return ConfigError::parse_error("not a JSON object");
    }
    return JsonValue{content};
}

// Step 3: Validate and extract config
Result<Config> validate_config(const JsonValue& json) {
    Config cfg;

    // Simplified parsing
    if (json.raw.find("baudrate") == std::string::npos) {
        return ConfigError::validation_error("baudrate");
    }
    if (json.raw.find("timeout") == std::string::npos) {
        return ConfigError::validation_error("timeout");
    }

    // Extract values (simplified)
    cfg.baudrate = 115200;  // Would parse from JSON
    cfg.timeout = 1000;

    return cfg;
}

// Step 4: Apply config
Result<void> apply_config(const Config& cfg) {
    std::cout << "Applying configuration..." << std::endl;
    cfg.print();
    return {};  // Success
}

// TRY macro for clean error propagation
#define TRY(...) ({ \
    auto _result = (__VA_ARGS__); \
    if (!_result) return std::unexpected(_result.error()); \
    _result.value(); \
})

// Load pipeline using manual propagation
Result<void> load_config_manual(const std::string& path) {
    auto content_result = read_file(path);
    if (!content_result) return content_result.error();

    auto json_result = parse_json(content_result.value());
    if (!json_result) return json_result.error();

    auto config_result = validate_config(json_result.value());
    if (!config_result) return config_result.error();

    return apply_config(config_result.value());
}

// Load pipeline using TRY macro
Result<void> load_config_clean(const std::string& path) {
    auto content = TRY(read_file(path));
    auto json = TRY(parse_json(content));
    auto config = TRY(validate_config(json));
    return apply_config(config);
}

// Nested and_then approach
Result<void> load_config_functional(const std::string& path) {
    return read_file(path)
        .and_then([](const std::string& content) {
            return parse_json(content);
        })
        .and_then([](const JsonValue& json) {
            return validate_config(json);
        })
        .and_then([](const Config& cfg) {
            return apply_config(cfg);
        });
}

int main() {
    std::cout << "=== Configuration Loader Demo ===" << std::endl;

    // Test with a mock file content
    std::cout << "\n--- Manual propagation ---" << std::endl;
    auto r1 = load_config_manual("config.json");
    if (!r1) {
        std::cout << "Failed: " << r1.error().message << std::endl;
    }

    std::cout << "\n--- TRY macro ---" << std::endl;
    auto r2 = load_config_clean("config.json");
    if (!r2) {
        std::cout << "Failed: " << r2.error().message << std::endl;
    }

    std::cout << "\n--- Functional (and_then) ---" << std::endl;
    auto r3 = load_config_functional("config.json");
    if (!r3) {
        std::cout << "Failed: " << r3.error().message << std::endl;
    }

    std::cout << "\n=== Key Points ===" << std::endl;
    std::cout << "- Errors are values, not exceptions" << std::endl;
    std::cout << "- Each step returns Result<T, Error>" << std::endl;
    std::cout << "- Error propagation is explicit and type-safe" << std::endl;
    std::cout << "- TRY macro provides clean syntax like Rust's ?" << std::endl;
    std::cout << "- and_then chains operations naturally" << std::endl;

    return 0;
}
