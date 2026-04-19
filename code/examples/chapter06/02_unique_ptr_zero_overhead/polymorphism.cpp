// std::unique_ptr 与多态示例
// 演示如何使用 unique_ptr 管理派生类对象

#include <memory>
#include <cstdio>
#include <cstring>

// 基类必须有虚析构函数
struct Base {
    virtual ~Base() = default;
    virtual void speak() const = 0;
};

struct Dog : Base {
    void speak() const override {
        printf("Woof!\n");
    }
    ~Dog() {
        printf("Dog destroyed\n");
    }
};

struct Cat : Base {
    void speak() const override {
        printf("Meow!\n");
    }
    ~Cat() {
        printf("Cat destroyed\n");
    }
};

struct Robot : Base {
    void speak() const override {
        printf("Beep boop!\n");
    }
    ~Robot() {
        printf("Robot destroyed\n");
    }
};

// 工厂函数返回 unique_ptr<Base>
std::unique_ptr<Base> create_animal(const char* type) {
    if (strcmp(type, "dog") == 0) {
        return std::make_unique<Dog>();
    } else if (strcmp(type, "cat") == 0) {
        return std::make_unique<Cat>();
    } else {
        return std::make_unique<Robot>();
    }
}

// 使用 unique_ptr<Base> 存储派生类
void polymorphism_example() {
    printf("=== Polymorphism Example ===\n");

    std::unique_ptr<Base> pet1 = std::make_unique<Dog>();
    std::unique_ptr<Base> pet2 = std::make_unique<Cat>();

    printf("Pet 1 says: ");
    pet1->speak();

    printf("Pet 2 says: ");
    pet2->speak();

    // 移动语义
    std::unique_ptr<Base> moved = std::move(pet1);
    if (!pet1) {
        printf("pet1 is now null (moved)\n");
    }

    printf("Moved pet says: ");
    moved->speak();
}

// 容器存储 unique_ptr
#include <vector>

void container_example() {
    printf("\n=== Container Example ===\n");

    std::vector<std::unique_ptr<Base>> zoo;

    zoo.push_back(std::make_unique<Dog>());
    zoo.push_back(std::make_unique<Cat>());
    zoo.push_back(std::make_unique<Robot>());
    zoo.push_back(create_animal("dog"));

    printf("Zoo sounds:\n");
    for (const auto& animal : zoo) {
        printf("  ");
        animal->speak();
    }

    // 容器销毁时，所有 unique_ptr 自动释放
    printf("Zoo being destroyed...\n");
}

// PIMPL 模式示例 (简化版)
class Widget {
public:
    Widget();
    ~Widget();
    void do_work();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

struct Widget::Impl {
    void do_work() {
        printf("Widget::Impl doing work\n");
    }
};

Widget::Widget() : pImpl(std::make_unique<Impl>()) {}
Widget::~Widget() = default;  // 即使 Impl 是不完整类型也没关系
void Widget::do_work() { pImpl->do_work(); }

void pimpl_example() {
    printf("\n=== PIMPL Example ===\n");
    Widget w;
    w.do_work();
}

int main() {
    polymorphism_example();
    container_example();
    pimpl_example();

    printf("\n=== All Examples Complete ===\n");

    return 0;
}
