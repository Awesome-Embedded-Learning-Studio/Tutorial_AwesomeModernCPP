// 02_thread_local_exit.cpp —— 《thread_local：每线程一份的世界》std::exit 析构缺席实验
//
// 正文出处：documents/vol5-concurrency/ch02-mutex-condition-sync/02-thread-local.md
// 「析构：逆序、时点，还有 std::exit」一节的完整实验程序（L185-207 的代码原样）。
//
// 编译：g++ -std=c++20 -Wall -Wextra -pedantic -pthread -o 02_thread_local_exit 02_thread_local_exit.cpp
// 运行：./02_thread_local_exit ; echo $?
//
// 预期输出恰好两行（与正文输出块逐字一致），随后进程退出，退出码 0：
//   ctor worker_tag
//   main: 还没等 worker 醒，直接 std::exit
// 全程找不到 dtor worker_tag 的行——谁调用 std::exit，被析构的就只有调用方
// 线程的对象；worker 线程的 thread_local 对象没人负责析构，进程退出的瞬间
// 被操作系统直接回收，析构函数一次执行的机会都没有。
//
// 为什么单独一个文件：worker_tag 是动态初始化的 thread_local，若与别的动态
// 初始化 thread_local 同处一个翻译单元，worker 对它的 odr-use 会把整个翻译
// 单元的推迟初始化一起结算（正文场景 2 讲的粒度），输出就多出来了。
// 另外本文件的 Tag 按正文本处输出块用无缩进打印（ctor worker_tag），与
// 02_thread_local.cpp 里带四空格缩进的场景 2 探针不是同一份，别混用。

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

struct Tag {
    const char* name;
    explicit Tag(const char* n) : name(n) { std::printf("ctor %s\n", n); }
    ~Tag() { std::printf("dtor %s\n", name); }
};

thread_local Tag worker_tag{"worker_tag"};   // Tag 与场景 2 的相同

int main()
{
    std::thread worker([] {
        (void)worker_tag;   // 触发本线程的构造
        std::this_thread::sleep_for(std::chrono::seconds(5));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::printf("main: 还没等 worker 醒，直接 std::exit\n");
    std::exit(0);
}
