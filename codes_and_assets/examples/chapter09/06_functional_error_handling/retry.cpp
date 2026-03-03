// Retry Logic with Functional Error Handling
// Demonstrates exponential backoff and retry patterns

#include <iostream>
#include <expected>
#include <string>
#include <thread>
#include <chrono>
#include <random>

struct Error {
    enum Code { Timeout, ServerError, RateLimit, Unknown };
    Code code;
    std::string message;

    static Error timeout() {
        return {Timeout, "Request timeout"};
    }

    static Error server_error(int code) {
        return {ServerError, "Server error: " + std::to_string(code)};
    }

    static Error rate_limited() {
        return {RateLimit, "Rate limited"};
    }
};

template<typename T>
using Result = std::expected<T, Error>;

// Simulated HTTP request
Result<std::string> fetch_http(const std::string& url) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 3);

    int outcome = dis(gen);

    switch (outcome) {
        case 0:
            std::cout << "  Request succeeded" << std::endl;
            return "Response body from " + url;
        case 1:
            std::cout << "  Request timed out" << std::endl;
            return Error::timeout();
        case 2:
            std::cout << "  Server error 503" << std::endl;
            return Error::server_error(503);
        case 3:
            std::cout << "  Rate limited" << std::endl;
            return Error::rate_limited();
    }
    return Error{Error::Unknown, "Unknown error"};
}

// Retry with exponential backoff
template<typename F, typename Rep, typename Period>
auto retry_with_backoff(F&& func,
                       unsigned max_attempts,
                       std::chrono::duration<Rep, Period> initial_delay)
    -> decltype(func())
{
    using ResultType = decltype(func());

    auto delay = initial_delay;

    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
        auto result = func();

        if (result) {
            return result;
        }

        // Last attempt failed, return error
        if (attempt == max_attempts - 1) {
            std::cout << "  All " << max_attempts << " attempts failed" << std::endl;
            return result;
        }

        // Don't retry on rate limit (would need longer delay)
        if (result.error().code == Error::RateLimit) {
            std::cout << "  Rate limited - not retrying" << std::endl;
            return result;
        }

        std::cout << "  Retry in "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(delay).count()
                  << " ms..." << std::endl;

        std::this_thread::sleep_for(delay);
        delay *= 2;  // Exponential backoff
    }

    return ResultType();
}

// Retry only on specific errors
template<typename F>
auto retry_on_transient(F&& func, unsigned max_attempts) -> decltype(func()) {
    using ResultType = decltype(func());
    auto delay = std::chrono::milliseconds(100);

    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
        auto result = func();

        if (result) {
            return result;
        }

        // Only retry on timeout
        if (result.error().code == Error::Timeout) {
            if (attempt < max_attempts - 1) {
                std::this_thread::sleep_for(delay);
                delay *= 2;
                continue;
            }
        }

        return result;
    }

    return ResultType();
}

void demo_basic_retry() {
    std::cout << "=== Basic Retry Demo ===" << std::endl;

    auto result = retry_with_backoff(
        []() { return fetch_http("http://example.com/api"); },
        5,
        std::chrono::milliseconds(100)
    );

    if (result) {
        std::cout << "Success: " << result.value() << std::endl;
    } else {
        std::cout << "Failed: " << result.error().message << std::endl;
    }
}

void demo_selective_retry() {
    std::cout << "\n=== Selective Retry Demo ===" << std::endl;

    auto result = retry_on_transient(
        []() { return fetch_http("http://example.com/api"); },
        3
    );

    if (result) {
        std::cout << "Success: " << result.value() << std::endl;
    } else {
        std::cout << "Failed: " << result.error().message << std::endl;
    }
}

// Retry with callback for each attempt
template<typename F, typename LogCallback>
auto retry_with_log(F&& func, unsigned max_attempts, LogCallback&& log_cb)
    -> decltype(func())
{
    using ResultType = decltype(func());

    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
        log_cb(attempt);

        auto result = func();
        if (result) {
            log_cb(-1);  // Success signal
            return result;
        }
    }

    return ResultType();
}

void demo_retry_with_logging() {
    std::cout << "\n=== Retry with Logging ===" << std::endl;

    auto result = retry_with_log(
        []() { return fetch_http("http://example.com/data"); },
        5,
        [](int attempt) {
            if (attempt >= 0) {
                std::cout << "  Attempt " << (attempt + 1) << std::endl;
            } else {
                std::cout << "  Success on retry!" << std::endl;
            }
        }
    );

    if (result) {
        std::cout << "Got: " << result.value() << std::endl;
    }
}

int main() {
    demo_basic_retry();
    demo_selective_retry();
    demo_retry_with_logging();

    std::cout << "\n=== Key Points ===" << std::endl;
    std::cout << "- Functional error handling enables clean retry logic" << std::endl;
    std::cout << "- Can selectively retry based on error type" << std::endl;
    std::cout << "- Exponential backoff prevents overwhelming servers" << std::endl;
    std::cout << "- Composable with other functional patterns" << std::endl;

    return 0;
}
