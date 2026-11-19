// JoiningThread 完整教学版:接管所有权,析构自动 join,move 赋值先收旧的
#include <chrono>
#include <cstdio>
#include <print>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

class JoiningThread {
  public:
    JoiningThread() noexcept = default;

    // 接受任意可调用对象和参数，原样转发给 std::thread
    template <typename Callable, typename... Args>
    explicit JoiningThread(Callable&& func, Args&&... args)
        : thread_(std::forward<Callable>(func), std::forward<Args>(args)...) {}

    // 从 std::thread 接管所有权
    explicit JoiningThread(std::thread t) noexcept : thread_(std::move(t)) {}

    JoiningThread(JoiningThread&& other) noexcept : thread_(std::move(other.thread_)) {}

    JoiningThread& operator=(JoiningThread&& other) noexcept {
        if (this != &other) {
            try {
                if (thread_.joinable())
                    thread_.join(); // 收掉手里的，再接新的
            } catch (const std::system_error& e) {
                std::fprintf(stderr, "join failed in move assignment: %s\n", e.what());
            }
            thread_ = std::move(other.thread_);
        }
        return *this;
    }

    ~JoiningThread() {
        if (thread_.joinable()) {
            try {
                thread_.join();
            } catch (const std::system_error& e) {
                std::fprintf(stderr, "join failed in destructor: %s\n", e.what());
            }
        }
    }

    void join() { thread_.join(); }
    bool joinable() const noexcept { return thread_.joinable(); }

    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;

  private:
    std::thread thread_;
};

void task(int id) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::println("task {} done", id);
}

int main() {
    std::println("-- phase 1: two threads, nobody calls join by hand");
    {
        JoiningThread t1(task, 1);
        JoiningThread t2(task, 2);
    }
    std::println("-- phase 2: reassignment joins the old thread first");
    {
        JoiningThread t(task, 3);
        t = JoiningThread(task, 4);
    }
    std::println("-- phase 3: a vector of JoiningThread");
    {
        std::vector<JoiningThread> threads;
        threads.reserve(4);
        for (int i = 5; i < 9; ++i)
            threads.emplace_back(task, i);
    }
    std::println("main exits, every thread was joined by destructors");
}
