// Take and Drop Views - Slicing Ranges
// Demonstrates std::views::take and std::views::drop

#include <iostream>
#include <ranges>
#include <vector>

void demo_take() {
    std::cout << "=== Take Demo ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // Take first 5 elements
    auto first_five = std::views::take(data, 5);

    std::cout << "First 5: ";
    for (int x : first_five) {
        std::cout << x << " ";
    }
    std::cout << std::endl;

    // Take more than available
    auto take_all = std::views::take(data, 100);
    std::cout << "Take 100 (only " << std::ranges::distance(take_all) << " available)" << std::endl;
}

void demo_drop() {
    std::cout << "\n=== Drop Demo ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // Skip first 3 elements
    auto after_three = std::views::drop(data, 3);

    std::cout << "After dropping first 3: ";
    for (int x : after_three) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void demo_take_drop_combination() {
    std::cout << "\n=== Take + Drop Combination ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // Drop first 2, then take 4 (get elements 3-6)
    auto middle = std::views::take(std::views::drop(data, 2), 4);

    std::cout << "Elements 3-6: ";
    for (int x : middle) {
        std::cout << x << " ";
    }
    std::cout << std::endl;

    // Using pipe syntax
    auto middle_pipe = data | std::views::drop(2) | std::views::take(4);
    std::cout << "Same with pipes: ";
    for (int x : middle_pipe) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void demo_protocol_parsing() {
    std::cout << "\n=== Protocol Parsing ===" << std::endl;

    // Simulated packet: [HEADER(2)] [DATA(4)] [CRC(2)]
    std::vector<uint8_t> packet = {
        0xAA, 0x55,  // Header
        0x01, 0x02, 0x03, 0x04,  // Data
        0x12, 0x34   // CRC
    };

    // Skip header, take data
    auto payload = std::views::take(std::views::drop(packet, 2), 4);

    std::cout << "Payload: ";
    for (uint8_t b : payload) {
        std::cout << std::hex << "0x" << static_cast<int>(b) << " " << std::dec;
    }
    std::cout << std::endl;

    // Get CRC (last 2 bytes)
    auto crc = std::views::drop(packet, packet.size() - 2);
    std::cout << "CRC: ";
    for (uint8_t b : crc) {
        std::cout << std::hex << "0x" << static_cast<int>(b) << " " << std::dec;
    }
    std::cout << std::endl;
}

void demo_sliding_window() {
    std::cout << "\n=== Sliding Window ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8};

    // Create sliding windows of size 3
    std::cout << "Sliding windows (size 3):" << std::endl;
    for (size_t start = 0; start <= data.size() - 3; ++start) {
        auto window = std::views::drop(data, start) | std::views::take(3);
        std::cout << "  Window " << start << ": ";
        for (int x : window) {
            std::cout << x << " ";
        }
        std::cout << std::endl;
    }
}

void demo_take_while() {
    std::cout << "\n=== Take While ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5, 3, 2, 1};

    // Take while elements are increasing
    auto increasing = std::views::take_while(data, [prev = 0](int x) mutable {
        bool result = x > prev;
        prev = x;
        return result;
    });

    std::cout << "Increasing prefix: ";
    for (int x : increasing) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void demo_embedded_buffer_processing() {
    std::cout << "\n=== Embedded Buffer Processing ===" << std::endl;

    // UART receive buffer
    std::vector<uint8_t> rx_buffer = {
        0x02,  // STX
        0x10, 0x20, 0x30,  // Data
        0x03,  // ETX
        0xFF, 0xFF,  // Padding
        0x02, 0x40, 0x50, 0x03  // Another message
    };

    // Process first message (5 bytes starting from beginning)
    auto message1 = rx_buffer | std::views::take(5);

    std::cout << "Message 1: ";
    for (uint8_t b : message1) {
        std::cout << std::hex << "0x" << static_cast<int>(b) << " " << std::dec;
    }
    std::cout << std::endl;

    // Process second message (skip first message)
    auto message2 = rx_buffer | std::views::drop(7) | std::views::take(4);

    std::cout << "Message 2: ";
    for (uint8_t b : message2) {
        std::cout << std::hex << "0x" << static_cast<int>(b) << " " << std::dec;
    }
    std::cout << std::endl;
}

int main() {
    demo_take();
    demo_drop();
    demo_take_drop_combination();
    demo_protocol_parsing();
    demo_sliding_window();
    demo_take_while();
    demo_embedded_buffer_processing();

    std::cout << "\n=== Key Points ===" << std::endl;
    std::cout << "- take(n): Gets first n elements" << std::endl;
    std::cout << "- drop(n): Skips first n elements" << std::endl;
    std::cout << "- Can be combined for extracting ranges" << std::endl;
    std::cout << "- Perfect for protocol parsing" << std::endl;
    std::cout << "- O(1) operation, no copying" << std::endl;

    return 0;
}
