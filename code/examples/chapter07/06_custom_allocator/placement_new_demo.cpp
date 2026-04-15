// placement_new_demo.cpp - 演示 placement new 与对象构造/析构
#include <iostream>
#include <new>
#include <cstring>

// 简单的分配器接口
class SimpleAllocator {
public:
    virtual void* allocate(std::size_t n) = 0;
    virtual void deallocate(void* p) = 0;
};

// 示例对象
struct Widget {
    int id;
    char name[32];
    bool initialized;

    Widget(int i, const char* n) : id(i), initialized(true) {
        std::strncpy(name, n, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        std::cout << "  Widget " << id << " constructed\n";
    }

    ~Widget() {
        std::cout << "  Widget " << id << " destroyed\n";
        initialized = false;
    }

    void greet() const {
        if (initialized) {
            std::cout << "    Hello, I'm Widget " << id << " (" << name << ")\n";
        }
    }
};

// C++ 风格的构造包装器
template<typename T, typename Alloc, typename... Args>
T* construct_with(Alloc& a, Args&&... args) {
    void* mem = a.allocate(sizeof(T));
    if (!mem) return nullptr;
    return new (mem) T(std::forward<Args>(args)...);
}

// C++ 风格的析构包装器
template<typename T, typename Alloc>
void destroy_with(Alloc& a, T* obj) noexcept {
    if (!obj) return;
    obj->~T();
    a.deallocate(static_cast<void*>(obj));
}

// 简单的线性分配器用于演示
class DemoAllocator : public SimpleAllocator {
    char buffer[1024];
    char* ptr;

public:
    DemoAllocator() : ptr(buffer) {}

    void* allocate(std::size_t n) override {
        if (ptr + n > buffer + sizeof(buffer)) return nullptr;
        void* res = ptr;
        ptr += n;
        return res;
    }

    void deallocate(void* p) override {
        // 线性分配器不支持单个释放
        (void)p;
    }
};

int main() {
    std::cout << "=== Placement New Demo ===\n\n";

    DemoAllocator alloc;

    std::cout << "=== Using construct_with ===\n";
    Widget* w1 = construct_with<Widget>(alloc, 1, "Alpha");
    Widget* w2 = construct_with<Widget>(alloc, 2, "Beta");
    Widget* w3 = construct_with<Widget>(alloc, 3, "Gamma");

    std::cout << "\n=== Using widgets ===\n";
    if (w1) w1->greet();
    if (w2) w2->greet();
    if (w3) w3->greet();

    std::cout << "\n=== Using destroy_with ===\n";
    destroy_with(alloc, w2);
    std::cout << "Widget 2 destroyed\n";

    std::cout << "\n=== Manual placement new demo ===\n";
    // 手动 placement new
    char storage[sizeof(Widget)];
    Widget* w_manual = new (storage) Widget(99, "Manual");

    std::cout << "\n";
    w_manual->greet();

    std::cout << "\n=== Manual cleanup ===\n";
    // 必须手动调用析构函数
    w_manual->~Widget();

    // 清理剩余对象
    std::cout << "\n=== Cleanup remaining ===\n";
    destroy_with(alloc, w1);
    destroy_with(alloc, w3);

    std::cout << "\n=== Key points ===\n";
    std::cout << "- Placement new separates allocation from construction\n";
    std::cout << "- Must manually call destructor for placement-new objects\n";
    std::cout << "- Perfect for custom allocators and object pools\n";
    std::cout << "- Always pair new (mem) Type with ptr->~Type()\n";

    return 0;
}
