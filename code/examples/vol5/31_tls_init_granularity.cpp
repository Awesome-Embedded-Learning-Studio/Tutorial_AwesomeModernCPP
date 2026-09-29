// 演示 thread_local 初始化粒度是整个翻译单元：只碰一个变量，三个构造函数一起跑
#include <cstdio>
#include <thread>

struct Tag {
    const char* name;
    explicit Tag(const char* n) : name(n) { std::printf("    ctor %s\n", n); }
    ~Tag() { std::printf("    dtor %s\n", name); }
};

thread_local Tag probe_a{"a"};
thread_local Tag probe_b{"b"};
thread_local Tag probe_c{"c"};

void scenario_init_granularity() {
    std::printf("main 只碰 probe_c：\n");
    (void)probe_c; // 只碰这一个

    std::thread worker([] {
        std::printf("worker 只碰 probe_a：\n");
        (void)probe_a; // 也只碰这一个
    });
    worker.join();
    std::printf("join 已返回\n");
}

int main() {
    scenario_init_granularity();
    return 0;
}
