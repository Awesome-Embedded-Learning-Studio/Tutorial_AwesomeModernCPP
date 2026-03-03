// Monadic Combinators for Error Handling
// Demonstrates map, and_then, and other functional combinators

#include <iostream>
#include <expected>
#include <string>
#include <sstream>

struct Error {
    enum Code { ParseError, ValidationError, TransformError };
    Code code;
    std::string message;

    static Error make(Code c, std::string msg) {
        return Error{c, std::move(msg)};
    }
};

template<typename T>
using Result = std::expected<T, Error>;

// map combinator: transform success value
template<typename T, typename F>
auto map(Result<T> result, F&& func) {
    using ResultType = decltype(func(result.value()));
    if constexpr (std::is_void_v<ResultType>) {
        if (result) {
            func(result.value());
            return Result<void>{};
        }
        return Result<void>{std::unexpect, result.error()};
    } else {
        if (result) {
            return Result<ResultType>{func(result.value())};
        }
        return Result<ResultType>{std::unexpect, result.error()};
    }
}

// and_then combinator: chain operations that return Result
template<typename T, typename F>
auto and_then(Result<T> result, F&& func) -> decltype(func(result.value())) {
    if (result) {
        return func(result.value());
    }
    return std::unexpected(result.error());
}

// map_error: transform error
template<typename T, typename F>
Result<T> map_error(Result<T> result, F&& func) {
    if (!result) {
        return Result<T>{std::unexpect, func(result.error())};
    }
    return result;
}

// Demo functions
Result<int> parse_number(const std::string& s) {
    try {
        return Result<int>{std::stoi(s)};
    } catch (...) {
        return Result<int>{std::unexpect, Error::make(Error::ParseError, "Invalid number")};
    }
}

Result<bool> validate_positive(int x) {
    if (x > 0) {
        return true;
    }
    return Result<bool>{std::unexpect, Error::make(Error::ValidationError, "Not positive")};
}

Result<std::string> format_result(int x) {
    std::stringstream ss;
    ss << "Result: " << x;
    return ss.str();
}

void demo_map() {
    std::cout << "=== Map Combinator ===" << std::endl;

    auto result = parse_number("42");
    auto formatted = map(result, format_result);

    if (formatted) {
        std::cout << formatted.value() << std::endl;
    }

    // Chaining map
    auto chained = map(
        map(parse_number("21"), [](int x) { return x * 2; }),
        [](int x) { return x + 10; }
    );

    if (chained) {
        std::cout << "Chained result: " << chained.value() << std::endl;
    }
}

void demo_and_then() {
    std::cout << "\n=== And Then Combinator ===" << std::endl;

    // Chain operations that return Result
    auto result = and_then(
        parse_number("10"),
        validate_positive
    );

    if (result) {
        std::cout << "Valid positive number: " << result.value() << std::endl;
    }

    // Chain with failure
    auto failed = and_then(
        parse_number("-5"),
        validate_positive
    );

    if (!failed) {
        std::cout << "Validation failed: " << failed.error().message << std::endl;
    }

    // Multiple chains
    auto multi_chain = and_then(
        and_then(
            parse_number("5"),
            validate_positive
        ),
        [](bool) { return format_result(42); }
    );

    if (multi_chain) {
        std::cout << "Multi-chain: " << multi_chain.value() << std::endl;
    }
}

void demo_map_error() {
    std::cout << "\n=== Map Error Combinator ===" << std::endl;

    auto result = parse_number("invalid");

    auto enhanced = map_error(result, [](Error err) {
        err.message = "In " + err.message;
        return err;
    });

    if (!enhanced) {
        std::cout << "Enhanced error: " << enhanced.error().message << std::endl;
    }
}

// Composition helper
template<typename T1, typename T2, typename F1, typename F2>
auto compose(F1&& f1, F2&& f2) {
    return [f1, f2](auto&&... args) {
        auto r1 = f1(std::forward<decltype(args)>(args)...);
        if (!r1) {
            using R2 = decltype(f2(r1.value()));
            return R2{std::unexpect, r1.error()};
        }
        return f2(r1.value());
    };
}

void demo_composition() {
    std::cout << "\n=== Function Composition ===" << std::endl;

    auto process = compose(
        parse_number,
        [](int x) { return format_result(x * 2); }
    );

    auto result = process("21");
    if (result) {
        std::cout << "Composed: " << result.value() << std::endl;
    }
}

int main() {
    demo_map();
    demo_and_then();
    demo_map_error();
    demo_composition();

    return 0;
}
