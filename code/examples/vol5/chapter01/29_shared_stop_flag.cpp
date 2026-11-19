// shared_ptr 加 atomic 标志:对象销毁后线程仍能安全收场
#include <atomic>
#include <chrono>
#include <memory>
#include <print>
#include <thread>

class BackgroundWorker {
  public:
    BackgroundWorker() : running_(std::make_shared<std::atomic<bool>>(true)) {}

    void start() {
        auto running = running_; // 复制 shared_ptr，引用计数加一
        std::thread t([running] {
            while (running->load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            std::println("worker exits cleanly");
        });
        t.detach();
    }

    void stop() { running_->store(false); }
    ~BackgroundWorker() { stop(); }

  private:
    std::shared_ptr<std::atomic<bool>> running_;
};

int main() {
    {
        BackgroundWorker worker;
        worker.start();
    }
    std::println("worker object already destroyed");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::println("main exits");
}
