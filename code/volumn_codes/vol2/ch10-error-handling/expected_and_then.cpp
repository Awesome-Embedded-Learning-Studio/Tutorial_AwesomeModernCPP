#include <expected>
#include <string>
#include <iostream>

std::expected<int, std::string> parse_int(const std::string& s) {
    if (s.empty()) {
        return std::unexpected("Empty input");
    }
    try {
        std::size_t pos = 0;
        int value = std::stoi(s, &pos);
        if (pos != s.size()) {
            return std::unexpected("Invalid character in input");
        }
        return value;
    } catch (...) {
        return std::unexpected("Value out of range");
    }
}

std::expected<int, std::string> validate_positive(int value) {
    if (value > 0) return value;
    return std::unexpected("Value must be positive");
}

std::expected<double, std::string> safe_divide(int num, int denom) {
    if (denom == 0) {
        return std::unexpected("Division by zero");
    }
    return static_cast<double>(num) / denom;
}

int main() {
    std::string input = "42";

    auto result = parse_int(input)
        .and_then(validate_positive)
        .and_then([](int v) {
            return safe_divide(v, 2);
        });

    if (result) {
        std::cout << "Result: " << *result << "\n";  // 21.0
    } else {
        std::cout << "Error: " << result.error() << "\n";
    }
}
