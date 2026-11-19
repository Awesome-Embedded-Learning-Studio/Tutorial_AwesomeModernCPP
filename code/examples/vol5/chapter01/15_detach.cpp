// detach:放开线程自己收尾,主线程先走,进程等全部线程结束才退
#include <chrono>
#include <print>
#include <thread>

void background_task() {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::println("background task finished");
}

int main() {
    std::thread t(background_task);
    t.detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::println("main: exiting");
}
