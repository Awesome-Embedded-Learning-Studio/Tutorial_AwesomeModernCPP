// 03-release/release_reset_swap.cpp —— E3:归还语义三件套
//
//   release()  放弃所有权:fd 交还调用方,析构不再 close(strace 里该 fd
//              只有调用方自己那次 close)
//   reset(new) 先 close 手里的旧 fd,再接管新 fd;无参 reset = close + 变空
//   swap()     双方角色互换,fd 编号跟所有权走、不跟变量名走
//
// 用 /dev/null:open 永远成功,又不占盘。fd 编号见输出,close 的时序与
// 归属见 strace_release.out。
//
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I common 03-release/release_reset_swap.cpp

#include "raii.hpp"

#include <cstdio>

static int open_null(const char* tag) {
    const int fd = sys_call("open", ::open, "/dev/null", O_RDONLY);
    std::printf("[open     ] %s -> fd %d\n", tag, fd);
    std::fflush(stdout);
    return fd;
}

static const char* holds(const unique_fd& u) {
    return u ? "something" : "nothing";
}

int main() {
    std::printf("== 1) release(): give up ownership ==\n");
    std::fflush(stdout);
    {
        unique_fd a{open_null("a")};
        const int raw = a.release();
        std::printf("[release  ] a gave up fd %d, a now holds %s\n", raw, holds(a));
        std::fflush(stdout);
        // 从这里起这个 fd 的死活归调用方:要 close 自己 close,不 close
        // 就是故意泄漏。析构不会再碰它
        ::close(raw);
        std::printf("[manual   ] ::close(%d) done — the only close it gets\n", raw);
        std::fflush(stdout);
    } // ~unique_fd():a 是空的,什么也不做

    std::printf("== 2) reset(new_fd): close old, take new ==\n");
    std::fflush(stdout);
    {
        unique_fd b{open_null("b")};
        const int c = open_null("c");
        b.reset(c);
        std::printf("[reset(new)] b closed its old fd and now holds %d\n", b.get());
        std::fflush(stdout);
        b.reset();
        std::printf("[reset()  ] b.reset() -> b now holds %s\n", holds(b));
        std::fflush(stdout);
    }

    std::printf("== 3) swap(): exchange roles ==\n");
    std::fflush(stdout);
    {
        unique_fd x{open_null("x")};
        unique_fd y{open_null("y")};
        x.swap(y);
        std::printf("[swap     ] x holds %d, y holds %d\n", x.get(), y.get());
        std::fflush(stdout);
    } // 析构顺序与声明相反:y 先走,再 x;strace 里 close 的先后可对

    std::printf("== done ==\n");
}
