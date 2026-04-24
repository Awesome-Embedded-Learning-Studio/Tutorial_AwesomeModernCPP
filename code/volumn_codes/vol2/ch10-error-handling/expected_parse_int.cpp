#include <expected>
#include <string>
#include <iostream>

enum class ParseError {
    kEmptyInput,
    kInvalidCharacter,
    kOutOfRange,
};

std::expected<int, ParseError> parse_int(const std::string& s) {
    if (s.empty()) {
        return std::unexpected(ParseError::kEmptyInput);
    }

    try {
        std::size_t pos = 0;
        int value = std::stoi(s, &pos);
        if (pos != s.size()) {
            return std::unexpected(ParseError::kInvalidCharacter);
        }
        return value;
    } catch (...) {
        return std::unexpected(ParseError::kOutOfRange);
    }
}

int main() {
    auto r1 = parse_int("42");
    if (r1) {
        std::cout << "Value: " << r1.value() << "\n";  // 42
    }

    auto r2 = parse_int("42abc");
    if (!r2) {
        std::cout << "Error: " << static_cast<int>(r2.error()) << "\n";
        // 输出 Error: 1（kInvalidCharacter）
    }
}
