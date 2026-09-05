// primer_01_vector_growth.cpp
// 观察 std::vector 的扩容：容量序列 + 缓冲地址变化（搬家现场）

#include <cstdio>
#include <vector>

int main() {
    std::vector<int> v;
    std::size_t last_cap = v.capacity();
    std::printf("born    : size=%zu capacity=%zu data=%p\n", v.size(), v.capacity(),
                static_cast<void*>(v.data()));

    for (int i = 0; i < 40; ++i) {
        v.push_back(i);
        if (v.capacity() != last_cap) {
            std::printf("push #%-2d: size=%zu capacity=%zu data=%p\n", i + 1, v.size(),
                        v.capacity(), static_cast<void*>(v.data()));
            last_cap = v.capacity();
        }
    }
    return 0;
}
