#include <iostream>
#include <new>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// 演示placement new的基础用法

struct Foo {
    int x;
    double y;

    Foo(int v) : x(v), y(v * 1.5) {
        std::cout << "Foo(" << x << ", " << y << ") constructed\n";
    }

    ~Foo() {
        std::cout << "Foo(" << x << ", " << y << ") destructed\n";
    }

    void print() const {
        std::cout << "Foo{x=" << x << ", y=" << y << "}\n";
    }
};

void basic_placement_new_demo() {
    std::cout << "=== Basic Placement New Demo ===\n\n";

    // 方法1: 使用 alignas 和 unsigned char 数组
    std::cout << "--- Method 1: alignas + unsigned char array ---\n";
    {
        alignas(Foo) unsigned char buffer1[sizeof(Foo)];
        std::cout << "Buffer address: " << static_cast<void*>(buffer1) << "\n";
        std::cout << "Buffer aligned to " << alignof(Foo) << " bytes? "
                  << (reinterpret_cast<uintptr_t>(buffer1) % alignof(Foo) == 0) << "\n";

        Foo* p1 = new (buffer1) Foo(42);
        p1->print();

        // 显式析构（非常重要！）
        p1->~Foo();
    }

    // 方法2: 使用 std::aligned_storage (C++11/14风格)
    std::cout << "\n--- Method 2: std::aligned_storage (C++11/14) ---\n";
    {
        using Storage = typename std::aligned_storage<sizeof(Foo), alignof(Foo)>::type;
        Storage storage2;
        std::cout << "Storage address: " << &storage2 << "\n";

        Foo* p2 = new (&storage2) Foo(100);
        p2->print();
        p2->~Foo();
    }

    // 方法3: C++17 std::aligned_storage_t (更简洁)
    std::cout << "\n--- Method 3: std::aligned_storage_t (C++17) ---\n";
    {
        std::aligned_storage_t<sizeof(Foo), alignof(Foo)> storage3;
        Foo* p3 = new (&storage3) Foo(200);
        p3->print();
        p3->~Foo();
    }

    // 多个对象的placement new
    std::cout << "\n--- Multiple Objects in Array ---\n";
    {
        constexpr size_t N = 3;
        alignas(Foo) unsigned char buffer[N * sizeof(Foo)];

        Foo* objs[N];
        for (size_t i = 0; i < N; ++i) {
            void* slot = buffer + i * sizeof(Foo);
            objs[i] = new (slot) Foo(static_cast<int>(i * 10));
        }

        for (size_t i = 0; i < N; ++i) {
            objs[i]->print();
        }

        // 反向析构（构造的逆序）
        for (int i = static_cast<int>(N - 1); i >= 0; --i) {
            objs[i]->~Foo();
        }
    }
}

// 异常安全的placement new
void exception_safe_demo() {
    std::cout << "\n=== Exception Safe Placement New ===\n\n";

    struct MightThrow {
        int value;
        bool should_throw;

        MightThrow(int v, bool throw_flag) : value(v), should_throw(throw_flag) {
            if (should_throw) {
                throw std::runtime_error("Construction failed");
            }
            std::cout << "MightThrow(" << value << ") constructed\n";
        }

        ~MightThrow() {
            std::cout << "MightThrow(" << value << ") destructed\n";
        }
    };

    constexpr size_t N = 3;
    alignas(MightThrow) unsigned char buffer[N * sizeof(MightThrow)];
    MightThrow* objs[N] = {nullptr};
    int constructed = 0;

    try {
        for (int i = 0; i < 3; ++i) {
            void* slot = buffer + i * sizeof(MightThrow);
            // 第三个对象会抛出异常
            objs[i] = new (slot) MightThrow(i, (i == 2));
            ++constructed;
        }
    } catch (const std::exception& e) {
        std::cout << "Exception caught: " << e.what() << "\n";
        std::cout << "Constructed " << constructed << " objects before failure\n";

        // 回滚已经构造的对象
        for (int i = constructed - 1; i >= 0; --i) {
            if (objs[i]) {
                objs[i]->~MightThrow();
            }
        }
    }
}

// 对齐检查工具
template<typename T>
bool is_aligned(void* ptr, size_t alignment = alignof(T)) {
    return reinterpret_cast<uintptr_t>(ptr) % alignment == 0;
}

void alignment_check_demo() {
    std::cout << "\n=== Alignment Check Demo ===\n\n";

    // 检查不同类型的对齐要求
    std::cout << "Alignment requirements:\n";
    std::cout << "  char:     " << alignof(char) << " bytes\n";
    std::cout << "  int:      " << alignof(int) << " bytes\n";
    std::cout << "  double:   " << alignof(double) << " bytes\n";
    std::cout << "  int64_t:  " << alignof(int64_t) << " bytes\n";

    // 演示未对齐的后果
    alignas(int) unsigned char buffer[32];

    for (int offset = 0; offset < 4; ++offset) {
        void* ptr = buffer + offset;
        bool aligned = is_aligned<int>(ptr);
        std::cout << "  Offset " << offset << ": aligned? " << aligned << "\n";

        if (aligned) {
            int* p = new (ptr) int(42);
            std::cout << "    Successfully constructed int at " << p << "\n";
            p->~int();
        }
    }
}

// 在栈上构造"动态"大小的对象
void stack_vector_demo() {
    std::cout << "\n=== Stack-Allocated \"Dynamic\" Container ===\n\n";

    template<size_t N>
    class StackVector {
        alignas(double) unsigned char buffer_[N * sizeof(double)];
        size_t size_ = 0;

    public:
        void push(double v) {
            if (size_ < N) {
                new (buffer_ + size_ * sizeof(double)) double(v);
                ++size_;
            }
        }

        double& operator[](size_t i) {
            return *reinterpret_cast<double*>(buffer_ + i * sizeof(double));
        }

        size_t size() const { return size_; }

        ~StackVector() {
            for (size_t i = 0; i < size_; ++i) {
                reinterpret_cast<double*>(buffer_ + i * sizeof(double))->~double();
            }
        }
    };

    StackVector<10> vec;
    for (int i = 0; i < 5; ++i) {
        vec.push(i * 1.1);
    }

    std::cout << "StackVector contents: ";
    for (size_t i = 0; i < vec.size(); ++i) {
        std::cout << vec[i] << " ";
    }
    std::cout << "\n";
}

int main() {
    basic_placement_new_demo();
    exception_safe_demo();
    alignment_check_demo();
    stack_vector_demo();

    std::cout << "\n=== Key Takeaways ===\n";
    std::cout << "1. Placement new constructs objects at pre-allocated memory\n";
    std::cout << "2. Always call destructor explicitly for placement-new objects\n";
    std::cout << "3. Use alignas() or aligned_storage for proper alignment\n";
    std::cout << "4. Exception safety requires manual cleanup in catch blocks\n";
    std::cout << "5. Never use delete on placement-new objects\n";

    return 0;
}
