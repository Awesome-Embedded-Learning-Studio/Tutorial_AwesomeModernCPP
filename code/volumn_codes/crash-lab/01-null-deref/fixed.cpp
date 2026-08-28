// 01 - Null Pointer Dereference 修复方案
//
// 核心原则：解引用前必须检查指针有效性。
// 现代 C++ 使用 optional 表达"可能没有值"的语义。

#include <cstdio>
#include <iostream>
#include <optional>

// ============================================================
// 方案一：调用方判空（C++11+，最基本）
// ============================================================
int* find_value_raw(int* arr, int size, int target) {
    for (int i = 0; i < size; i++) {
        if (arr[i] == target)
            return &arr[i];
    }
    return nullptr;
}

void use_raw_pointer() {
    int data[] = {10, 20, 30, 40, 50};
    int* result = find_value_raw(data, 5, 999);

    // 使用前检查
    if (result != nullptr) {
        printf("[raw ptr] found: %d\n", *result);
    } else {
        printf("[raw ptr] not found, safely handled\n");
    }
}

// ============================================================
// 方案二：std::optional（推荐，C++17+）
// ============================================================
// 用 optional<int> 代替 int*，语义更清晰：
// 明确表达"可能返回值，也可能没有"
std::optional<int> find_value_optional(const int* arr, int size, int target) {
    for (int i = 0; i < size; i++) {
        if (arr[i] == target)
            return arr[i]; // 找到，返回值
    }
    return std::nullopt; // 没找到，返回空 optional
}

void use_optional() {
    int data[] = {10, 20, 30, 40, 50};
    auto result = find_value_optional(data, 5, 999);

    if (result.has_value()) {
        printf("[optional] found: %d\n", result.value());
    } else {
        printf("[optional] not found, safely handled\n");
    }

    // 或者使用 value_or 提供默认值
    int val = find_value_optional(data, 5, 30).value_or(-1);
    printf("[optional] value_or: %d\n", val);
}

int main() {
    printf("=== 01 Null Deref Fix ===\n");
    use_raw_pointer();
    use_optional();
    return 0;
}
