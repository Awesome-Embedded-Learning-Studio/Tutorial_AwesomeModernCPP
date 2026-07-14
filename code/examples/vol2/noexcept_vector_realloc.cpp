// noexcept_vector_realloc.cpp -- noexcept 移动 vs 非 noexcept 移动 在 vector 扩容时的差异
// Standard: C++17

#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// 用模板参数 NoexceptMove 切换移动构造是否标 noexcept，其余代码完全相同
template <bool NoexceptMove> class TrackedBuffer {
    char* data_;
    std::size_t capacity_;
    std::string tag_;

  public:
    explicit TrackedBuffer(std::size_t cap, std::string tag)
        : data_(new char[cap]), capacity_(cap), tag_(std::move(tag)) {}

    ~TrackedBuffer() { delete[] data_; }

    TrackedBuffer(const TrackedBuffer& other)
        : data_(new char[other.capacity_]), capacity_(other.capacity_), tag_(other.tag_) {
        std::cout << "  [" << tag_ << "] 拷贝构造\n";
    }

    // 唯一的区别：noexcept 标记
    TrackedBuffer(TrackedBuffer&& other) noexcept(NoexceptMove)
        : data_(other.data_), capacity_(other.capacity_), tag_(std::move(other.tag_)) {
        other.data_ = nullptr;
        other.capacity_ = 0;
        std::cout << "  [" << tag_ << "] 移动构造\n";
    }

    TrackedBuffer& operator=(const TrackedBuffer&) = delete;
    TrackedBuffer& operator=(TrackedBuffer&&) = delete;
};

int main() {
    using NB = TrackedBuffer<true>;  // 移动构造标了 noexcept
    using TB = TrackedBuffer<false>; // 移动构造没标 noexcept

    static_assert(std::is_nothrow_move_constructible_v<NB>, "NB 的移动构造是 noexcept");
    static_assert(!std::is_nothrow_move_constructible_v<TB>, "TB 的移动构造不是 noexcept");

    std::cout << "=== noexcept 移动 + vector 扩容 ===\n";
    {
        std::vector<NB> v;
        v.reserve(1);                     // 先预留 1 个槽位
        v.emplace_back(64, "Noexcept版"); // 占住唯一的槽位
        std::cout << "--- 触发扩容 ---\n";
        v.emplace_back(64, "Noexcept版"); // 超出容量，必须扩容搬运
    }

    std::cout << "\n=== 非 noexcept 移动 + vector 扩容 ===\n";
    {
        std::vector<TB> v;
        v.reserve(1);
        v.emplace_back(64, "Throwing版");
        std::cout << "--- 触发扩容 ---\n";
        v.emplace_back(64, "Throwing版"); // 扩容时 vector 不敢用移动，退回拷贝
    }

    return 0;
}
