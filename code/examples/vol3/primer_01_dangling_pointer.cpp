// primer_01_dangling_pointer.cpp
// 扩容后旧指针悬空：先看两个地址分道扬镳，再看 UB 是死是活

#include <cstdio>
#include <vector>

int main() {
    std::vector<int> v;
    v.reserve(2);
    v.push_back(41);
    v.push_back(1);

    int* p = &v[0]; // 缓存一个指针
    std::printf("before: p = %p, v.data() = %p\n", static_cast<void*>(p),
                static_cast<void*>(v.data()));

    v.push_back(99); // 容量已满，触发扩容（缓冲整体搬迁）
    std::printf("after : p = %p, v.data() = %p\n", static_cast<void*>(p),
                static_cast<void*>(v.data()));

    *p = 100; // 往旧地址写：未定义行为
    std::printf("*p = %d (this write went to the OLD buffer)\n", *p);
    return 0;
}
