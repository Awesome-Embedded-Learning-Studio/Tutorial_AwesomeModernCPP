// bump_allocator_demo.cpp - 线性分配器演示
#include "bump_allocator.h"
#include <iostream>
#include <iomanip>

// 用于测试的结构
struct TestData {
    int id;
    double value;
    char name[16];

    TestData(int i, double v, const char* n) : id(i), value(v) {
        std::strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }

    void display() const {
        std::cout << "  Data[" << id << "]: " << name
                  << " = " << value << '\n';
    }
};

int main() {
    std::cout << "=== Bump Allocator Demo ===\n\n";

    // 创建缓冲区
    constexpr std::size_t BUFFER_SIZE = 1024;
    static char buffer[BUFFER_SIZE];

    BumpAllocator allocator(buffer, BUFFER_SIZE);

    std::cout << "Initial state:\n";
    std::cout << "  Capacity: " << allocator.capacity() << " bytes\n";
    std::cout << "  Used: " << allocator.used() << " bytes\n";
    std::cout << "  Available: " << allocator.available() << " bytes\n\n";

    // 分配一些对象
    std::cout << "=== Allocating objects ===\n";
    TestData* d1 = static_cast<TestData*>(allocator.allocate(sizeof(TestData)));
    if (d1) {
        new (d1) TestData(1, 3.14, "Pi");
        d1->display();
    }

    TestData* d2 = static_cast<TestData*>(allocator.allocate(sizeof(TestData)));
    if (d2) {
        new (d2) TestData(2, 2.71, "Euler");
        d2->display();
    }

    // 分配一个大块
    std::cout << "\nAllocating large block...\n";
    void* large_block = allocator.allocate(512);
    if (large_block) {
        std::cout << "  Allocated 512 bytes at " << large_block << '\n';
    }

    std::cout << "\nState after allocations:\n";
    std::cout << "  Used: " << allocator.used() << " bytes\n";
    std::cout << "  Available: " << allocator.available() << " bytes\n\n";

    // 尝试分配超过剩余空间的大小
    std::cout << "=== Attempting over-allocation ===\n";
    void* fail = allocator.allocate(allocator.available() + 100);
    if (!fail) {
        std::cout << "  Correctly failed to allocate (buffer exhausted)\n";
    }

    // 重置并重新使用
    std::cout << "\n=== Resetting allocator ===\n";
    allocator.reset();

    std::cout << "After reset:\n";
    std::cout << "  Used: " << allocator.used() << " bytes\n";
    std::cout << "  Available: " << allocator.available() << " bytes\n\n";

    // 可以重新分配
    std::cout << "=== Allocating after reset ===\n";
    TestData* d3 = static_cast<TestData*>(allocator.allocate(sizeof(TestData)));
    if (d3) {
        new (d3) TestData(3, 1.41, "Sqrt(2)");
        d3->display();
    }

    std::cout << "\n=== Key characteristics ===\n";
    std::cout << "- O(1) allocation\n";
    std::cout << "- No individual deallocation\n";
    std::cout << "- Reset clears all at once\n";
    std::cout << "- Perfect for startup/phase allocation\n";
    std::cout << "- Zero fragmentation\n";

    return 0;
}
