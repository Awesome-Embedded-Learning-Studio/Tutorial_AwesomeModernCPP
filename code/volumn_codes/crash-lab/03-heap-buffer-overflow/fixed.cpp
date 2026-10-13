// 03 - Heap Buffer Overflow 修复方案
//
// 核心原则：使用带边界检查的容器，杜绝手动索引越界。

#include <cstdio>
#include <stdexcept>
#include <vector>

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);

    // ============================================================
    // 方案一：vector + at()（推荐，带边界检查）
    // ============================================================
    std::vector<int> arr = {0, 10, 20, 30, 40};
    printf("Vector size = %zu\n", arr.size());

    // at() 会做边界检查，越界时抛 std::out_of_range
    try {
        for (int i = 0; i < 10; i++) {
            arr.at(i) = 0xDEAD; // i >= 5 时抛异常
            printf("  arr[%d] = 0xDEAD\n", i);
        }
    } catch (const std::out_of_range& e) {
        printf("Caught: %s\n", e.what());
        printf("Safe: out-of-bounds access prevented!\n");
    }

    // ============================================================
    // 方案二：vector + size() 检查
    // ============================================================
    printf("\nUsing size() check:\n");
    for (size_t i = 0; i < 10; i++) {
        if (i < arr.size()) {
            arr[i] = static_cast<int>(i);
            printf("  arr[%zu] = %d\n", i, arr[i]);
        } else {
            printf("  arr[%zu] skipped (out of bounds)\n", i);
        }
    }

    // ============================================================
    // 方案三：范围 for 循环（C++11+，避免手动索引）
    // ============================================================
    printf("\nUsing range-for:\n");
    int idx = 0;
    for (auto& val : arr) {
        val = idx++;
        printf("  val = %d\n", val);
    }

    printf("\nNo crash, no corruption. Vector auto-manages memory.\n");
    return 0;
}
