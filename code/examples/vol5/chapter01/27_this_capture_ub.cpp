// 捕获 this 指针后销毁对象:未定义行为,读到垃圾或崩溃都是它的样子
#include <chrono>
#include <print>
#include <thread>

class Worker {
  public:
    void start() {
        t_ = std::thread([this] { // 捕获的是指针，不是对象
            for (int i = 0; i < 8; ++i) {
                std::println("tick {}, sees running_ = {}", i, running_);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        t_.detach();
    }

  private:
    bool running_ = true;
    std::thread t_;
};

int main() {
    Worker* w = new Worker;
    w->start();
    delete w; // 对象立刻销毁，线程还在跑
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    std::println("main exits");
}
