// Configuration Builder Pattern with Lambda
// Demonstrates using lambda expressions to create fluent configuration APIs

#include <iostream>
#include <functional>
#include <cstdint>

// Timer configuration structure
struct TimerConfig {
    uint32_t prescaler = 0;
    uint32_t period = 0;
    bool auto_reload = false;
    uint32_t clock_div = 1;

    void print() const {
        std::cout << "TimerConfig: prescaler=" << prescaler
                  << ", period=" << period
                  << ", auto_reload=" << (auto_reload ? "yes" : "no")
                  << ", clock_div=" << clock_div << std::endl;
    }
};

// Configuration builder function
TimerConfig make_timer_config(std::function<void(TimerConfig&)> builder) {
    TimerConfig config;
    builder(config);
    return config;
}

// UART configuration structure
struct UARTConfig {
    uint32_t baudrate = 115200;
    uint8_t data_bits = 8;
    uint8_t stop_bits = 1;
    bool parity_enable = false;
    bool parity_odd = false;

    void print() const {
        std::cout << "UARTConfig: baudrate=" << baudrate
                  << ", data_bits=" << static_cast<int>(data_bits)
                  << ", stop_bits=" << static_cast<int>(stop_bits)
                  << ", parity=" << (parity_enable ? (parity_odd ? "odd" : "even") : "none")
                  << std::endl;
    }
};

UARTConfig make_uart_config(std::function<void(UARTConfig&)> builder) {
    UARTConfig config;
    builder(config);
    return config;
}

// Chainable builder for more complex scenarios
class GPIOConfigBuilder {
public:
    GPIOConfigBuilder(int pin) : pin_(pin) {}

    GPIOConfigBuilder& as_output() {
        config_.direction = "output";
        return *this;
    }

    GPIOConfigBuilder& as_input() {
        config_.direction = "input";
        return *this;
    }

    GPIOConfigBuilder& pull_up() {
        config_.pull = "up";
        return *this;
    }

    GPIOConfigBuilder& pull_down() {
        config_.pull = "down";
        return *this;
    }

    GPIOConfigBuilder& speed_high() {
        config_.speed = "high";
        return *this;
    }

    struct GPIOConfig {
        std::string direction = "input";
        std::string pull = "none";
        std::string speed = "low";
        int pin;
    };

    GPIOConfig build() {
        config_.pin = pin_;
        return config_;
    }

private:
    int pin_;
    GPIOConfig config_;
};

void print_gpio_config(const GPIOConfigBuilder::GPIOConfig& config) {
    std::cout << "GPIO Pin " << config.pin << ": "
              << config.direction << ", "
              << "pull=" << config.pull << ", "
              << "speed=" << config.speed << std::endl;
}

int main() {
    std::cout << "=== Configuration Builder Demo ===" << std::endl;

    // Example 1: 1kHz timer configuration using lambda
    std::cout << "\n--- 1kHz Timer Configuration ---" << std::endl;
    auto timer1_cfg = make_timer_config([](TimerConfig& c) {
        c.prescaler = 7200 - 1;    // 72MHz / 7200 = 10kHz
        c.period = 10 - 1;          // 10kHz / 10 = 1kHz
        c.auto_reload = true;
    });
    timer1_cfg.print();

    // Example 2: PWM timer configuration
    std::cout << "\n--- PWM Timer Configuration ---" << std::endl;
    auto pwm_cfg = make_timer_config([](TimerConfig& c) {
        c.prescaler = 1 - 1;        // No prescaling
        c.period = 1000 - 1;        // PWM period
        c.auto_reload = true;
        c.clock_div = 2;
    });
    pwm_cfg.print();

    // Example 3: UART configuration with lambda
    std::cout << "\n--- UART Configuration ---" << std::endl;
    auto uart_cfg = make_uart_config([](UARTConfig& c) {
        c.baudrate = 9600;
        c.data_bits = 8;
        c.stop_bits = 1;
        c.parity_enable = false;
    });
    uart_cfg.print();

    // Example 4: UART with parity
    std::cout << "\n--- UART with Parity ---" << std::endl;
    auto uart_parity = make_uart_config([](UARTConfig& c) {
        c.baudrate = 115200;
        c.data_bits = 9;           // 8 data + 1 parity
        c.parity_enable = true;
        c.parity_odd = true;
    });
    uart_parity.print();

    // Example 5: Chainable GPIO builder
    std::cout << "\n--- GPIO Chainable Builder ---" << std::endl;
    auto gpio5 = GPIOConfigBuilder(5)
        .as_output()
        .speed_high()
        .build();
    print_gpio_config(gpio5);

    auto gpio12 = GPIOConfigBuilder(12)
        .as_input()
        .pull_up()
        .build();
    print_gpio_config(gpio12);

    // Example 6: Configuration from variables
    std::cout << "\n--- Configuration from Variables ---" << std::endl;
    int target_frequency = 5000;
    int system_clock = 72000000;

    auto computed_cfg = make_timer_config([target_frequency, system_clock](TimerConfig& c) {
        int prescaler = system_clock / (target_frequency * 1000);
        c.prescaler = prescaler - 1;
        c.period = 999;
        c.auto_reload = true;
        std::cout << "Computed prescaler: " << prescaler << " for " << target_frequency << " Hz" << std::endl;
    });
    computed_cfg.print();

    // Example 7: Conditional configuration
    std::cout << "\n--- Conditional Configuration ---" << std::endl;
    bool high_speed_mode = true;

    auto adaptive_cfg = make_timer_config([high_speed_mode](TimerConfig& c) {
        if (high_speed_mode) {
            c.prescaler = 1;
            c.period = 999;
            c.clock_div = 1;
        } else {
            c.prescaler = 100;
            c.period = 999;
            c.clock_div = 2;
        }
    });
    adaptive_cfg.print();

    return 0;
}
