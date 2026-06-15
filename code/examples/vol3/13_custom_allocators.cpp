// Standard: C++20
// 自定义分配器：手写一个 bump（线性）分配器原型 + std::pmr 让容器在栈 buffer 里分配
#include <cstdint>
#include <iostream>
#include <memory_resource>
#include <vector>

// 最简 bump arena：指针线性前进，不回收单块，整块 reset（典型的帧/临时分配场景）
template <std::size_t N> class Arena {
    alignas(std::max_align_t) std::byte buf_[N];
    std::size_t offset_ = 0;

  public:
    void* alloc(std::size_t bytes, std::size_t align) {
        std::size_t space = N - offset_;
        void* ptr = buf_ + offset_;
        if (std::align(align, bytes, ptr, space)) {
            offset_ = static_cast<std::byte*>(ptr) - buf_ + bytes;
            return ptr;
        }
        return nullptr; // 满了
    }
    std::size_t used() const { return offset_; }
    static constexpr std::size_t capacity() { return N; }
};

int main() {
    std::cout << "== 手写 bump arena：指针线性前进，分配 O(1) ==\n";
    Arena<256> arena;
    void* p1 = arena.alloc(16, alignof(int));
    void* p2 = arena.alloc(32, alignof(double));
    void* p3 = arena.alloc(64, alignof(std::max_align_t));
    std::cout << "alloc 16/32/64 字节后，used = " << arena.used() << " / " << arena.capacity()
              << '\n';
    std::cout << "（连续地址：p1=" << p1 << " p2=" << p2 << " p3=" << p3 << "）\n";

    std::cout << "\n== std::pmr：让 vector 在栈上的一块 buffer 里分配，零堆分配 ==\n";
    std::byte stack_buf[1024];
    std::pmr::monotonic_buffer_resource mbr(stack_buf, sizeof(stack_buf));
    {
        std::pmr::vector<int> v(&mbr); // 这 vector 的 new/delete 走 mbr，即栈 buffer
        for (int i = 0; i < 200; ++i) {
            v.push_back(i);
        }
        std::cout << "pmr::vector 存了 " << v.size()
                  << " 个 int，分配全部落在栈上 1024 字节 buffer，没碰堆\n";
    }
    std::cout << "（monotonic_buffer_resource 就是 bump 思路：只进不退，整块释放）\n";
    return 0;
}
