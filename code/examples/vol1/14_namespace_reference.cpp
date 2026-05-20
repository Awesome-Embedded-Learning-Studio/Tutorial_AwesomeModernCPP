// 14_namespace_reference.cpp
// 演示命名空间、引用与作用域解析

#include <cstdio>

namespace sensor {
struct Reading {
    float temperature;
    float humidity;
};

void print_reading(const Reading& r) {
    std::printf("Temp: %.1f C, Humidity: %.1f%%\n", r.temperature, r.humidity);
}
} // namespace sensor

namespace hardware {
namespace gpio {
enum PinMode { INPUT, OUTPUT, ALTERNATE };

const char* mode_to_string(PinMode mode) {
    switch (mode) {
        case INPUT:
            return "INPUT";
        case OUTPUT:
            return "OUTPUT";
        case ALTERNATE:
            return "ALTERNATE";
    }
    return "UNKNOWN";
}
} // namespace gpio
} // namespace hardware

namespace hw = hardware;

namespace {
int internal_counter = 0;
void count_call() {
    ++internal_counter;
}
} // namespace

int status_code = 100;

void demo_scope_resolution() {
    int status_code = 50;
    std::printf("Local status:  %d\n", status_code);
    std::printf("Global status: %d\n", ::status_code);
}

void scale_by_ref(float& value, float factor) {
    value *= factor;
}

int main() {
    std::printf("=== Namespaces ===\n");
    sensor::Reading r{25.5f, 60.0f};
    sensor::print_reading(r);

    std::printf("Pin mode: %s\n", hw::gpio::mode_to_string(hw::gpio::OUTPUT));

    count_call();
    count_call();
    std::printf("Internal counter: %d\n", internal_counter);

    std::printf("\n=== References ===\n");
    float value = 10.0f;
    scale_by_ref(value, 2.5f);
    std::printf("After scaling: %.1f\n", value);

    std::printf("\n=== Scope Resolution ===\n");
    demo_scope_resolution();

    return 0;
}
