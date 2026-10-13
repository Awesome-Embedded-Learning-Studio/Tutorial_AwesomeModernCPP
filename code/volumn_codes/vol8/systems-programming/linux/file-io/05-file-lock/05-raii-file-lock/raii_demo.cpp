// e5_raii_demo.cpp —— file_lock 双进程演示(文章《文件锁》E5)
//
// 时序剧本:A(父)构造 file_lock(拿到写锁)→ 写数据 → 睡 700 ms → 作用域结束;
// B(子,defer_lock 起手):
//   try_lock()            → 立刻 false(锁在 A 手里)
//   try_lock_for(200 ms)  → 超时 false,打出实际等待时长
//   try_lock_for(3 s)     → 在 A 放锁瞬间拿到,读出 A 写的数据
// 另附 move 语义:moved-from 对象析构不放锁,锁跟着新主人走。
#include "file_lock.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>

#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const char* path = "/home/charliechen/l05_scratch/e5/raii.bin";

const auto t0 = std::chrono::steady_clock::now();

long ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 t0)
        .count();
}

// 竞争者探针:自己 open,flock 非阻塞试锁
void probe(const char* tag) {
    pid_t pid = ::fork();
    if (pid == 0) {
        unique_fd own{sys_call("open", ::open, path, O_RDWR)};
        int r = ::flock(own.get(), LOCK_EX | LOCK_NB);
        std::printf("  [%6ld ms] 探针(%s):flock = %d%s\n", ms(), tag, r,
                    r == -1 ? "(锁被占)" : "(拿到锁)");
        ::_exit(0);
    }
    int st = 0;
    ::waitpid(pid, &st, 0);
}

void scene_two_processes() {
    std::printf("\n==== 双进程时序:A 拿写锁写数据,B 有限等待接棒 ====\n");
    file_lock a{path}; // 构造 = open + 阻塞加锁
    const char msg[] = "ticket=42";
    sys_call("pwrite", ::pwrite, a.fd(), msg, sizeof msg, 0);
    std::printf("  [%6ld ms] A(pid=%d):构造 file_lock,已写 \"%s\",持锁 700 ms\n", ms(), ::getpid(),
                msg);

    pid_t b = ::fork();
    if (b == 0) { // B:defer_lock 起手,三种拿锁姿势各试一遍
        file_lock lk{path, defer_lock};

        if (lk.try_lock()) {
            std::printf("  [%6ld ms] B:try_lock 意外成功?!\n", ms());
        } else {
            std::printf("  [%6ld ms] B(pid=%d):try_lock() = false(锁在 A 手里)\n", ms(),
                        ::getpid());
        }

        auto t1 = std::chrono::steady_clock::now();
        bool ok = lk.try_lock_for(std::chrono::milliseconds(200));
        auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t1)
                          .count();
        std::printf("  [%6ld ms] B:try_lock_for(200ms) = %s(实际等了 %ld ms,超时)\n", ms(),
                    ok ? "true" : "false", waited);

        t1 = std::chrono::steady_clock::now();
        ok = lk.try_lock_for(std::chrono::seconds(3));
        waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - t1)
                     .count();
        std::printf("  [%6ld ms] B:try_lock_for(3s) = %s(等了 %ld ms —— A 一放锁就拿到)\n", ms(),
                    ok ? "true" : "false", waited);

        char buf[16]{};
        sys_call("pread", ::pread, lk.fd(), buf, sizeof buf - 1, 0);
        std::printf("  [%6ld ms] B:读到 \"%s\"(临界区数据完好)\n", ms(), buf);
        ::_exit(0); // lk 析构:放锁 + close
    }

    ::usleep(700 * 1000);
    std::printf("  [%6ld ms] A:作用域将尽,file_lock 析构在即\n", ms());
    // a 的析构在这里发生:LOCK_UN + close
    a.reset();
    std::printf("  [%6ld ms] A:已放锁\n", ms());
    int st = 0;
    ::waitpid(b, &st, 0);
}

void scene_move() {
    std::printf("\n==== move 语义:锁跟着新主人走,moved-from 析构不放锁 ====\n");
    file_lock lk2; // 空对象
    {
        file_lock lk1{path};
        probe("lk1 持锁中");
        lk2 = std::move(lk1); // 所有权移交
        std::printf("  [%6ld ms] move 后:lk1 还活着吗?%s;lk2 持有 fd=%d\n", ms(),
                    lk1 ? "是" : "否(moved-from)", lk2.fd());
    } // lk1(moved-from)析构:不许碰 lk2 的锁
    probe("lk1 析构后"); // 仍被占 → moved-from 析构确实没放锁
    lk2.reset();         // 真正的主人放锁
    probe("lk2 reset 后");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    { // 清场
        unique_fd fd{sys_call("open", ::open, path, O_RDWR | O_CREAT | O_TRUNC, 0666)};
    }
    std::printf("pid=%d, 文件:%s(ext4)\n", ::getpid(), path);
    scene_two_processes();
    scene_move();
    return 0;
}
