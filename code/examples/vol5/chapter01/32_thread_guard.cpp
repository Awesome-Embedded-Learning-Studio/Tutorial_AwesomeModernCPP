// ThreadGuard:栈展开时析构函数替您 join,异常也收得了场
#include <chrono>
#include <print>
#include <stdexcept>
#include <thread>

class ThreadGuard {
  public:
    enum class Action { kJoin, kDetach };
    explicit ThreadGuard(std::thread& t, Action action = Action::kJoin)
        : thread_(t), action_(action) {}
    ~ThreadGuard() {
        if (thread_.joinable()) {
            if (action_ == Action::kJoin)
                thread_.join();
            else
                thread_.detach();
        }
    }
    ThreadGuard(const ThreadGuard&) = delete;
    ThreadGuard& operator=(const ThreadGuard&) = delete;

  private:
    std::thread& thread_; // 持有引用，不拥有线程
    Action action_;
};

void risky() {
    throw std::runtime_error("boom");
}

void run_with_guard() {
    std::thread t([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::println("  worker finished its work");
    });
    ThreadGuard guard(t);
    risky(); // 抛出，栈展开时 guard 的析构函数 join 线程
}

int main() {
    try {
        run_with_guard();
    } catch (const std::exception& e) {
        std::println("caught: {}", e.what());
    }
    std::println("main continues normally");
}
