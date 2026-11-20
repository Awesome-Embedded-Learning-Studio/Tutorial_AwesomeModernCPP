// JoiningThread 的析构:focus 在析构函数上——joinable 才 join,失败不逃出析构
#include <chrono>
#include <print>
#include <system_error>
#include <thread>
#include <utility>

class JoiningThread {
  public:
    explicit JoiningThread(std::thread t) noexcept : thread_(std::move(t)) {}

    ~JoiningThread() {
        if (thread_.joinable()) {
            try {
                thread_.join();
            } catch (const std::system_error& e) {
                std::println("join failed in destructor: {}", e.what());
            }
        }
    }

    JoiningThread(const JoiningThread&) = delete;
    JoiningThread& operator=(const JoiningThread&) = delete;

  private:
    std::thread thread_;
};

int main() {
    std::println("entering scope");
    {
        std::thread raw([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::println("  worker done");
        });
        JoiningThread t(std::move(raw));
        std::println("scope body, nobody calls join by hand");
    } // 析构在这里替我们 join
    std::println("scope left, worker already joined");
}
