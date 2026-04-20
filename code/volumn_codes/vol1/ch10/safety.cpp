// safety.cpp
// 演示异常安全与不安全代码的行为对比

#include <cstdio>
#include <memory>
#include <stdexcept>

void might_throw(bool should_fail) {
    if (should_fail) {
        throw std::runtime_error("Something went wrong!");
    }
    std::puts("  Operation succeeded.");
}

// ---- 不安全版本 ----
void unsafe_version() {
    std::puts("[Unsafe] Allocating resources...");
    int* data = new int[100];
    double* temp = new double[50];
    std::puts("[Unsafe] Resources allocated. Starting work...");

    might_throw(true);  // 故意触发异常

    delete[] temp;
    delete[] data;
    std::puts("[Unsafe] Resources released.");
}

// ---- 安全版本 ----
void safe_version() {
    std::puts("[Safe] Allocating resources...");
    auto data = std::make_unique<int[]>(100);
    auto temp = std::make_unique<double[]>(50);
    std::puts("[Safe] Resources allocated. Starting work...");

    might_throw(true);  // 同样触发异常

    std::puts("[Safe] Resources released.");
}

int main() {
    // 测试不安全版本
    std::puts("=== Testing unsafe version ===");
    try {
        unsafe_version();
    } catch (const std::exception& e) {
        std::printf("  Caught: %s\n", e.what());
    }
    std::puts("  Note: memory leaked! data and temp were never freed.\n");

    // 测试安全版本
    std::puts("=== Testing safe version ===");
    try {
        safe_version();
    } catch (const std::exception& e) {
        std::printf("  Caught: %s\n", e.what());
    }
    std::puts("  Note: no leak! unique_ptr destructors cleaned up.\n");

    return 0;
}
