// 02_thread_local.cpp —— 《thread_local：每线程一份的世界》配套代码
//
// 编译：g++ -std=c++20 -Wall -Wextra -pedantic -pthread -o 02_thread_local 02_thread_local.cpp
// 运行：./02_thread_local
//
// 演示矩阵：
//   场景 1｜每线程一份：同名 counter 在三个线程里地址互不相同、计数互不干扰
//   场景 2｜初始化粒度：只碰三个 thread_local 中的一个，三个构造函数一起跑；
//          析构按构造的逆序，worker 的析构在 join 返回前完成
//   场景 3｜块作用域：首次经过声明才初始化；构造抛异常不算初始化完成，
//          下一次经过声明时重试；只有成功初始化过的对象才会析构
//   场景 4｜类内静态成员：Widget::calls 两个线程各自加到 1，地址互不相同
//
// 另有两个独立实验各占一文件（都要单独进程跑，详见各文件头注释）：
//   02_thread_local_exit.cpp   std::exit 时别的线程的析构缺席（正文「析构」节）
//   02_thread_local_throw.cpp  命名空间作用域构造抛异常：接得住 / 逃逸 134

#include <cstdio>
#include <stdexcept>
#include <thread>

// ── 场景 2 的三个探针：构造与析构全部留痕 ─────────────────────────────
struct Tag {
    const char* name;
    explicit Tag(const char* n) : name(n) { std::printf("    ctor %s\n", n); }
    ~Tag() { std::printf("    dtor %s\n", name); }
};

thread_local Tag probe_a{"a"};
thread_local Tag probe_b{"b"};
thread_local Tag probe_c{"c"};

// ── 场景 1：每线程一份的计数器（常量初始化，不触发场景 2 的动态初始化）──
thread_local int counter = 0;

void bump(const char* who)
{
    for (int i = 0; i < 3; ++i) {
        ++counter;
        std::printf("%s: counter=%d，地址 %p\n", who, counter, (void*)&counter);
    }
}

void scenario_per_thread()
{
    std::printf("场景 1：每线程一份\n");
    std::thread t1(bump, "t1");
    std::thread t2(bump, "t2");
    t1.join();
    t2.join();
    std::printf("main: counter=%d，地址 %p\n", counter, (void*)&counter);
}

// ── 场景 2：初始化粒度与析构时点 ─────────────────────────────────────
void scenario_init_granularity()
{
    std::printf("\n场景 2：初始化粒度\n");
    std::printf("main 只碰 probe_c：\n");
    (void)probe_c;
    std::thread worker([] {
        std::printf("worker 只碰 probe_a：\n");
        (void)probe_a;
    });
    worker.join();
    std::printf("join 已返回\n");
}

// ── 场景 3：块作用域的重试语义 ───────────────────────────────────────
int g_attempts = 0;

struct Flaky {
    Flaky()
    {
        ++g_attempts;
        std::printf("    第 %d 次尝试构造\n", g_attempts);
        if (g_attempts < 3) {
            throw std::runtime_error("还没准备好");
        }
    }
    ~Flaky() { std::printf("    dtor：成功构造过，退出线程时析构\n"); }
};

int next_id()
{
    try {
        thread_local Flaky engine;   // 块作用域：首次经过声明才初始化
    } catch (const std::exception& e) {
        std::printf("    捕获：%s\n", e.what());
        return -1;
    }
    return g_attempts;
}

void scenario_block_retry()
{
    std::printf("\n场景 3：块作用域的重试\n");
    std::printf("  第 1 次调用 -> %d\n", next_id());
    std::printf("  第 2 次调用 -> %d\n", next_id());
    std::printf("  第 3 次调用 -> %d\n", next_id());
}

// ── 场景 4：类内静态数据成员 thread_local ─────────────────────────────
// 正文「声明能写在哪」节：类内声明、类外定义时 static 不再写、thread_local 要写。
// Widget::calls 是常量初始化（= 0），odr-use 它不会触发上面三个探针的动态初始化。
struct Widget {
    static thread_local int calls;    // 类内声明
};
thread_local int Widget::calls = 0;   // 类外定义：static 不重复，thread_local 要写

void widget_bump(const char* who)
{
    ++Widget::calls;
    std::printf("%s: Widget::calls=%d，地址 %p\n", who, Widget::calls, (void*)&Widget::calls);
}

void scenario_class_static()
{
    std::printf("\n场景 4：类内静态成员 thread_local\n");
    std::thread t1(widget_bump, "t1");
    std::thread t2(widget_bump, "t2");
    t1.join();
    t2.join();
    std::printf("main: Widget::calls=%d，地址 %p\n", Widget::calls, (void*)&Widget::calls);
}

int main()
{
    scenario_per_thread();
    scenario_init_granularity();
    scenario_block_retry();
    scenario_class_static();
    std::printf("\nmain 返回：main 线程的三个探针在此之后析构\n");
    return 0;
}
