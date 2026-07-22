// noexcept_sort_vs_realloc.cpp -- 验证 noexcept 对 std::sort 和 vector 扩容的影响
// Standard: C++17
// 对应文档：vol2-modern-features/ch00-move-semantics/05-move-in-practice.md
//
// 核心结论：
//   - std::sort 只用移动，不区分移动操作是否 noexcept（两种类型都是 拷贝=0）
//   - vector 扩容通过 move_if_noexcept 选择策略：noexcept 类型用移动，
//     非 noexcept 类型退回拷贝（强异常安全）
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

// 移动操作带 noexcept 的类型
struct NoexceptType {
    std::string payload;
    int value;

    static int copy_count;
    static int move_count;

    NoexceptType(int v) : payload("data"), value(v) {}
    NoexceptType(const NoexceptType& o) : payload(o.payload + "_c"), value(o.value) {
        ++copy_count;
    }
    NoexceptType(NoexceptType&& o) noexcept : payload(std::move(o.payload)), value(o.value) {
        o.payload = "(moved)";
        ++move_count;
    }
    NoexceptType& operator=(NoexceptType&& o) noexcept {
        payload = std::move(o.payload);
        value = o.value;
        o.payload = "(moved)";
        ++move_count;
        return *this;
    }
    NoexceptType& operator=(const NoexceptType& o) {
        payload = o.payload + "_c";
        value = o.value;
        ++copy_count;
        return *this;
    }
    bool operator<(const NoexceptType& rhs) const { return value < rhs.value; }
    static void reset() {
        copy_count = 0;
        move_count = 0;
    }
};

// ThrowingType 与 NoexceptType 完全相同，唯一区别是移动操作没有 noexcept
struct ThrowingType {
    std::string payload;
    int value;

    static int copy_count;
    static int move_count;

    ThrowingType(int v) : payload("data"), value(v) {}
    ThrowingType(const ThrowingType& o) : payload(o.payload + "_c"), value(o.value) {
        ++copy_count;
    }
    ThrowingType(ThrowingType&& o) // 注意：没有 noexcept
        : payload(std::move(o.payload)), value(o.value) {
        o.payload = "(moved)";
        ++move_count;
    }
    ThrowingType& operator=(ThrowingType&& o) // 注意：没有 noexcept
    {
        payload = std::move(o.payload);
        value = o.value;
        o.payload = "(moved)";
        ++move_count;
        return *this;
    }
    ThrowingType& operator=(const ThrowingType& o) {
        payload = o.payload + "_c";
        value = o.value;
        ++copy_count;
        return *this;
    }
    bool operator<(const ThrowingType& rhs) const { return value < rhs.value; }
    static void reset() {
        copy_count = 0;
        move_count = 0;
    }
};

int NoexceptType::copy_count = 0;
int NoexceptType::move_count = 0;
int ThrowingType::copy_count = 0;
int ThrowingType::move_count = 0;

int main() {
    const int kCount = 5000;

    // Test 1: std::sort（noexcept 类型）
    {
        std::vector<NoexceptType> vec;
        vec.reserve(kCount);
        for (int i = 0; i < kCount; ++i)
            vec.emplace_back(kCount - i);
        NoexceptType::reset();
        std::sort(vec.begin(), vec.end());
        std::cout << "noexcept sort:  拷贝=" << NoexceptType::copy_count
                  << " 移动=" << NoexceptType::move_count << "\n";
    }

    // Test 2: std::sort（非 noexcept 类型）
    {
        std::vector<ThrowingType> vec;
        vec.reserve(kCount);
        for (int i = 0; i < kCount; ++i)
            vec.emplace_back(kCount - i);
        ThrowingType::reset();
        std::sort(vec.begin(), vec.end());
        std::cout << "非noexcept sort: 拷贝=" << ThrowingType::copy_count
                  << " 移动=" << ThrowingType::move_count << "\n";
    }

    std::cout << "\n";

    // Test 3: vector 扩容（noexcept 类型，无 reserve）
    {
        NoexceptType::reset();
        std::vector<NoexceptType> vec;
        for (int i = 0; i < 200; ++i)
            vec.emplace_back(i);
        std::cout << "noexcept 扩容:  拷贝=" << NoexceptType::copy_count
                  << " 移动=" << NoexceptType::move_count << "\n";
    }

    // Test 4: vector 扩容（非 noexcept 类型，无 reserve）
    // ThrowingType 的扩容会退回拷贝，因为 move_if_noexcept 不选中它的移动
    {
        ThrowingType::reset();
        std::vector<ThrowingType> vec;
        for (int i = 0; i < 200; ++i)
            vec.emplace_back(i);
        std::cout << "非noexcept扩容: 拷贝=" << ThrowingType::copy_count
                  << " 移动=" << ThrowingType::move_count << "\n";
    }
}
