// ADC Data Processing Pipeline
// Demonstrates multi-stage ADC data processing with ranges

#include <iostream>
#include <ranges>
#include <vector>
#include <cmath>
#include <iomanip>

class ADCProcessor {
public:
    void add_sample(uint16_t raw) {
        samples_.push_back(raw);
        keep_recent(100);
    }

    // Full processing pipeline
    std::vector<float> process() {
        auto pipeline = samples_
            | std::views::filter([](uint16_t v) {
                return v >= 100 && v <= 4000;  // Stage 1: Filter invalid
            })
            | std::views::transform([](uint16_t v) {
                return v * 3.3f / 4095.0f;     // Stage 2: To voltage
            })
            | std::views::transform([](float voltage) {
                return 1.001f * voltage + 0.0002f * voltage * voltage;  // Stage 3: Calibrate
            });

        return std::vector<float>(pipeline.begin(), pipeline.end());
    }

    // Get filtered current value with moving average
    std::optional<float> get_filtered_value() {
        if (samples_.empty()) {
            return std::nullopt;
        }

        auto pipeline = samples_
            | std::views::filter([](uint16_t v) {
                return v >= 100 && v <= 4000;
            })
            | std::views::transform([](uint16_t v) {
                return v * 3.3f / 4095.0f;
            });

        float sum = 0.0f;
        size_t count = 0;
        for (float v : pipeline) {
            sum += v;
            count++;
        }

        return count > 0 ? std::optional<float>(sum / count) : std::nullopt;
    }

private:
    std::vector<uint16_t> samples_;

    void keep_recent(size_t n) {
        if (samples_.size() > n) {
            samples_.erase(samples_.begin(), samples_.end() - n);
        }
    }
};

void demo_basic_pipeline() {
    std::cout << "=== ADC Pipeline Demo ===" << std::endl;

    ADCProcessor adc;

    // Add samples with some invalid values
    for (uint16_t v : {50, 4500, 2048, 2100, 100, 4100, 2000, 2050}) {
        adc.add_sample(v);
    }

    auto results = adc.process();

    std::cout << "Processed voltages: " << std::endl;
    for (size_t i = 0; i < results.size(); ++i) {
        std::cout << "  [" << i << "] " << std::fixed << std::setprecision(4)
                  << results[i] << " V" << std::endl;
    }
}

void demo_moving_average() {
    std::cout << "\n=== Moving Average ===" << std::endl;

    ADCProcessor adc;

    // Add samples
    for (uint16_t v : {2048, 2100, 2050, 2075, 2060}) {
        adc.add_sample(v);
    }

    if (auto avg = adc.get_filtered_value()) {
        std::cout << "Average: " << std::fixed << std::setprecision(4)
                  << *avg << " V" << std::endl;
    }
}

void demo_stage_by_stage() {
    std::cout << "\n=== Stage-by-Stage Breakdown ===" << std::endl;

    std::vector<uint16_t> samples = {50, 4500, 2048, 2100, 100};

    std::cout << "Original: ";
    for (auto s : samples) {
        std::cout << s << " ";
    }
    std::cout << std::endl;

    // Stage 1: Filter
    auto stage1 = samples | std::views::filter([](uint16_t v) {
        return v >= 100 && v <= 4000;
    });
    std::cout << "After filter: ";
    for (auto v : stage1) {
        std::cout << v << " ";
    }
    std::cout << std::endl;

    // Stage 2: To voltage
    auto stage2 = stage1 | std::views::transform([](uint16_t v) {
        return v * 3.3f / 4095.0f;
    });
    std::cout << "As voltage: ";
    for (auto v : stage2) {
        std::cout << std::fixed << std::setprecision(3) << v << " ";
    }
    std::cout << std::endl;
}

void demo_calibration_curve() {
    std::cout << "\n=== Calibration Curve ===" << std::endl;

    std::vector<uint16_t> samples = {1000, 2000, 3000, 4000};

    // Two-point calibration
    struct CalPoint {
        uint16_t adc;
        float actual;
    };
    CalPoint cal1 = {1000, 0.8f};
    CalPoint cal2 = {4000, 3.2f};

    float scale = (cal2.actual - cal1.actual) / (cal2.adc - cal1.adc);
    float offset = cal1.actual - cal1.adc * scale;

    auto calibrated = samples | std::views::transform([scale, offset](uint16_t adc) {
        return adc * scale + offset;
    });

    std::cout << "Calibrated voltages:" << std::endl;
    size_t i = 0;
    for (float v : calibrated) {
        std::cout << "  " << samples[i] << " ADC -> " << std::fixed << std::setprecision(3)
                  << v << " V" << std::endl;
        i++;
    }
}

void demo_statistics_pipeline() {
    std::cout << "\n=== Statistics Pipeline ===" << std::endl;

    std::vector<uint16_t> samples = {2000, 2050, 2100, 1950, 2025};

    auto valid = samples
        | std::views::filter([](uint16_t v) { return v >= 1000 && v <= 3500; })
        | std::views::transform([](uint16_t v) { return v * 3.3f / 4095.0f; });

    float sum = 0.0f;
    float min_val = 10.0f;
    float max_val = 0.0f;
    size_t count = 0;

    for (float v : valid) {
        sum += v;
        min_val = std::min(min_val, v);
        max_val = std::max(max_val, v);
        count++;
    }

    if (count > 0) {
        float avg = sum / count;
        std::cout << "Statistics:" << std::endl;
        std::cout << "  Count: " << count << std::endl;
        std::cout << "  Min: " << min_val << " V" << std::endl;
        std::cout << "  Max: " << max_val << " V" << std::endl;
        std::cout << "  Avg: " << avg << " V" << std::endl;
    }
}

int main() {
    demo_basic_pipeline();
    demo_moving_average();
    demo_stage_by_stage();
    demo_calibration_curve();
    demo_statistics_pipeline();

    std::cout << "\n=== Benefits ===" << std::endl;
    std::cout << "- No temporary vectors between stages" << std::endl;
    std::cout << "- Single pass through data" << std::endl;
    std::cout << "- Easy to add/remove stages" << std::endl;
    std::cout << "- Memory efficient for embedded" << std::endl;

    return 0;
}
