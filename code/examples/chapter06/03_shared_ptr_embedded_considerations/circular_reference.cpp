// std::shared_ptr 循环引用与 weak_ptr 示例
// 演示循环引用导致的内存泄漏和 weak_ptr 的解决方案

#include <memory>
#include <cstdio>
#include <string>

// ============ 循环引用问题示例 ============

struct NodeBad {
    std::string name;
    std::shared_ptr<NodeBad> next;

    NodeBad(const std::string& n) : name(n) {
        printf("NodeBad '%s' constructed\n", name.c_str());
    }
    ~NodeBad() {
        printf("NodeBad '%s' destroyed\n", name.c_str());
    }

    void set_next(std::shared_ptr<NodeBad> n) {
        next = n;
    }
};

// 演示循环引用导致的内存泄漏
void circular_reference_leak() {
    printf("=== Circular Reference Leak Demo ===\n");

    auto node_a = std::make_shared<NodeBad>("A");
    auto node_b = std::make_shared<NodeBad>("B");

    printf("Before linking: A.use_count() = %ld, B.use_count() = %ld\n",
           node_a.use_count(), node_b.use_count());

    // 创建循环：A -> B, B -> A
    node_a->set_next(node_b);
    node_b->set_next(node_a);

    printf("After linking: A.use_count() = %ld, B.use_count() = %ld\n",
           node_a.use_count(), node_b.use_count());

    // 离开作用域时，两个节点的引用计数都变成 1，不会被释放！
    // 这是内存泄漏
}

// ============ 使用 weak_ptr 解决循环引用 ============

struct NodeGood : public std::enable_shared_from_this<NodeGood> {
    std::string name;
    std::shared_ptr<NodeGood> next;
    std::weak_ptr<NodeGood> prev;  // 使用 weak_ptr 打破循环

    NodeGood(const std::string& n) : name(n) {
        printf("NodeGood '%s' constructed\n", name.c_str());
    }
    ~NodeGood() {
        printf("NodeGood '%s' destroyed\n", name.c_str());
    }

    void set_next(std::shared_ptr<NodeGood> n) {
        next = n;
        n->prev = shared_from_this();
    }

    void print_prev() const {
        if (auto p = prev.lock()) {
            printf("  Prev: %s\n", p->name.c_str());
        } else {
            printf("  Prev: (null)\n");
        }
    }
};

// 正确的双向链表实现
void correct_doubly_linked_list() {
    printf("\n=== Correct Doubly Linked List Demo ===\n");

    auto node_a = std::make_shared<NodeGood>("A");
    auto node_b = std::make_shared<NodeGood>("B");

    printf("Before linking: A.use_count() = %ld, B.use_count() = %ld\n",
           node_a.use_count(), node_b.use_count());

    node_a->set_next(node_b);

    printf("After linking: A.use_count() = %ld, B.use_count() = %ld\n",
           node_a.use_count(), node_b.use_count());

    node_b->print_prev();
}

// ============ enable_shared_from_this 示例 ============

class Task : public std::enable_shared_from_this<Task> {
    std::string name_;
public:
    Task(const std::string& name) : name_(name) {
        printf("Task '%s' constructed\n", name_.c_str());
    }
    ~Task() {
        printf("Task '%s' destroyed\n", name_.c_str());
    }

    // 正确获取指向自身的 shared_ptr
    std::shared_ptr<Task> get_shared() {
        return shared_from_this();
    }

    void run() {
        printf("Task '%s' running\n", name_.c_str());
    }
};

void enable_shared_from_this_example() {
    printf("\n=== enable_shared_from_this Demo ===\n");

    auto task = std::make_shared<Task>("ImportantTask");
    printf("Initial use_count: %ld\n", task.use_count());

    // 从成员函数获取 shared_ptr，不会创建新的控制块
    auto task2 = task->get_shared();
    printf("After get_shared: use_count = %ld\n", task.use_count());

    task->run();
}

// ============ weak_ptr 锁定示例 ============

void weak_ptr_lock_example() {
    printf("\n=== weak_ptr lock() Example ===\n");

    auto sp = std::make_shared<NodeGood>("Temp");
    std::weak_ptr<NodeGood> wp = sp;

    printf("weak_ptr expired: %s\n", wp.expired() ? "yes" : "no");

    if (auto locked = wp.lock()) {
        printf("Locked successfully: %s\n", locked->name.c_str());
    }

    sp.reset();  // 销毁对象

    printf("After reset, weak_ptr expired: %s\n", wp.expired() ? "yes" : "no");

    if (auto locked = wp.lock()) {
        printf("This won't print\n");
    } else {
        printf("lock() failed, object was destroyed\n");
    }
}

int main() {
    circular_reference_leak();
    // 注意：node_a 和 node_b 没有被销毁（内存泄漏）

    correct_doubly_linked_list();
    // node_a 和 node_b 会被正确销毁

    enable_shared_from_this_example();

    weak_ptr_lock_example();

    printf("\n=== All Examples Complete ===\n");
    printf("Notice: In circular_reference_leak(), the destructors were NOT called.\n");

    return 0;
}
