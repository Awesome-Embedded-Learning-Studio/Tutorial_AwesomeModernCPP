// Protocol Parsing with Ranges Pipelines
// Demonstrates parsing binary protocols using ranges

#include <iostream>
#include <ranges>
#include <vector>
#include <cstdint>
#include <iomanip>

// Simulated SPI data reception (16-bit big-endian words)
std::vector<uint8_t> receive_spi_data() {
    return {0x01, 0x00,  // 0x0100
            0x00, 0x64,  // 0x0064
            0x00, 0x02,  // 0x0002
            0xFF, 0xFF};  // 0xFFFF (padding)
}

void demo_spi_parsing() {
    std::cout << "=== SPI Protocol Parsing ===" << std::endl;

    auto data = receive_spi_data();

    // Chunk into pairs
    auto chunks = data | std::views::chunk(2);

    std::cout << "Parsed words:" << std::endl;
    for (auto chunk : chunks) {
        if (chunk.size() == 2) {
            uint16_t high = chunk[0];
            uint16_t low = chunk[1];
            uint16_t word = (high << 8) | low;
            std::cout << "  0x" << std::hex << std::setw(4) << std::setfill('0') << word << std::dec << std::endl;
        }
    }
}

void demo_spi_filter_padding() {
    std::cout << "\n=== SPI with Padding Filter ===" << std::endl;

    auto data = receive_spi_data();

    // Chunk, convert to 16-bit, filter out padding
    auto valid_words = data
        | std::views::chunk(2)
        | std::views::transform([](auto chunk) {
            uint16_t high = chunk[0];
            uint16_t low = chunk[1];
            return (high << 8) | low;
        })
        | std::views::filter([](uint16_t w) { return w != 0xFFFF; });

    std::cout << "Valid words (no padding):" << std::endl;
    for (uint16_t w : valid_words) {
        std::cout << "  0x" << std::hex << w << std::dec << " (" << w << ")" << std::endl;
    }
}

void demo_packet_structure() {
    std::cout << "\n=== Structured Packet Parsing ===" << std::endl;

    // Packet: [CMD(1)] [LEN(1)] [DATA(N)] [CRC(2)]
    std::vector<uint8_t> packet = {
        0x01,        // Command
        0x04,        // Length
        0x10, 0x20, 0x30, 0x40,  // Data
        0xAA, 0xBB   // CRC
    };

    auto cmd = packet[0];
    auto len = packet[1];

    auto payload = std::views::drop(packet, 2);
    auto data = std::views::take(payload, len);
    auto crc = std::views::drop(payload, len);

    std::cout << "Command: 0x" << std::hex << static_cast<int>(cmd) << std::dec << std::endl;
    std::cout << "Length: " << static_cast<int>(len) << std::endl;
    std::cout << "Data: ";
    for (uint8_t b : data) {
        std::cout << std::hex << "0x" << static_cast<int>(b) << " " << std::dec;
    }
    std::cout << std::endl;
    std::cout << "CRC: ";
    for (uint8_t b : crc) {
        std::cout << std::hex << "0x" << static_cast<int>(b) << " " << std::dec;
    }
    std::cout << std::endl;
}

void demo_nmea_sentence() {
    std::cout << "\n=== NMEA Sentence Parsing ===" << std::endl;

    std::string nmea = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";

    // Remove checksum for demo
    auto clean = std::views::take(nmea, nmea.find('*'));

    // Split by comma
    auto parts = std::views::split(clean, ',');

    const char* field_names[] = {"Type", "Time", "Lat", "NS", "Lon", "EW", "Quality", "Sats"};
    int idx = 0;

    for (auto part : parts) {
        std::string_view sv(part.begin(), part.end());
        if (idx < 8) {
            std::cout << "  " << field_names[idx] << ": " << sv << std::endl;
        }
        idx++;
        if (idx >= 8) break;
    }
}

void demo_variable_length_packets() {
    std::cout << "\n=== Variable Length Packets ===" << std::endl;

    // Stream with multiple packets
    std::vector<uint8_t> stream = {
        // Packet 1
        0xAA, 0x02, 0x10, 0x20,
        // Packet 2
        0xAA, 0x03, 0x11, 0x22, 0x33,
        // Packet 3
        0xAA, 0x01, 0x40
    };

    size_t pos = 0;
    while (pos < stream.size()) {
        // Check for sync byte
        if (stream[pos] == 0xAA) {
            if (pos + 1 < stream.size()) {
                uint8_t len = stream[pos + 1];

                if (pos + 2 + len <= stream.size()) {
                    std::cout << "Packet at pos " << pos << ", len " << static_cast<int>(len) << ": ";

                    auto packet_data = std::views::drop(stream, pos + 2)
                                     | std::views::take(len);

                    for (uint8_t b : packet_data) {
                        std::cout << std::hex << "0x" << static_cast<int>(b) << " " << std::dec;
                    }
                    std::cout << std::endl;

                    pos += 2 + len;
                    continue;
                }
            }
        }
        pos++;
    }
}

void demo_hex_dump_pipeline() {
    std::cout << "\n=== Hex Dump Pipeline ===" << std::endl;

    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05};

    // Format as hex dump
    auto hex_format = data | std::views::transform([](uint8_t b) {
        char buf[8];
        snprintf(buf, sizeof(buf), "0x%02X", b);
        return std::string(buf);
    });

    std::cout << "Hex dump: ";
    for (const auto& s : hex_format) {
        std::cout << s << " ";
    }
    std::cout << std::endl;
}

int main() {
    demo_spi_parsing();
    demo_spi_filter_padding();
    demo_packet_structure();
    demo_nmea_sentence();
    demo_variable_length_packets();
    demo_hex_dump_pipeline();

    std::cout << "\n=== Key Points ===" << std::endl;
    std::cout << "- chunk(n) splits range into n-element subranges" << std::endl;
    std::cout << "- Pipelines allow complex parsing in readable form" << std::endl;
    std::cout << "- No temporary storage for intermediate results" << std::endl;
    std::cout << "- Perfect for embedded protocol parsing" << std::endl;

    return 0;
}
