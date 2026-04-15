// Transform Views - Converting Each Element
// Demonstrates std::views::transform for data transformation

#include <iostream>
#include <ranges>
#include <vector>
#include <cmath>

void demo_basic_transform() {
    std::cout << "=== Basic Transform ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5};

    // Double each value
    auto doubled = std::views::transform(data, [](int x) {
        return x * 2;
    });

    std::cout << "Doubled: ";
    for (int x : doubled) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void demo_adc_to_voltage() {
    std::cout << "\n=== ADC to Voltage Conversion ===" << std::endl;

    std::vector<uint16_t> adc_values = {0, 1024, 2048, 3072, 4095};

    // Convert 12-bit ADC to voltage (0-3.3V)
    auto voltages = std::views::transform(adc_values, [](uint16_t adc) {
        return adc * 3.3f / 4095.0f;
    });

    std::cout << "ADC -> Voltage:" << std::endl;
    size_t i = 0;
    for (float v : voltages) {
        std::cout << "  " << adc_values[i] << " -> " << v << " V" << std::endl;
        i++;
    }
}

void demo_chained_transform() {
    std::cout << "\n=== Chained Transform ===" << std::endl;

    std::vector<int> celsius = {-40, -10, 0, 20, 37, 100};

    // Celsius -> Fahrenheit -> String
    auto formatted = celsius
        | std::views::transform([](int c) {
            return c * 9.0f / 5.0f + 32.0f;  // To Fahrenheit
        })
        | std::views::transform([](float f) {
            int whole = static_cast<int>(f);
            int frac = static_cast<int>((f - whole) * 10);
            return std::to_string(whole) + "." + std::to_string(frac) + " F";
        });

    std::cout << "Temperature conversions:" << std::endl;
    size_t i = 0;
    for (const auto& s : formatted) {
        std::cout << "  " << celsius[i] << " C -> " << s << std::endl;
        i++;
    }
}

void demo_filter_then_transform() {
    std::cout << "\n=== Filter then Transform ===" << std::endl;

    std::vector<int> readings = {12, 45, 230, 67, 340, 89, 56, 180};

    // Filter valid range, then convert to voltage
    auto voltages = readings
        | std::views::filter([](int v) { return v >= 50 && v <= 300; })
        | std::views::transform([](int adc) {
            return adc * 3.3f / 4095.0f;
        });

    std::cout << "Valid voltages: ";
    for (float v : voltages) {
        std::cout << v << " V ";
    }
    std::cout << std::endl;
}

void demo_transform_with_state() {
    std::cout << "\n=== Transform with State ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5};

    // Running total (cumulative sum)
    int offset = 100;
    auto cumulative = std::views::transform(
        data,
        [offset, sum = 0](int x) mutable {
            sum += x;
            return sum + offset;
        }
    );

    std::cout << "Cumulative + " << offset << ": ";
    for (int x : cumulative) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void demo_indexed_transform() {
    std::cout << "\n=== Indexed Transform ===" << std::endl;

    std::vector<int> values = {10, 20, 30, 40, 50};

    // Transform with index
    auto indexed = std::views::transform(
        std::views::iota(size_t{0}, values.size()),
        [&](size_t i) {
            return "[" + std::to_string(i) + "]=" + std::to_string(values[i]);
        }
    );

    std::cout << "Indexed values: ";
    for (const auto& s : indexed) {
        std::cout << s << " ";
    }
    std::cout << std::endl;
}

void demo_multiple_outputs() {
    std::cout << "\n=== Multiple Output Formats ===" << std::endl;

    std::vector<float> temperatures = {20.5f, 22.3f, 19.8f, 21.0f};

    // Format as hex, oct, and binary
    auto to_hex = [](float f) {
        return std::to_string(static_cast<int>(f)) + "h";
    };

    auto hex_temps = temperatures | std::views::transform(to_hex);

    std::cout << "Hex format: ";
    for (const auto& s : hex_temps) {
        std::cout << s << " ";
    }
    std::cout << std::endl;
}

void demo_calibration_curve() {
    std::cout << "\n=== Calibration Curve ===" << std::endl;

    std::vector<int> raw_values = {1000, 2000, 3000, 4000};

    // Apply second-order calibration
    struct Calibration {
        float a, b, c;  // ax^2 + bx + c
    } cal = {0.0002f, 1.001f, 0.0f};

    auto calibrated = std::views::transform(raw_values, [cal](int raw) {
        float x = static_cast<float>(raw);
        return cal.a * x * x + cal.b * x + cal.c;
    });

    std::cout << "Calibrated values:" << std::endl;
    size_t i = 0;
    for (float v : calibrated) {
        std::cout << "  " << raw_values[i] << " -> " << v << std::endl;
        i++;
    }
}

int main() {
    demo_basic_transform();
    demo_adc_to_voltage();
    demo_chained_transform();
    demo_filter_then_transform();
    demo_transform_with_state();
    demo_indexed_transform();
    demo_multiple_outputs();
    demo_calibration_curve();

    std::cout << "\n=== Key Points ===" << std::endl;
    std::cout << "- Transform applies a function to each element" << std::endl;
    std::cout << "- Can be chained with filter and other views" << std::endl;
    std::cout << "- Lazy evaluation: no temporary storage" << std::endl;
    std::cout << "- Perfect for unit conversion and calibration" << std::endl;

    return 0;
}
