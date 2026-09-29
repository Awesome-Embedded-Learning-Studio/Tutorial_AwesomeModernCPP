// 演示块作用域 thread_local 的懒构造与重试：构造抛异常后下次经过声明再试
#include <cstdio>
#include <stdexcept>
#include <thread>

struct Tag {
    const char* name;
    explicit Tag(const char* n) : name(n) { std::printf("    ctor %s\n", n); }
    ~Tag() { std::printf("    dtor %s\n", name); }
};

thread_local Tag probe_a{"a"};
thread_local Tag probe_b{"b"};
thread_local Tag probe_c{"c"};

int g_attempts = 0;

struct Flaky {
    Flaky() {
        ++g_attempts;
        std::printf("    第 %d 次尝试构造\n", g_attempts);
        if (g_attempts < 3) {
            throw std::runtime_error("还没准备好");
        }
    }
    ~Flaky() { std::printf("    dtor：成功构造过，退出线程时析构\n"); }
};

int next_id() {
    try {
        thread_local Flaky engine; // 每线程一份，首次经过声明时构造
    } catch (const std::exception& e) {
        std::printf("    捕获：%s\n", e.what());
        return -1;
    }
    return g_attempts;
}

int main() {
    for (int i = 1; i <= 3; ++i) {
        std::printf("  第 %d 次调用 -> %d\n", i, next_id());
    }
    std::printf("main 返回：main 线程的三个探针在此之后析构\n");
    return 0;
}
