// 15_cpp98_overloading_default.cpp
// 演示 C++98 函数重载、Logger 类重载与默认参数

#include <cstdint>
#include <cstdio>

void print(int value) {
    std::printf("int:    %d\n", value);
}

void print(float value) {
    std::printf("float:  %.2f\n", value);
}

void print(const char* str) {
    std::printf("string: %s\n", str);
}

void init_uart(int baudrate) {
    std::printf("UART init: baud=%d (default: 8N1)\n", baudrate);
}

void init_uart(int baudrate, int databits, int stopbits) {
    std::printf("UART init: baud=%d, data=%d, stop=%d\n", baudrate, databits, stopbits);
}

class Logger {
  public:
    void log(int value) { std::printf("[INFO] %d\n", value); }
    void log(float value) { std::printf("[INFO] %.2f\n", value); }
    void log(const char* message) { std::printf("[INFO] %s\n", message); }
    void log(const uint8_t* data, size_t length) {
        std::printf("[INFO] Data (%zu bytes): ", length);
        for (size_t i = 0; i < length; ++i) {
            std::printf("%02X ", data[i]);
        }
        std::printf("\n");
    }
};

void configure_uart(int baudrate, int databits = 8, int stopbits = 1, char parity = 'N') {
    std::printf("Config UART: %d baud, %d-%d-%c\n", baudrate, databits, stopbits, parity);
}

int main() {
    std::printf("=== Function Overloading ===\n");
    print(42);
    print(3.14f);
    print("Hello, C++98!");

    std::printf("\n=== Overloading by Count ===\n");
    init_uart(115200);
    init_uart(9600, 7, 2);

    std::printf("\n=== Logger Overloads ===\n");
    Logger logger;
    logger.log(42);
    logger.log(25.5f);
    logger.log("System started");
    uint8_t packet[] = {0x01, 0x02, 0xAB};
    logger.log(packet, 3);

    std::printf("\n=== Default Arguments ===\n");
    configure_uart(115200);
    configure_uart(115200, 8);
    configure_uart(115200, 8, 2);
    configure_uart(9600, 7, 1, 'E');

    return 0;
}
