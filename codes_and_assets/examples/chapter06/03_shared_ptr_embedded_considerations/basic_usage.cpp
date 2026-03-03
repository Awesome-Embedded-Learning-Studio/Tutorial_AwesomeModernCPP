// std::shared_ptr 基本用法与代价分析示例
// 演示 shared_ptr 的使用和嵌入式环境下的注意事项

#include <memory>
#include <cstdio>
#include <cstdio>

struct Widget {
    int id;
    Widget(int i) : id(i) {
        printf("Widget %d constructed\n", id);
    }
    ~Widget() {
        printf("Widget %d destroyed\n", id);
    }
    void use() const {
        printf("Using widget %d\n", id);
    }
};

// 基本用法示例
void basic_example() {
    printf("=== Basic shared_ptr Example ===\n");

    auto p1 = std::make_shared<Widget>(1);
    printf("After creation: use_count = %ld\n", p1.use_count());

    {
        auto p2 = p1;  // 拷贝，引用计数增加
        printf("After copy: use_count = %ld\n", p1.use_count());
        p2->use();
    }

    printf("After p2 destroyed: use_count = %ld\n", p1.use_count());
}

// 内存开销示例
void memory_overhead_example() {
    printf("\n=== Memory Overhead Example ===\n");

    printf("sizeof(int*):                         %zu bytes\n", sizeof(int*));
    printf("sizeof(std::unique_ptr<int>):          %zu bytes\n", sizeof(std::unique_ptr<int>));
    printf("sizeof(std::shared_ptr<int>):          %zu bytes\n", sizeof(std::shared_ptr<int>));
    printf("sizeof(std::weak_ptr<int>):            %zu bytes\n", sizeof(std::weak_ptr<int>));

    // 控制块通常单独分配
    auto p = std::make_shared<int>(42);
    printf("Control block address: %p\n", p.get());
}

// 自定义删除器示例
void custom_deleter_example() {
    printf("\n=== Custom Deleter Example ===\n");

    auto deleter = [](Widget* w) {
        printf("[Custom Deleter] Destroying widget %d\n", w->id);
        delete w;
    };

    std::shared_ptr<Widget> p(new Widget(100), deleter);
    p->use();
    printf("use_count: %ld\n", p.use_count());
}

// make_shared 优化示例
void make_shared_example() {
    printf("\n=== make_shared Optimization ===\n");

    // make_shared 一次性分配对象和控制块
    auto p1 = std::make_shared<Widget>(200);
    printf("Widget created with make_shared\n");

    // 相比之下，这种方式需要两次分配（对象 + 控制块）
    std::shared_ptr<Widget> p2(new Widget(201));
    printf("Widget created with shared_ptr constructor\n");
}

int main() {
    basic_example();
    memory_overhead_example();
    custom_deleter_example();
    make_shared_example();

    printf("\n=== All Examples Complete ===\n");

    return 0;
}
