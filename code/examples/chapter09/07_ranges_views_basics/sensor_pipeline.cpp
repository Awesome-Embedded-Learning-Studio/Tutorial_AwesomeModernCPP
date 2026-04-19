// Complete Sensor Data Processing Pipeline
// Demonstrates a real embedded scenario using Ranges views

#include <iostream>
#include <ranges>
#include <vector>
#include <cmath>
#include <iomanip>
#include <cstdint>

// Sensor reading structure
struct SensorReading {
    uint8_t sensor_id;
    uint16_t raw_value;
    bool valid;

    float to_voltage() const {
        return raw_value * 3.3f / 4095.0f;
    }

    float to_celsius() const {
        // LM35: 10mV per degree C
        return to_voltage() * 100.0f;
    }

    float to_fahrenheit(float celsius) const {
        return celsius * 9.0f / 5.0f + 32.0f;
    }
};

std::vector<SensorReading> get_readings() {
    return {
        {1, 120, true},
        {2, 45, false},   // Invalid
        {3, 230, true},
        {4, 67, true},
        {5, 340, false},  // Invalid (> 300)
        {6, 89, true},
        {7, 4100, true},  // Invalid (> 4095)
        {8, 56, true}
    };
}

void demo_basic_pipeline() {
    std::cout << "=== Basic Sensor Pipeline ===" << std::endl;

    auto readings = get_readings();

    // Pipeline: filter valid -> convert to celsius -> filter reasonable temps
    auto valid_temps = readings
        | std::views::filter([](const SensorReading& r) { return r.valid; })
        | std::views::transform([](const SensorReading& r) { return r.to_celsius(); })
        | std::views::filter([](float t) { return t >= -50 && t <= 150; });

    std::cout << "Valid temperatures (C): ";
    for (float t : valid_temps) {
        std::cout << std::fixed << std::setprecision(1) << t << " ";
    }
    std::cout << std::endl;
}

void demo_fahrenheit_pipeline() {
    std::cout << "\n=== Fahrenheit Pipeline ===" << std::endl;

    auto readings = get_readings();

    // Pipeline: valid readings -> celsius -> fahrenheit -> display
    auto display_data = readings
        | std::views::filter([](const SensorReading& r) { return r.valid; })
        | std::views::transform([](const SensorReading& r) {
            float c = r.to_celsius();
            return std::make_pair(r.sensor_id, c * 9.0f / 5.0f + 32.0f);
        })
        | std::views::filter([](const auto& p) {
            return p.second >= -58 && p.second <= 302;  // Valid F range
        });

    std::cout << "Sensor readings (F):" << std::endl;
    for (const auto& [id, temp_f] : display_data) {
        std::cout << "  Sensor " << static_cast<int>(id) << ": "
                  << std::fixed << std::setprecision(1) << temp_f << " F" << std::endl;
    }
}

void demo_voltage_display() {
    std::cout << "\n=== Voltage Display ===" << std::endl;

    auto readings = get_readings();

    // Show voltage for valid readings
    auto voltages = readings
        | std::views::filter([](const SensorReading& r) {
            return r.valid && r.raw_value >= 100 && r.raw_value <= 4000;
        })
        | std::views::transform([](const SensorReading& r) {
            return std::make_tuple(r.sensor_id, r.to_voltage());
        });

    std::cout << "Sensor voltages:" << std::endl;
    for (const auto& [id, voltage] : voltages) {
        std::cout << "  Sensor " << static_cast<int>(id) << ": "
                  << std::fixed << std::setprecision(3) << voltage << " V" << std::endl;
    }
}

void demo_statistics() {
    std::cout << "\n=== Statistics Pipeline ===" << std::endl;

    auto readings = get_readings();

    // Get valid celsius readings for statistics
    auto temps = readings
        | std::views::filter([](const SensorReading& r) { return r.valid; })
        | std::views::transform([](const SensorReading& r) { return r.to_celsius(); });

    float sum = 0.0f;
    size_t count = 0;
    float min_val = 1000.0f;
    float max_val = -1000.0f;

    for (float t : temps) {
        sum += t;
        count++;
        min_val = std::min(min_val, t);
        max_val = std::max(max_val, t);
    }

    if (count > 0) {
        float avg = sum / count;
        std::cout << "Temperature statistics:" << std::endl;
        std::cout << "  Count: " << count << std::endl;
        std::cout << "  Min: " << min_val << " C" << std::endl;
        std::cout << "  Max: " << max_val << " C" << std::endl;
        std::cout << "  Avg: " << avg << " C" << std::endl;
    }
}

void demo_alarm_check() {
    std::cout << "\n=== Alarm Check ===" << std::endl;

    auto readings = get_readings();

    // Check for alarm conditions
    auto alarms = readings
        | std::views::filter([](const SensorReading& r) { return r.valid; })
        | std::views::transform([](const SensorReading& r) {
            return std::make_pair(r.sensor_id, r.to_celsius());
        })
        | std::views::filter([](const auto& p) {
            return p.second > 80;  // High temp alarm
        });

    std::cout << "High temperature alarms (> 80 C):" << std::endl;
    for (const auto& [id, temp] : alarms) {
        std::cout << "  ALARM: Sensor " << static_cast<int>(id)
                  << " at " << temp << " C" << std::endl;
    }
}

void demo_processing_chain() {
    std::cout << "\n=== Complete Processing Chain ===" << std::endl;

    auto readings = get_readings();

    // Full processing chain:
    // 1. Filter valid readings
    // 2. Filter reasonable ADC range
    // 3. Convert to celsius
    // 4. Filter reasonable temperature range
    // 5. Convert to fahrenheit
    // 6. Round to integer
    auto processed = readings
        | std::views::filter([](const SensorReading& r) { return r.valid; })
        | std::views::filter([](const SensorReading& r) {
            return r.raw_value >= 500 && r.raw_value <= 3500;
        })
        | std::views::transform([](const SensorReading& r) {
            return r.to_celsius();
        })
        | std::views::filter([](float c) {
            return c >= 0 && c <= 100;
        })
        | std::views::transform([](float c) {
            return static_cast<int>(c * 9.0f / 5.0f + 32.0f);
        });

    std::cout << "Processed values (int F): ";
    for (int f : processed) {
        std::cout << f << " F ";
    }
    std::cout << std::endl;
}

int main() {
    demo_basic_pipeline();
    demo_fahrenheit_pipeline();
    demo_voltage_display();
    demo_statistics();
    demo_alarm_check();
    demo_processing_chain();

    std::cout << "\n=== Key Points ===" << std::endl;
    std::cout << "- Views compose naturally into pipelines" << std::endl;
    std::cout << "- No temporary allocations" << std::endl;
    std::cout << "- Single pass through data" << std::endl;
    std::cout << "- Perfect for embedded sensor processing" << std::endl;

    return 0;
}
