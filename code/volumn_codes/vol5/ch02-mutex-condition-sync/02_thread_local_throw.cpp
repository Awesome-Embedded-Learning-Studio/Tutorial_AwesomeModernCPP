// 02_thread_local_throw.cpp —— 《thread_local：每线程一份的世界》构造抛异常实测
//
// 正文出处：documents/vol5-concurrency/ch02-mutex-condition-sync/02-thread-local.md
// 「构造抛异常：条文与实测」一节。[basic.start.dynamic] 第 8 段说非块级变量的
// 初始化以异常退出时标准给的后果是 std::terminate；GCC 16 的实测行为存在出入：
// 推迟的初始化发生在线程第一次 odr-use 的现场，异常从那里抛出来——
//   包在 try/catch 里：接得住，进程活下来（本文件 caught 场景，退出码 0）
//   裸 odr-use 逃逸：进程以退出码 134 终止（本文件 escape 场景）
//
// 编译：g++ -std=c++20 -Wall -Wextra -pedantic -pthread -o 02_thread_local_throw 02_thread_local_throw.cpp
//
// 用法（两个场景必须分两次进程跑，原因见文件尾的注）：
//   ./02_thread_local_throw caught    # 第一次 odr-use 包在 try 里（默认场景）
//   ./02_thread_local_throw escape    # 第一次 odr-use 裸奔 -> std::terminate -> 134

#include <cstdio>
#include <stdexcept>
#include <string_view>

struct Boom {
    Boom() { throw std::runtime_error("线程局部对象的构造炸了"); }
};

thread_local Boom boom;   // 命名空间作用域：动态初始化推迟到本线程第一次 odr-use

int main(int argc, char** argv)
{
    const std::string_view scenario = argc > 1 ? argv[1] : "caught";

    if (scenario == "escape") {
        (void)boom;   // 裸 odr-use：异常直接逃逸 -> std::terminate -> abort（134）
        return 0;
    }

    try {
        (void)boom;   // 第一次 odr-use：推迟的初始化在这里现场抛出
    } catch (const std::exception& e) {
        std::printf("接住了：%s\n", e.what());
    }
    std::printf("进程活了下来，退出码 0\n");
    return 0;
}

// 注：为什么两个场景要分两次进程跑——GCC 的 __tls_guard 在构造之前就置位
// （正文汇编节可见：置位 guard 的指令排在调用构造函数之前），构造抛了异常
// 不算初始化完成，但 guard 已经立起来：同一进程里第二次 odr-use 既不会再试、
// 也不会再抛。所以在同一进程里先 caught 再 escape 是跑不出 134 的，只能分开。
// 条文与实测的出入正文如实摆着，裁决不做；给 thread_local 对象写不抛异常的
// 构造函数，两种解释的分歧就与您无关了。
