// 例程:盯着 Vector 扩容 —— 容量什么时候变、变成多少。
// 细验收在 ../tests/,这里只求"读得懂、跑得通"。
#include <cstdio>

#include "tamcpp_ministl/vector.hpp"

int main() {
    using tamcpp::ministl::Vector;

    Vector<int> v;
    std::printf("初始         size=%-3zu capacity=%zu\n", v.size(), v.capacity());

    std::size_t last = v.capacity();
    for (int i = 0; i < 40; ++i) {
        v.push_back(i);
        if (v.capacity() != last) {
            std::printf("第 %-2d 个入库 size=%-3zu capacity=%zu(扩容)\n", i + 1, v.size(),
                        v.capacity());
            last = v.capacity();
        }
    }
    std::printf("最终         size=%-3zu capacity=%zu\n", v.size(), v.capacity());
    return 0;
}
