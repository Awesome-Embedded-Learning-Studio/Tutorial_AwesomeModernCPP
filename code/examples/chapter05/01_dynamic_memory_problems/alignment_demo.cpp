#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <cstring>

// 演示对齐和结构体填充

// 示例1：默认对齐
struct DefaultAlignment {
    char a;      // offset 0, size 1
                 // 3 bytes padding
    int b;       // offset 4, size 4
    char c;      // offset 8, size 1
                 // 3 bytes padding
};               // total: 12 bytes

// 示例2：优化后的布局
struct OptimizedAlignment {
    int b;       // offset 0, size 4
    char a;      // offset 4, size 1
    char c;      // offset 5, size 1
                 // 2 bytes padding
};               // total: 8 bytes

// 示例3：使用 #pragma pack (不推荐，除非必要)
#pragma pack(push, 1)
struct PackedStruct {
    char a;      // offset 0, size 1
    int b;       // offset 1, size 4
    char c;      // offset 5, size 1
};               // total: 6 bytes
#pragma pack(pop)

// 示例4：DMA对齐要求示例
struct __attribute__((aligned(32))) AlignedBuffer {
    uint8_t data[256];  // 32字节对齐，适合DMA
};

// 演示未对齐访问的问题
void misaligned_access_demo() {
    std::cout << "\n--- Misaligned Access Demo ---\n";

    // 创建一个故意未对齐的缓冲区
    char buffer[32] = {0};

    // 在偏移1处放置一个int（未对齐！）
    buffer[0] = 0xFF;
    uint32_t* misaligned = reinterpret_cast<uint32_t*>(&buffer[1]);

    std::cout << "Buffer address: " << static_cast<void*>(buffer) << "\n";
    std::cout << "Misaligned address: " << misaligned << "\n";
    std::cout << "Is aligned to 4 bytes? " << (reinterpret_cast<uintptr_t>(misaligned) % 4 == 0) << "\n";

    // 在x86上这可能会工作，但会慢；在某些架构上会崩溃
    // *misaligned = 0x12345678;  // 取消注释以测试
}

// 对齐辅助函数
template<typename T>
T* align_up(T* ptr, size_t alignment) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    return reinterpret_cast<T*>(aligned);
}

void custom_alignment_demo() {
    std::cout << "\n--- Custom Alignment Demo ---\n";

    // 原始缓冲区
    alignas(8) char buffer[128];

    std::cout << "Buffer address: " << static_cast<void*>(buffer) << "\n";

    // 手动对齐到32字节
    void* aligned_32 = align_up(buffer + 1, 32);  // +1 故意制造未对齐
    std::cout << "32-byte aligned address: " << aligned_32 << "\n";
    std::cout << "Is aligned? " << (reinterpret_cast<uintptr_t>(aligned_32) % 32 == 0) << "\n";

    // 对齐到64字节
    void* aligned_64 = align_up(buffer + 3, 64);
    std::cout << "64-byte aligned address: " << aligned_64 << "\n";
    std::cout << "Is aligned? " << (reinterpret_cast<uintptr_t>(aligned_64) % 64 == 0) << "\n";
}

void struct_size_demo() {
    std::cout << "\n--- Struct Size and Alignment ---\n";

    std::cout << "DefaultAlignment:\n";
    std::cout << "  sizeof: " << sizeof(DefaultAlignment) << " bytes\n";
    std::cout << "  alignof: " << alignof(DefaultAlignment) << " bytes\n";
    std::cout << "  offset of a: " << offsetof(DefaultAlignment, a) << "\n";
    std::cout << "  offset of b: " << offsetof(DefaultAlignment, b) << "\n";
    std::cout << "  offset of c: " << offsetof(DefaultAlignment, c) << "\n";

    std::cout << "\nOptimizedAlignment:\n";
    std::cout << "  sizeof: " << sizeof(OptimizedAlignment) << " bytes\n";
    std::cout << "  alignof: " << alignof(OptimizedAlignment) << " bytes\n";
    std::cout << "  offset of a: " << offsetof(OptimizedAlignment, a) << "\n";
    std::cout << "  offset of b: " << offsetof(OptimizedAlignment, b) << "\n";
    std::cout << "  offset of c: " << offsetof(OptimizedAlignment, c) << "\n";

    std::cout << "\nPackedStruct:\n";
    std::cout << "  sizeof: " << sizeof(PackedStruct) << " bytes\n";
    std::cout << "  alignof: " << alignof(PackedStruct) << " bytes\n";
    std::cout << "  offset of a: " << offsetof(PackedStruct, a) << "\n";
    std::cout << "  offset of b: " << offsetof(PackedStruct, b) << "\n";
    std::cout << "  offset of c: " << offsetof(PackedStruct, c) << "\n";

    std::cout << "\nMemory saved: " << sizeof(DefaultAlignment) - sizeof(OptimizedAlignment) << " bytes\n";
}

// 演示缓存行影响
struct __attribute__((aligned(64))) CacheLineAligned {
    int data;
};

void cache_line_demo() {
    std::cout << "\n--- Cache Line Alignment ---\n";

    CacheLineAligned a;
    CacheLineAligned b;

    std::cout << "Address of a: " << &a << "\n";
    std::cout << "Address of b: " << &b << "\n";
    std::cout << "Difference: " << (reinterpret_cast<char*>(&b) - reinterpret_cast<char*>(&a)) << " bytes\n";
    std::cout << "On different cache lines? " << ((reinterpret_cast<char*>(&b) - reinterpret_cast<char*>(&a)) >= 64) << "\n";
}

int main() {
    std::cout << "=== Alignment and Padding Demo ===\n";

    struct_size_demo();
    misaligned_access_demo();
    custom_alignment_demo();
    cache_line_demo();

    std::cout << "\n=== Key Takeaways ===\n";
    std::cout << "1. Arrange struct members by size (largest first) to minimize padding\n";
    std::cout << "2. Alignment affects DMA, cache performance, and atomic operations\n";
    std::cout << "3. Use alignas() for specific alignment requirements\n";
    std::cout << "4. Avoid #pragma pack unless necessary (performance impact)\n";

    return 0;
}
