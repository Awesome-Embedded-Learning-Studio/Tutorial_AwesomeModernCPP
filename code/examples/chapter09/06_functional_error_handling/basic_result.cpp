// Basic Functional Error Handling
// Demonstrates using std::expected and std::optional for error handling

#include <iostream>
#include <expected>
#include <optional>
#include <string>

// Error types
enum class ErrorCode {
    FileNotFound,
    InvalidFormat,
    ChecksumMismatch,
    OutOfMemory,
};

struct Error {
    ErrorCode code;
    std::string message;

    static Error make(ErrorCode c, std::string msg) {
        return Error{c, std::move(msg)};
    }
};

// Result type alias
template<typename T>
using Result = std::expected<T, Error>;

using VoidResult = std::expected<void, Error>;

// Example functions
Result<int> parse_int(const std::string& s) {
    try {
        size_t pos = 0;
        int value = std::stoi(s, &pos);
        if (pos != s.length()) {
            return Error::make(ErrorCode::InvalidFormat,
                             "trailing characters: " + s.substr(pos));
        }
        return value;
    } catch (const std::exception& e) {
        return Error::make(ErrorCode::InvalidFormat, e.what());
    }
}

Result<std::string> read_file(const std::string& path) {
    if (path.empty()) {
        return Error::make(ErrorCode::FileNotFound, "empty path");
    }
    return "file content: " + path;
}

VoidResult validate_data(const std::string& data) {
    if (data.empty()) {
        return Error::make(ErrorCode::InvalidFormat, "empty data");
    }
    return {};  // Success
}

void demo_basic_usage() {
    std::cout << "=== Basic Result Usage ===" << std::endl;

    // Success case
    auto r1 = parse_int("42");
    if (r1) {
        std::cout << "Parsed: " << r1.value() << std::endl;
    } else {
        std::cout << "Error: " << r1.error().message << std::endl;
    }

    // Error case
    auto r2 = parse_int("abc");
    if (!r2) {
        std::cout << "Parse failed: " << r2.error().message << std::endl;
    }

    // Value-or-default
    auto r3 = parse_int("invalid");
    int value = r3.value_or(-1);
    std::cout << "Value or default: " << value << std::endl;
}

void demo_void_result() {
    std::cout << "\n=== Void Result ===" << std::endl;

    auto valid = validate_data("some data");
    if (valid) {
        std::cout << "Validation passed" << std::endl;
    }

    auto invalid = validate_data("");
    if (!invalid) {
        std::cout << "Validation failed: " << invalid.error().message << std::endl;
    }
}

void demo_error_propagation() {
    std::cout << "\n=== Error Propagation ===" << std::endl;

    auto process = [](const std::string& path) -> Result<std::string> {
        // Manual error propagation
        auto content_result = read_file(path);
        if (!content_result) {
            return content_result.error();
        }

        auto validation_result = validate_data(content_result.value());
        if (!validation_result) {
            return validation_result.error();
        }

        return content_result.value();
    };

    auto result = process("test.txt");
    if (result) {
        std::cout << "Success: " << result.value() << std::endl;
    } else {
        std::cout << "Failed: " << result.error().message << std::endl;
    }
}

// TRY macro simulation (GCC/Clang statement expression)
#define TRY(...) ({ \
    auto _result = (__VA_ARGS__); \
    if (!_result) return _result.error(); \
    _result.value(); \
})

void demo_try_macro() {
    std::cout << "\n=== TRY Macro ===" << std::endl;

    auto process_clean = [](const std::string& path) -> Result<std::string> {
        auto content = TRY(read_file(path));
        TRY(validate_data(content));
        return content;
    };

    auto result = process_clean("config.txt");
    if (result) {
        std::cout << "Success: " << result.value() << std::endl;
    } else {
        std::cout << "Failed: " << result.error().message << std::endl;
    }
}

int main() {
    demo_basic_usage();
    demo_void_result();
    demo_error_propagation();
    demo_try_macro();

    return 0;
}
