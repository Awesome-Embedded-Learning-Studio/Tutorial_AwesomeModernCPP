// 验证高阶函数实现和正确性
#include <iostream>
#include <functional>
#include <algorithm>
#include <vector>

// 高阶函数：接受"操作"和"判断函数"作为参数
template<typename Operation, typename ShouldRetry>
auto with_retry(Operation&& op, ShouldRetry&& should_retry, int max_attempts)
    -> std::invoke_result_t<Operation>
{
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        try {
            auto result = op();
            return result;
        } catch (const std::exception& e) {
            if (attempt == max_attempts || !should_retry(attempt, e)) {
                throw;
            }
            std::cout << "Attempt " << attempt << " failed: " << e.what()
                      << ", retrying...\n";
        }
    }
    throw std::runtime_error("unreachable");
}

// 返回函数的函数
auto make_threshold_filter(int threshold) {
    return [threshold](const std::vector<int>& data) {
        std::vector<int> result;
        std::copy_if(data.begin(), data.end(), std::back_inserter(result),
                    [threshold](int x) { return x > threshold; });
        return result;
    };
}

int main() {
    // 测试 with_retry
    std::cout << "=== Testing with_retry ===\n";
    int call_count = 0;

    auto result = with_retry(
        [&call_count]() -> int {
            call_count++;
            if (call_count < 3) {
                throw std::runtime_error("connection timeout");
            }
            return 42;
        },
        [](int attempt, const std::exception& /* e */) {
            return attempt < 5;   // 最多重试 5 次
        },
        5
    );

    std::cout << "Result: " << result << "\n";   // Result: 42

    // 测试 make_threshold_filter
    std::cout << "\n=== Testing make_threshold_filter ===\n";
    auto filter_above_50 = make_threshold_filter(50);
    auto filter_above_80 = make_threshold_filter(80);

    std::vector<int> data = {12, 45, 67, 89, 23, 90};
    auto r1 = filter_above_50(data);
    auto r2 = filter_above_80(data);

    std::cout << "Above 50: ";
    for (int x : r1) std::cout << x << " ";
    std::cout << "\nAbove 80: ";
    for (int x : r2) std::cout << x << " ";
    std::cout << "\n";

    return 0;
}
