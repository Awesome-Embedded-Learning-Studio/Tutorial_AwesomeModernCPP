// std::unique_ptr 基本用法示例
// 演示 unique_ptr 的基本使用和零开销特性

#include <memory>
#include <cstdio>
#include <cstring>

// 简单的 Sensor 类
struct Sensor {
    int id;
    bool active;

    Sensor(int i) : id(i), active(true) {
        printf("Sensor %d constructed\n", id);
    }

    void shutdown() {
        if (active) {
            printf("Sensor %d shutting down\n", id);
            active = false;
        }
    }

    ~Sensor() {
        shutdown();
    }

    void read() const {
        printf("Sensor %d reading\n", id);
    }
};

// 基本用法
void basic_example() {
    printf("=== Basic Usage Example ===\n");

    auto p = std::make_unique<Sensor>(42);
    p->read();
    (*p).read();

    // 离开作用域时自动 delete
}

// sizeof 验证零开销
void size_example() {
    printf("\n=== Size Verification Example ===\n");

    printf("sizeof(int*):            %zu bytes\n", sizeof(int*));
    printf("sizeof(std::unique_ptr<int>): %zu bytes\n", sizeof(std::unique_ptr<int>));
    printf("sizeof(std::unique_ptr<Sensor>): %zu bytes\n", sizeof(std::unique_ptr<Sensor>));

    // 通常 unique_ptr 的大小等于裸指针
    static_assert(sizeof(std::unique_ptr<int>) == sizeof(int*),
                  "unique_ptr should be same size as raw pointer");
}

// 移动语义示例
void move_example() {
    printf("\n=== Move Semantics Example ===\n");

    auto p1 = std::make_unique<Sensor>(1);
    printf("p1 created\n");

    // unique_ptr 不可拷贝，但可以移动
    auto p2 = std::move(p1);
    printf("p1 moved to p2\n");

    if (p1) {
        printf("p1 is valid\n");  // 不会执行
    } else {
        printf("p1 is null (after move)\n");  // 会执行
    }

    if (p2) {
        printf("p2 is valid\n");  // 会执行
        p2->read();
    }
}

// reset 和 release 示例
void reset_release_example() {
    printf("\n=== Reset and Release Example ===\n");

    auto p = std::make_unique<Sensor>(99);
    printf("Sensor 99 created\n");

    // reset() 销毁旧资源并接管新资源
    p.reset(new Sensor(100));
    printf("After reset: Sensor 100\n");

    // release() 返回原始指针并将 unique_ptr 置空
    Sensor* raw = p.release();
    printf("Released raw pointer to Sensor %d\n", raw->id);

    // 现在需要手动释放
    delete raw;
    printf("Manually deleted Sensor 100\n");
}

int main() {
    basic_example();
    size_example();
    move_example();
    reset_release_example();

    printf("\n=== All Examples Complete ===\n");

    return 0;
}
