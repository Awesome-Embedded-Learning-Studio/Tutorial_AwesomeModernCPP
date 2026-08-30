// 例程:noexcept 的真金白银 —— 数组的数组扩容,到底走移动还是拷贝。
// 数的是"元素级拷贝构造"的次数:移动畅通时它应当一次都不发生。
// 细验收在 ../tests/,这里只求"读得懂、跑得通"。
#include <cstdio>
#include <utility>

#include "tamcpp_ministl/vector.hpp"

namespace {
// 普查员同款,多带一本"拷贝构造专账"
struct Census {
    static inline int alive = 0;
    static inline int copies = 0;
    explicit Census(int x = 0) : v(x) { ++alive; }
    Census(const Census& o) : v(o.v) {
        ++alive;
        ++copies;
    }
    Census(Census&& o) noexcept : v(o.v) { ++alive; }
    ~Census() { --alive; }
    int v;
};
} // namespace

int main() {
    using tamcpp::ministl::Vector;

    // 20 个各装 5 个元素的 Vector 搬进外层,外层中途扩容好几轮
    Vector<Vector<Census>> outer;
    for (int i = 0; i < 20; ++i) {
        Vector<Census> inner;
        for (int j = 0; j < 5; ++j)
            inner.emplace_back(j);
        outer.push_back(std::move(inner));
    }

    std::printf("outer=%zu 个,元素活着 %d 个,元素级拷贝 %d 次\n", outer.size(), Census::alive,
                Census::copies);
    return 0;
}
