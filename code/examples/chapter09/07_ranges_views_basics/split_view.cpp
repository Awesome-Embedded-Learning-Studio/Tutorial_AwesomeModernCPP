// Split View - Dividing Ranges by Delimiter
// Demonstrates std::views::split for parsing

#include <iostream>
#include <ranges>
#include <string>
#include <vector>
#include <sstream>

void demo_basic_split() {
    std::cout << "=== Basic Split ===" << std::endl;

    std::string data = "sensor1=25,sensor2=30,sensor3=28";

    // Split by comma
    auto fields = std::views::split(data, ',');

    std::cout << "Fields:" << std::endl;
    for (auto field : fields) {
        std::string_view sv(field.begin(), field.end());
        std::cout << "  " << sv << std::endl;
    }
}

void demo_csv_parsing() {
    std::cout << "\n=== CSV Parsing ===" << std::endl;

    std::string csv = "1,2,3,4,5";

    auto numbers = std::views::split(csv, ',');

    std::cout << "Numbers: ";
    for (auto num : numbers) {
        std::string_view sv(num.begin(), num.end());
        std::cout << sv << " ";
    }
    std::cout << std::endl;
}

void demo_key_value_parsing() {
    std::cout << "\n=== Key-Value Parsing ===" << std::endl;

    std::string config = "baudrate=115200&timeout=1000&parity=none";

    // Split by &
    auto pairs = std::views::split(config, '&');

    for (auto pair : pairs) {
        std::string_view sv(pair.begin(), pair.end());

        // Split each pair by =
        auto kv = std::views::split(sv, '=');
        auto it = kv.begin();

        if (it != kv.end()) {
            std::string_view key((*it).begin(), (*it).end());
            ++it;
            if (it != kv.end()) {
                std::string_view value((*it).begin(), (*it).end());
                std::cout << "  " << key << " = " << value << std::endl;
            }
        }
    }
}

void demo_nmea_parsing() {
    std::cout << "\n=== NMEA Sentence Parsing ===" << std::endl;

    // NMEA format: $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
    std::string nmea = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";

    // Remove checksum for demo
    auto clean_nmea = nmea.substr(0, nmea.find('*'));

    auto parts = std::views::split(clean_nmea, ',');

    const char* field_names[] = {
        "Type", "Time", "Lat", "NS", "Lon", "EW", "Quality", "Sats",
        "HDOP", "Alt", "Units", "Geoid", "Units2", "Empty", "Empty"
    };

    int idx = 0;
    for (auto part : parts) {
        std::string_view sv(part.begin(), part.end());
        if (idx < static_cast<int>(sizeof(field_names)/sizeof(field_names[0]))) {
            std::cout << "  " << field_names[idx] << ": " << sv << std::endl;
        }
        idx++;
    }
}

void demo_split_lines() {
    std::cout << "\n=== Split Lines ===" << std::endl;

    std::string text = "Line 1\nLine 2\nLine 3\nLine 4";

    auto lines = std::views::split(text, '\n');

    std::cout << "Lines:" << std::endl;
    for (auto line : lines) {
        std::string_view sv(line.begin(), line.end());
        std::cout << "  " << sv << std::endl;
    }
}

void demo_parse_hex_bytes() {
    std::cout << "\n=== Parse Hex Bytes ===" << std::endl;

    std::string hex_string = "AA:BB:CC:DD:EE:FF";

    auto bytes = std::views::split(hex_string, ':');

    std::cout << "Hex bytes: ";
    for (auto byte : bytes) {
        std::string_view sv(byte.begin(), byte.end());
        std::cout << "0x" << sv << " ";
    }
    std::cout << std::endl;
}

void demo_nested_split() {
    std::cout << "\n=== Nested Split ===" << std::endl;

    std::string data = "1,2;3,4;5,6";

    // First split by semicolon, then each part by comma
    auto rows = std::views::split(data, ';');

    std::cout << "Matrix:" << std::endl;
    for (auto row : rows) {
        std::string_view row_sv(row.begin(), row.end());
        auto cols = std::views::split(row_sv, ',');

        std::cout << "  ";
        for (auto col : cols) {
            std::string_view col_sv(col.begin(), col.end());
            std::cout << col_sv << " ";
        }
        std::cout << std::endl;
    }
}

int main() {
    demo_basic_split();
    demo_csv_parsing();
    demo_key_value_parsing();
    demo_nmea_parsing();
    demo_split_lines();
    demo_parse_hex_bytes();
    demo_nested_split();

    std::cout << "\n=== Key Points ===" << std::endl;
    std::cout << "- split divides a range by delimiter" << std::endl;
    std::cout << "- Returns subranges (not strings)" << std::endl;
    std::cout << "- Use string_view to work with split results" << std::endl;
    std::cout << "- Perfect for parsing CSV, NMEA, key-value pairs" << std::endl;

    return 0;
}
