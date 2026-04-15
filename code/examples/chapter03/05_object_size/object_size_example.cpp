// 对象大小和平凡类型示例

#include <iostream>
#include <cstdint>
#include <type_traits>

// ==================== 平凡类型 ====================
struct Trivial {
    int x;
    int y;
};

// 非平凡：有自定义构造函数
struct NonTrivial {
    int x;
    NonTrivial() : x(0) {}
};

// ==================== 平凡可复制 ====================
struct TriviallyCopyable {
    int x;
    double y;
};

// 非平凡可复制：有拷贝构造
struct NonTriviallyCopyable {
    int* p;
    NonTriviallyCopyable(int val) : p(new int(val)) {}
    NonTriviallyCopyable(const NonTriviallyCopyable& other)
        : p(new int(*other.p)) {}
    ~NonTriviallyCopyable() { delete p; }
};

// ==================== 标准布局 ====================
struct StandardLayout {
    int x;
    int y;
    void* z;
};

// 非标准布局：有虚函数
struct NonStandardLayout {
    int x;
    virtual void func() {}
};

// ==================== 对齐影响 ====================
struct Packed {
    char c;
    int i;
};

struct Aligned {
    char c;
    int i;
} __attribute__((aligned(16)));

// ==================== 演示主函数 ====================
int main() {
    std::cout << "=== 对象大小与平凡类型示例 ===" << std::endl;

    // 1. 平凡类型
    std::cout << "\n--- 平凡类型 (is_trivial) ---" << std::endl;
    std::cout << "Trivial: " << std::is_trivial_v<Trivial> << std::endl;
    std::cout << "sizeof(Trivial): " << sizeof(Trivial) << std::endl;
    std::cout << "NonTrivial: " << std::is_trivial_v<NonTrivial> << std::endl;
    std::cout << "sizeof(NonTrivial): " << sizeof(NonTrivial) << std::endl;

    // 2. 平凡可复制
    std::cout << "\n--- 平凡可复制 (is_trivially_copyable) ---" << std::endl;
    std::cout << "TriviallyCopyable: " << std::is_trivially_copyable_v<TriviallyCopyable> << std::endl;
    std::cout << "sizeof(TriviallyCopyable): " << sizeof(TriviallyCopyable) << std::endl;
    std::cout << "NonTriviallyCopyable: " << std::is_trivially_copyable_v<NonTriviallyCopyable> << std::endl;
    std::cout << "sizeof(NonTriviallyCopyable): " << sizeof(NonTriviallyCopyable) << std::endl;

    // 3. 标准布局
    std::cout << "\n--- 标准布局 (is_standard_layout) ---" << std::endl;
    std::cout << "StandardLayout: " << std::is_standard_layout_v<StandardLayout> << std::endl;
    std::cout << "sizeof(StandardLayout): " << sizeof(StandardLayout) << std::endl;
    std::cout << "NonStandardLayout: " << std::is_standard_layout_v<NonStandardLayout> << std::endl;
    std::cout << "sizeof(NonStandardLayout): " << sizeof(NonStandardLayout) << std::endl;

    // 4. 对齐影响
    std::cout << "\n--- 对齐影响大小 ---" << std::endl;
    std::cout << "sizeof(Packed): " << sizeof(Packed) << std::endl;
    std::cout << "offset of Packed.c: " << offsetof(Packed, c) << std::endl;
    std::cout << "offset of Packed.i: " << offsetof(Packed, i) << std::endl;
    std::cout << "sizeof(Aligned): " << sizeof(Aligned) << std::endl;
    std::cout << "alignof(Aligned): " << alignof(Aligned) << std::endl;

    // 5. 指针大小
    std::cout << "\n--- 指针大小 ---" << std::endl;
    std::cout << "sizeof(int*): " << sizeof(int*) << std::endl;
    std::cout << "sizeof(void(*)()): " << sizeof(void(*)()) << std::endl;
    std::cout << "sizeof(void(*)(int)): " << sizeof(void(*)(int)) << std::endl;

    std::cout << "\n关键点：" << std::endl;
    std::cout << "1. 平凡类型可以用 memcpy 安全复制" << std::endl;
    std::cout << "2. 标准布局可以与 C 代码互操作" << std::endl;
    std::cout << "3. 对齐会填充内存，影响对象大小" << std::endl;
    std::cout << "4. 虚函数表指针会增加对象大小" << std::endl;
    std::cout << "5. 平凡可复制类型适合 DMA 传输" << std::endl;

    return 0;
}
