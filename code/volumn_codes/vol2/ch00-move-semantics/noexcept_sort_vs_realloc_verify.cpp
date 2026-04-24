// noexcept_sort_vs_realloc_verify.cpp
// 验证: noexcept 对 std::sort vs std::vector 扩容的影响
// 原文声称 noexcept 移动对排序性能有显著提升，实际验证 noexcept 主要影响 vector 扩容。
// Standard: C++17
// g++ -std=c++17 -O2 -Wall -Wextra -o noexcept_sort_vs_realloc_verify noexcept_sort_vs_realloc_verify.cpp

#include <iostream>
#include <vector>
#include <algorithm>
#include <string>
#include <type_traits>

/// @brief noexcept 移动的类型，用于对比测试
struct NoexceptType
{
    std::string payload;
    int value;

    static int copy_count;
    static int move_count;

    NoexceptType(int v) : payload("data"), value(v) {}
    NoexceptType(const NoexceptType& o)
        : payload(o.payload + "_c"), value(o.value) { ++copy_count; }
    NoexceptType(NoexceptType&& o) noexcept
        : payload(std::move(o.payload)), value(o.value)
    {
        o.payload = "(moved)";
        ++move_count;
    }
    NoexceptType& operator=(const NoexceptType& o)
    {
        payload = o.payload + "_c";
        value = o.value;
        ++copy_count;
        return *this;
    }
    NoexceptType& operator=(NoexceptType&& o) noexcept
    {
        payload = std::move(o.payload);
        value = o.value;
        o.payload = "(moved)";
        ++move_count;
        return *this;
    }
    bool operator<(const NoexceptType& rhs) const { return value < rhs.value; }
    static void reset() { copy_count = 0; move_count = 0; }
};

int NoexceptType::copy_count = 0;
int NoexceptType::move_count = 0;

/// @brief 非 noexcept 移动的类型，用于对比测试
struct ThrowingType
{
    std::string payload;
    int value;

    static int copy_count;
    static int move_count;

    ThrowingType(int v) : payload("data"), value(v) {}
    ThrowingType(const ThrowingType& o)
        : payload(o.payload + "_c"), value(o.value) { ++copy_count; }
    ThrowingType(ThrowingType&& o)
        : payload(std::move(o.payload)), value(o.value)
    {
        o.payload = "(moved)";
        ++move_count;
    }
    ThrowingType& operator=(const ThrowingType& o)
    {
        payload = o.payload + "_c";
        value = o.value;
        ++copy_count;
        return *this;
    }
    ThrowingType& operator=(ThrowingType&& o)
    {
        payload = std::move(o.payload);
        value = o.value;
        o.payload = "(moved)";
        ++move_count;
        return *this;
    }
    bool operator<(const ThrowingType& rhs) const { return value < rhs.value; }
    static void reset() { copy_count = 0; move_count = 0; }
};

int ThrowingType::copy_count = 0;
int ThrowingType::move_count = 0;

int main()
{
    const int kCount = 5000;

    std::cout << "=== Test 1: std::sort ===\n\n";

    // sort with noexcept move
    {
        std::vector<NoexceptType> vec;
        vec.reserve(kCount);
        for (int i = 0; i < kCount; ++i) vec.emplace_back(kCount - i);
        NoexceptType::reset();
        std::sort(vec.begin(), vec.end());
        std::cout << "noexcept 移动 sort:  拷贝=" << NoexceptType::copy_count
                  << " 移动=" << NoexceptType::move_count << "\n";
    }

    // sort with throwing move
    {
        std::vector<ThrowingType> vec;
        vec.reserve(kCount);
        for (int i = 0; i < kCount; ++i) vec.emplace_back(kCount - i);
        ThrowingType::reset();
        std::sort(vec.begin(), vec.end());
        std::cout << "非noexcept移动sort: 拷贝=" << ThrowingType::copy_count
                  << " 移动=" << ThrowingType::move_count << "\n";
    }

    std::cout << "\n=== Test 2: std::vector 扩容 ===\n\n";

    // vector realloc with noexcept move -> uses move
    {
        NoexceptType::reset();
        std::vector<NoexceptType> vec;
        for (int i = 0; i < 200; ++i) vec.emplace_back(i);
        std::cout << "noexcept 移动扩容:  拷贝=" << NoexceptType::copy_count
                  << " 移动=" << NoexceptType::move_count << "\n";
    }

    // vector realloc with throwing move -> falls back to copy (move_if_noexcept)
    {
        ThrowingType::reset();
        std::vector<ThrowingType> vec;
        for (int i = 0; i < 200; ++i) vec.emplace_back(i);
        std::cout << "非noexcept移动扩容: 拷贝=" << ThrowingType::copy_count
                  << " 移动=" << ThrowingType::move_count << "\n";
    }

    std::cout << "\n=== 结论 ===\n";
    std::cout << "std::sort 不区分 noexcept，两种情况都使用移动。\n";
    std::cout << "std::vector 扩容受 noexcept 影响：\n";
    std::cout << "  noexcept -> 扩容使用移动（move_if_noexcept 选中移动）\n";
    std::cout << "  非noexcept -> 扩容退回拷贝（move_if_noexcept 选中拷贝）\n";

    return 0;
}
