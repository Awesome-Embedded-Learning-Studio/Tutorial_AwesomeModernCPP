#include <iostream>
#include <memory>
#include <utility>

struct Task {
    explicit Task(int id) : id_(id) { std::cout << "Task(" << id_ << ") 构造\n"; }
    ~Task() { std::cout << "~Task(" << id_ << ") 析构\n"; }
    int id() const { return id_; }

  private:
    int id_;
};

// borrow：只读一下，不碰所有权
void report(const Task& t) {
    std::cout << "report 看到了 Task " << t.id() << "\n";
}

// sink：按值收，函数接手
void finish(std::unique_ptr<Task> t) {
    std::cout << "finish 接手了 Task " << t->id() << "\n";
} // t 在这里析构——对象死在函数里

// 工厂：造好之后，连所有权一起交出（move-out）
std::unique_ptr<Task> make_task(int id) {
    return std::make_unique<Task>(id);
}

int main() {
    std::cout << "--- 场景一：borrow ---\n";
    {
        auto t = make_task(1);
        report(*t); // 借用：用完就还
    } // t 离开作用域，unique_ptr 析构 Task

    std::cout << "--- 场景二：sink ---\n";
    {
        auto t = make_task(2);
        finish(std::move(t)); // 按值传参，所有权换手进函数
    } // t 已经两手空空，这里无事发生

    std::cout << "--- main 收尾 ---\n";
    return 0;
}
