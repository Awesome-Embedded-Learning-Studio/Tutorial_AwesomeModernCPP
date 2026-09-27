#include <utility>

// ─── 判题辅助(请勿改动):Box 会数自己被拷贝、被移动了几次 ───

struct SwapStats {
    static inline int copies = 0;
    static inline int moves = 0;
};

class Box {
public:
    Box() = default;
    explicit Box(int v) : value_(v) {}

    Box(const Box& other) : value_(other.value_) { ++SwapStats::copies; }

    Box& operator=(const Box& other) {
        value_ = other.value_;
        ++SwapStats::copies;
        return *this;
    }

    Box(Box&& other) noexcept : value_(other.value_) {
        other.value_ = 0;
        ++SwapStats::moves;
    }

    Box& operator=(Box&& other) noexcept {
        value_ = other.value_;
        other.value_ = 0;
        ++SwapStats::moves;
        return *this;
    }

    int value() const { return value_; }

private:
    int value_ = 0;
};

struct SwapCheck {
    bool exchanged;
    int copies;
    int moves;
};

template <typename T>
void my_swap(T& a, T& b);

inline SwapCheck swap_check(int a, int b) {
    SwapStats::copies = 0;
    SwapStats::moves = 0;
    Box x(a);
    Box y(b);
    my_swap(x, y);
    return {x.value() == b && y.value() == a, SwapStats::copies, SwapStats::moves};
}

// ─── 您要实现的:用移动语义完成交换 ───

template <typename T>
void my_swap(T& a, T& b) {
    // 在这里写你的实现:值要换对,一次拷贝都不许发生
    // std::move 已经在 <utility> 里等着您
}
