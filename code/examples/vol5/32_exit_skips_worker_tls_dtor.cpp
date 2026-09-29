// 演示 std::exit 只析构调用方线程的 thread_local：worker 的对象构造了却不析构
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

struct Tag {
    const char* name;
    explicit Tag(const char* n) : name(n) { std::printf("    ctor %s\n", n); }
    ~Tag() { std::printf("    dtor %s\n", name); }
};

thread_local Tag worker_tag{"worker_tag"}; // Tag 与场景 2 的相同

int main() {
    std::thread worker([] {
        (void)worker_tag; // 触发本线程的构造
        std::this_thread::sleep_for(std::chrono::seconds(5));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::printf("main: 还没等 worker 醒，直接 std::exit\n");
    std::exit(0);
}
