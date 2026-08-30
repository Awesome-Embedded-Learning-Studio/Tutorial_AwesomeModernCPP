// 例程:RawBuffer 一整个来回 —— 申请、写入、搬迁、转移产权、归还。
// 细验收在 ../tests/,这里只求"读得懂、跑得通"。
#include <cstdio>
#include <utility>

#include "tamcpp_ministl/memory_helper.hpp"
#include "tamcpp_ministl/raw_buffer.hpp"

int main() {
    using tamcpp::ministl::RawBuffer;

    RawBuffer<int> small(8);
    for (int i = 0; i < 8; ++i)
        small[i] = i * i;

    RawBuffer<int> big(16);
    tamcpp::ministl::helper::Relocate(small.data(), small.data() + 8, big.data());
    // ↑ int 平凡可拷贝:整块 memcpy 的快路
    std::printf("relocated[5] = %d\n", big[5]);

    // 空数组第一次扩容:两个空指针,规格说这合法。
    // nullptr 没类型、推导不出 Sources,所以显式给档位
    RawBuffer<int> fresh(4);
    tamcpp::ministl::helper::Relocate<int>(nullptr, nullptr, fresh.data());

    // 产权转移:移动构造 + 移动赋值各走一遍;旧块归还,探测器在看
    RawBuffer<int> stolen = std::move(big);
    big = std::move(stolen);

    std::puts("SMOKE GREEN");
    return 0;
}
