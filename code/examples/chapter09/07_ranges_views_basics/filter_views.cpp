// Filter Views - Selecting Elements with Predicates
// Demonstrates std::views::filter for data filtering

#include <iostream>
#include <ranges>
#include <vector>
#include <algorithm>
#include <cstdint>

void demo_basic_filter() {
    std::cout << "=== Basic Filter ===" << std::endl;

    std::vector<int> readings = {12, 45, 23, 67, 34, 89, 56};

    // Filter for values > 50
    auto high_values = std::views::filter(readings, [](int v) {
        return v > 50;
    });

    std::cout << "Values > 50: ";
    for (int v : high_values) {
        std::cout << v << " ";
    }
    std::cout << std::endl;
}

void demo_range_filter() {
    std::cout << "\n=== Filter with Range ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // Filter even numbers
    auto evens = std::views::filter(data, [](int x) { return x % 2 == 0; });

    std::cout << "Even numbers: ";
    for (int n : evens) {
        std::cout << n << " ";
    }
    std::cout << std::endl;
}

void demo_combined_filter() {
    std::cout << "\n=== Combined Filter ===" << std::endl;

    std::vector<int> readings = {12, 45, 230, 67, 340, 89, 56, 180};

    // Chain multiple filters
    auto valid = readings
        | std::views::filter([](int v) { return v >= 50; })
        | std::views::filter([](int v) { return v <= 300; });

    std::cout << "Valid readings (50-300): ";
    for (int v : valid) {
        std::cout << v << " ";
    }
    std::cout << std::endl;
}

void demo_embedded_adc_filtering() {
    std::cout << "\n=== Embedded: ADC Filtering ===" << std::endl;

    // Simulated ADC readings with some invalid values
    std::vector<uint16_t> adc_readings = {
        100, 4500,  // Invalid (> 4095)
        2048, 2100,
        0,          // Invalid (too low)
        2000, 2050,
        4096,       // Invalid
        1980
    };

    // Filter for valid ADC range (100-4095)
    auto valid_readings = std::views::filter(adc_readings, [](uint16_t v) {
        return v >= 100 && v <= 4095;
    });

    std::cout << "Valid ADC readings: ";
    for (uint16_t v : valid_readings) {
        std::cout << v << " ";
    }
    std::cout << std::endl;

    // Count invalid readings
    auto invalid_readings = std::views::filter(adc_readings, [](uint16_t v) {
        return v < 100 || v > 4095;
    });

    size_t invalid_count = 0;
    for ([[maybe_unused]] auto v : invalid_readings) {
        invalid_count++;
    }
    std::cout << "Invalid readings: " << invalid_count << std::endl;
}

void demo_filter_with_capture() {
    std::cout << "\n=== Filter with Capture ===" << std::endl;

    std::vector<int> data = {10, 20, 30, 40, 50};

    int threshold = 25;

    auto above_threshold = std::views::filter(data, [threshold](int x) {
        return x > threshold;
    });

    std::cout << "Values > " << threshold << ": ";
    for (int x : above_threshold) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void demo_filter_mutating() {
    std::cout << "\n=== Filter View Modification ===" << std::endl;

    std::vector<int> data = {1, 2, 3, 4, 5};

    // Note: Filter view doesn't allow direct modification
    // But you can iterate and modify underlying data
    auto is_even = [](int x) { return x % 2 == 0; };
    auto evens = std::views::filter(data, is_even);
    (void)evens;

    std::cout << "Even numbers: ";
    for (int& x : data) {  // Iterate original, check condition manually
        if (is_even(x)) {
            std::cout << x << " ";
            x *= 10;  // Modify
        }
    }
    std::cout << std::endl;

    std::cout << "After modification: ";
    for (int x : data) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
}

void demo_filter_with_indices() {
    std::cout << "\n=== Filter with Indices ===" << std::endl;

    std::vector<int> data = {10, 20, 30, 40, 50};

    // Use iota to get indices, then filter
    auto indices = std::views::iota(size_t{0}, data.size());
    auto selected_indices = std::views::filter(indices, [&](size_t i) {
        return data[i] > 25;
    });

    std::cout << "Indices of values > 25: ";
    for (size_t idx : selected_indices) {
        std::cout << idx << "(" << data[idx] << ") ";
    }
    std::cout << std::endl;
}

int main() {
    demo_basic_filter();
    demo_range_filter();
    demo_combined_filter();
    demo_embedded_adc_filtering();
    demo_filter_with_capture();
    demo_filter_mutating();
    demo_filter_with_indices();

    std::cout << "\n=== Key Points ===" << std::endl;
    std::cout << "- Filter views select elements matching a predicate" << std::endl;
    std::cout << "- Multiple filters can be chained with |" << std::endl;
    std::cout << "- No temporary storage needed" << std::endl;
    std::cout << "- Perfect for sensor data validation" << std::endl;

    return 0;
}
