#include <optional>
#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <iostream>
#include <charconv>

struct ServerConfig {
    std::string host;
    int port;
    int timeout_ms;
};

class ConfigParser {
public:
    std::optional<ServerConfig> parse(std::string_view content) {
        ServerConfig cfg;

        cfg.host = extract_field(content, "host")
            .value_or("localhost");

        auto port_str = extract_field(content, "port");
        if (port_str) {
            auto p = parse_int(*port_str);
            if (!p || *p < 1 || *p > 65535) {
                return std::nullopt;  // 端口无效
            }
            cfg.port = *p;
        } else {
            cfg.port = 8080;
        }

        auto timeout_str = extract_field(content, "timeout_ms");
        if (timeout_str) {
            auto t = parse_int(*timeout_str);
            if (!t || *t < 0) {
                return std::nullopt;
            }
            cfg.timeout_ms = *t;
        } else {
            cfg.timeout_ms = 5000;
        }

        return cfg;
    }

private:
    static std::optional<std::string> extract_field(
        std::string_view content, std::string_view key) {
        std::string search = std::string(key) + "=";
        auto pos = content.find(search);
        if (pos == std::string_view::npos) return std::nullopt;

        auto start = pos + search.size();
        auto end = content.find('\n', start);
        if (end == std::string_view::npos) end = content.size();

        return std::string(content.substr(start, end - start));
    }

    static std::optional<int> parse_int(std::string_view sv) {
        int value = 0;
        auto [ptr, ec] = std::from_chars(
            sv.data(), sv.data() + sv.size(), value);
        if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
            return value;
        }
        return std::nullopt;
    }
};

int main() {
    std::string config_text = "host=192.168.1.1\nport=3000\ntimeout_ms=10000\n";

    ConfigParser parser;
    auto cfg = parser.parse(config_text);

    if (cfg) {
        std::cout << "Host: " << cfg->host
                  << ", Port: " << cfg->port
                  << ", Timeout: " << cfg->timeout_ms << "ms\n";
    } else {
        std::cout << "Failed to parse config\n";
    }
}
