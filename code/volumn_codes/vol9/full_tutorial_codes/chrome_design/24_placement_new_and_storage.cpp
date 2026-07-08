// placement new + 对齐存储:NoDestructor 实现的核心机制
// 来源:NoDestructor 前置知识(一)+ 实战(二)
// 编译:g++ -std=c++20 -Wall -Wextra -I. 24_placement_new_and_storage.cpp -o
// 24_placement_new_and_storage

#include <cstdint>
#include <iostream>
#include <new>

// 手写最小版,展示 alignas + placement new + reinterpret_cast 三件套
template <typename T> class MiniNoDestructor {
  public:
    template <typename... Args> explicit MiniNoDestructor(Args&&... args) {
        new (storage_) T(std::forward<Args>(args)...); // placement new(不分配)
    }
    ~MiniNoDestructor() = default; // 只析构 char 成员,不调 ~T()
    T* get() { return reinterpret_cast<T*>(storage_); }

  private:
    alignas(T) char storage_[sizeof(T)]; // 对齐到 T 的缓冲
};

int main() {
    std::cout << "=== 对齐验证 ===\n";
    std::cout << "  alignof(int) = " << alignof(int) << "\n";
    std::cout << "  alignof(double) = " << alignof(double) << "\n";
    std::cout << "  alignof(MiniNoDestructor<int>) = " << alignof(MiniNoDestructor<int>) << "\n";

    std::cout << "\n=== placement new 构造 + reinterpret_cast 访问 ===\n";
    MiniNoDestructor<int> nd(42);
    int* p = nd.get();
    std::cout << "  *nd.get() = " << *p << "\n";
    std::cout << "  p 指向的地址 == &nd 的 storage_ 吗:"
              << (p == reinterpret_cast<int*>(&nd) ? "是(同一块内存)" : "否") << "\n";

    std::cout << "\n=== sizeof:NoDestructor 零额外开销 ===\n";
    std::cout << "  sizeof(MiniNoDestructor<int>) = " << sizeof(MiniNoDestructor<int>)
              << " (== sizeof(int) = " << sizeof(int) << ")\n";
    std::cout << "  sizeof(MiniNoDestructor<double>) = " << sizeof(MiniNoDestructor<double>)
              << " (== sizeof(double) = " << sizeof(double) << ")\n";

    std::cout << "\n=== 手动析构 vs NoDestructor 的区别 ===\n";
    std::cout << "  placement new 的对象通常要手动 ~T()(内置类型用 typedef:p->~I())\n";
    std::cout << "  但 NoDestructor 故意不调 ~T()——靠 =default 析构只看到 char 成员\n";
    return 0;
}
