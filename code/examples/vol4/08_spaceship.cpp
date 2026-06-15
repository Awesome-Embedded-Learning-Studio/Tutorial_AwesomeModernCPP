// C++20 spaceship operator (<=>) demo
#include <compare>
#include <cstdint>
#include <iostream>
#include <string>

// ---- Example 1: default <=> generates all comparisons ----
struct SensorData {
    uint8_t id;
    int16_t value;

    auto operator<=>(const SensorData&) const = default;
    bool operator==(const SensorData&) const = default;
};

// ---- Example 2: custom <=> for version comparison ----
struct Version {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;

    std::strong_ordering operator<=>(const Version& other) const {
        if (auto cmp = major <=> other.major; cmp != 0)
            return cmp;
        if (auto cmp = minor <=> other.minor; cmp != 0)
            return cmp;
        return patch <=> other.patch;
    }
    bool operator==(const Version&) const = default;
};

// ---- Example 3: comparing with different ordering categories ----
struct SafeFloat {
    float value;

    std::partial_ordering operator<=>(const SafeFloat& other) const {
        if (value != value || other.value != other.value) // NaN check
            return std::partial_ordering::unordered;
        if (value < other.value)
            return std::partial_ordering::less;
        if (value > other.value)
            return std::partial_ordering::greater;
        return std::partial_ordering::equivalent;
    }
    bool operator==(const SafeFloat& other) const { return value == other.value; }
};

int main() {
    // Example 1: default comparison
    SensorData s1{1, 100};
    SensorData s2{1, 200};
    SensorData s3{1, 100};

    std::cout << std::boolalpha;
    std::cout << "s1 == s3: " << (s1 == s3) << "\n";
    std::cout << "s1 < s2:  " << (s1 < s2) << "\n";
    std::cout << "s2 > s1:  " << (s2 > s1) << "\n";

    // Example 2: version comparison
    Version v1{1, 2, 3};
    Version v2{1, 2, 4};
    Version v3{2, 0, 0};

    std::cout << "\nVersion comparison:\n";
    std::cout << "v1 < v2: " << (v1 < v2) << "\n";
    std::cout << "v1 < v3: " << (v1 < v3) << "\n";
    std::cout << "v2 < v3: " << (v2 < v3) << "\n";

    auto cmp = v1 <=> v2;
    if (cmp < 0)
        std::cout << "v1 is less than v2\n";

    // Example 3: partial ordering with float
    SafeFloat a{1.0f};
    SafeFloat b{2.0f};
    SafeFloat c{1.0f};

    std::cout << "\nSafeFloat comparison:\n";
    std::cout << "a == c: " << (a == c) << "\n";
    std::cout << "a < b:  " << (a < b) << "\n";

    auto fc = a <=> b;
    if (fc == std::partial_ordering::less)
        std::cout << "a is less than b (partial_ordering)\n";

    return 0;
}
