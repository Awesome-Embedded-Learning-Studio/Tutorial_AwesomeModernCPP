// Peripheral Initialization with Functional Error Handling
// Demonstrates embedded peripheral init chains using Result types

#include <iostream>
#include <expected>
#include <string>

// Peripheral error types
struct PeripheralError {
    enum Code {
        ClockNotEnabled,
        GPIOInitFailed,
        PeripheralInitFailed,
        DMAConfigFailed,
        InvalidParameter
    };
    Code code;
    const char* peripheral_name;

    static PeripheralError clock_failed(const char* name) {
        return {ClockNotEnabled, name};
    }

    static PeripheralError init_failed(const char* name) {
        return {PeripheralInitFailed, name};
    }

    const char* to_string() const {
        switch (code) {
            case ClockNotEnabled: return "Clock not enabled";
            case GPIOInitFailed: return "GPIO init failed";
            case PeripheralInitFailed: return "Peripheral init failed";
            case DMAConfigFailed: return "DMA config failed";
            case InvalidParameter: return "Invalid parameter";
        }
        return "Unknown error";
    }
};

template<typename T>
using PeriphResult = std::expected<T, PeripheralError>;

// Specialization for void operations
using VoidPeriphResult = std::expected<void, PeripheralError>;

// Peripheral initialization steps
VoidPeriphResult enable_clock(const char* peripheral_name) {
    std::cout << "  Enabling clock for " << peripheral_name << "..." << std::endl;
    // Simulate success
    return {};
}

VoidPeriphResult init_gpio(uint8_t pin, const char* mode) {
    std::cout << "  Init GPIO " << static_cast<int>(pin) << " as " << mode << "..." << std::endl;
    // Simulate success
    return {};
}

VoidPeriphResult init_uart(uint32_t baudrate) {
    std::cout << "  Init UART at " << baudrate << " baud..." << std::endl;
    // Simulate success
    return {};
}

VoidPeriphResult init_dma_channel(uint8_t channel) {
    std::cout << "  Init DMA channel " << static_cast<int>(channel) << "..." << std::endl;
    // Simulate success
    return {};
}

// TRY macro for clean propagation
#define TRY(...) ({ \
    auto _result = (__VA_ARGS__); \
    if (!_result) return std::unexpected(_result.error()); \
    })

// Complete UART system initialization
VoidPeriphResult init_uart_system() {
    const char* uart_name = "USART1";

    std::cout << "Initializing " << uart_name << "..." << std::endl;

    // Enable clock
    TRY(enable_clock(uart_name));

    // Configure GPIO pins
    TRY(init_gpio(9, "alternate"));
    TRY(init_gpio(10, "alternate"));

    // Initialize UART peripheral
    TRY(init_uart(115200));

    // Setup DMA
    TRY(init_dma_channel(4));

    std::cout << uart_name << " initialized successfully!" << std::endl;
    return {};
}

// SPI initialization
VoidPeriphResult init_spi_system() {
    const char* spi_name = "SPI1";

    std::cout << "Initializing " << spi_name << "..." << std::endl;

    TRY(enable_clock(spi_name));
    TRY(init_gpio(13, "alternate"));  // SCK
    TRY(init_gpio(14, "input"));      // MISO
    TRY(init_gpio(15, "alternate"));  // MOSI

    std::cout << spi_name << " initialized successfully!" << std::endl;
    return {};
}

// Template-based initialization chain
template<typename... Inits>
VoidPeriphResult init_peripheral_chain(const char* name, Inits&&... inits) {
    std::cout << "Initializing " << name << "..." << std::endl;

    (TRY(inits), ...);

    std::cout << name << " initialized successfully!" << std::endl;
    return {};
}

void demo_manual_init() {
    std::cout << "=== Manual Initialization Chain ===" << std::endl;

    auto result = init_uart_system();

    if (!result) {
        std::cout << "Init failed: " << result.error().to_string()
                  << " (" << result.error().peripheral_name << ")" << std::endl;
    }
}

void demo_template_init() {
    std::cout << "\n=== Template-Based Init ===" << std::endl;

    auto result = init_peripheral_chain(
        "SPI1",
        []() { return enable_clock("SPI1"); },
        []() { return init_gpio(13, "alternate"); },
        []() { return init_gpio(14, "input"); },
        []() { return init_gpio(15, "alternate"); }
    );

    if (!result) {
        std::cout << "Init failed: " << result.error().to_string() << std::endl;
    }
}

// Comparison with error code style
void error_code_style_init() {
    std::cout << "\n=== Error Code Style (for comparison) ===" << std::endl;

    int err = 0;

    std::cout << "Initializing USART1..." << std::endl;

    auto r1 = enable_clock("USART1");
    if (!r1) { err = 1; goto error; }

    auto r2 = init_gpio(9, "alternate");
    if (!r2) { err = 2; goto error; }

    auto r3 = init_uart(115200);
    if (!r3) { err = 3; goto error; }

    std::cout << "USART1 initialized!" << std::endl;
    return;

error:
    std::cout << "Init failed with error code " << err << std::endl;
}

void demo_comparison() {
    std::cout << "\n=== Style Comparison ===" << std::endl;
    std::cout << "Error code style requires goto or nested ifs" << std::endl;
    std::cout << "Functional style with TRY macro is linear and readable" << std::endl;
    std::cout << "Both have zero runtime overhead" << std::endl;
}

int main() {
    demo_manual_init();
    demo_template_init();
    error_code_style_init();
    demo_comparison();

    std::cout << "\n=== Benefits ===" << std::endl;
    std::cout << "- Linear code flow (top to bottom)" << std::endl;
    std::cout << "- Error information preserved through chain" << std::endl;
    std::cout << "- Compile-time type safety" << std::endl;
    std::cout << "- Zero overhead compared to error codes" << std::endl;

    return 0;
}
