// BindOnce + WeakPtr 自动取消:工业级 InvokeHelper<true>::MakeItSo 的教学翻译
// 来源:WeakPtr 实战(五):与回调集成——关闭 OnceCallback 的环 (02-5)
// 编译:g++ -std=c++20 -Wall -Wextra -I. 18_bind_weakptr_cancel.cpp -o 18_bind_weakptr_cancel
// -pthread

#include "weak_ptr.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

// 一个极简的 OnceCallback<void()> 等价物(用 std::function 代替 01 系列的 move_only_function)
using Task = std::function<void()>;

namespace tamcpp::chrome {

// bind_weak_once:把成员方法 + WeakPtr<T> 绑成一个 void() 回调
// 核心是 if(!receiver) return; —— 对应 Chromium InvokeHelper<true>::MakeItSo 的取消点
template <typename T, typename... Bound>
Task bind_weak_once(void (T::*method)(Bound...), WeakPtr<T> receiver, Bound... bound_args) {
    // 注意:弱调用强制 void 返回(取消时无值可返);这里 method 本就返回 void
    return [method, receiver = std::move(receiver),
            bound = std::make_tuple(std::move(bound_args)...)]() mutable {
        if (!receiver) { // ← 取消点:对象死后静默 no-op
            std::cout << "    [task] receiver 已失效 → 静默 no-op\n";
            return;
        }
        std::cout << "    [task] receiver 有效 → 真正调用方法\n";
        std::apply([&](auto&&... args) { (receiver.get()->*method)(args...); }, bound);
    };
}

} // namespace tamcpp::chrome

class Controller {
  public:
    void on_done(int v) {
        buf_.push_back(v);
        std::cout << "    [Controller::on_done] 收到 " << v << ", buf_size=" << buf_.size() << "\n";
    }
    tamcpp::chrome::WeakPtr<Controller> get_weak() { return weak_factory_.get_weak_ptr(); }

  private:
    std::vector<int> buf_;
    tamcpp::chrome::WeakPtrFactory<Controller> weak_factory_{this}; // 最后成员
};

int main() {
    using namespace tamcpp::chrome;

    std::cout << "=== 对象活着时,绑定的回调正常调用 ===\n";
    {
        Controller c;
        Task task = bind_weak_once(&Controller::on_done, c.get_weak(), 42);
        std::cout << "  run task(c 还活着):\n";
        task(); // 真正调用 c.on_done(42)
    }

    std::cout << "\n=== 对象析构后,回调自动静默 no-op(不会悬空 deref)==\n";
    Task dangling;
    {
        Controller c;
        dangling = bind_weak_once(&Controller::on_done, c.get_weak(), 99);
        std::cout << "  (c 即将离开作用域,任务还持着 c 的 WeakPtr)\n";
    } // c 析构 → weak_factory_ 先失效所有 WeakPtr
    std::cout << "  run task(c 已析构):\n";
    dangling(); // receiver 失效 → if(!receiver) return; → 静默 no-op

    std::cout << "\n=== 对照:裸指针 receiver(Unretained 风格)对象死后即 UAF ===\n";
    std::cout << "  Unretained 走 InvokeHelper<false>,无 if(!target) 守门,\n";
    std::cout << "  对象死后还 run() 就是悬垂解引用(WeakPtr 用事前 no-op 规避)\n";
    return 0;
}
