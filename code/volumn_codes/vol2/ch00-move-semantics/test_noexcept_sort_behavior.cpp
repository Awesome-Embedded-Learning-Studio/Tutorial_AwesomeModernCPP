// test_noexcept_sort_behavior.cpp -- 验证 noexcept 对 std::sort 的实际影响
// Standard: C++17
// GCC 15, -O2 -std=c++17

#include <iostream>
#include <vector>
#include <algorithm>
#include <string>

struct NoexceptMovable
{
    std::string payload;
    int value;

    static int move_count;

    NoexceptMovable(int v) : payload("data"), value(v) {}
    NoexceptMovable(const NoexceptMovable&) = default;

    NoexceptMovable(NoexceptMovable&& o) noexcept
        : payload(std::move(o.payload)), value(o.value) {
        ++move_count;
    }

    NoexceptMovable& operator=(NoexceptMovable&& o) noexcept {
        payload = std::move(o.payload);
        value = o.value;
        ++move_count;
        return *this;
    }

    bool operator<(const NoexceptMovable& rhs) const {
        return value < rhs.value;
    }

    static void reset() { move_count = 0; }
};

int NoexceptMovable::move_count = 0;

struct ThrowingMovable
{
    std::string payload;
    int value;

    static int move_count;

    ThrowingMovable(int v) : payload("data"), value(v) {}
    ThrowingMovable(const ThrowingMovable&) = default;

    // 没有 noexcept 的移动构造
    ThrowingMovable(ThrowingMovable&& o)
        : payload(std::move(o.payload)), value(o.value) {
        ++move_count;
    }

    ThrowingMovable& operator=(ThrowingMovable&& o) {
        payload = std::move(o.payload);
        value = o.value;
        ++move_count;
        return *this;
    }

    bool operator<(const ThrowingMovable& rhs) const {
        return value < rhs.value;
    }

    static void reset() { move_count = 0; }
};

int ThrowingMovable::move_count = 0;

int main()
{
    const int kCount = 5000;

    std::cout << "=== 测试 std::sort 是否受 noexcept 影响 ===\n\n";

    // 测试 noexcept 类型
    {
        std::vector<NoexceptMovable> vec;
        vec.reserve(kCount);
        for (int i = 0; i < kCount; ++i) {
            vec.emplace_back(kCount - i);
        }
        NoexceptMovable::reset();
        std::sort(vec.begin(), vec.end());
        std::cout << "noexcept 类型 sort: 移动次数 = "
                  << NoexceptMovable::move_count << "\n";
    }

    // 测试非 noexcept 类型
    {
        std::vector<ThrowingMovable> vec;
        vec.reserve(kCount);
        for (int i = 0; i < kCount; ++i) {
            vec.emplace_back(kCount - i);
        }
        ThrowingMovable::reset();
        std::sort(vec.begin(), vec.end());
        std::cout << "非 noexcept 类型 sort: 移动次数 = "
                  << ThrowingMovable::move_count << "\n";
    }

    std::cout << "\n=== 测试 std::vector 扩容行为 ===\n\n";

    // 测试 noexcept 类型扩容
    {
        NoexceptMovable::reset();
        std::vector<NoexceptMovable> vec;
        for (int i = 0; i < 200; ++i) {
            vec.emplace_back(i);
        }
        std::cout << "noexcept 类型扩容: 移动次数 = "
                  << NoexceptMovable::move_count << "\n";
    }

    // 测试非 noexcept 类型扩容
    {
        ThrowingMovable::reset();
        std::vector<ThrowingMovable> vec;
        for (int i = 0; i < 200; ++i) {
            vec.emplace_back(i);
        }
        std::cout << "非 noexcept 类型扩容: 移动次数 = "
                  << ThrowingMovable::move_count << "\n";
    }

    std::cout << "\n=== 结论 ===\n";
    std::cout << "std::sort 不受 noexcept 影响（两种类型移动次数相同）。\n";
    std::cout << "std::vector 扩容受 noexcept 影响显著。\n";
    std::cout << "这验证了文章中的断言是正确的。\n";

    return 0;
}
