// 04-vector/vector_demo.cpp —— E4:unique_fd 进 std::vector
//
// 演示与验证:
//   A) 不 reserve 直接 emplace_back x 8:扩容靠 move 搬迁,数一数搬了几次
//   B) std::shuffle:走的是 iter_swap -> swap,零次移动构造
//   C) std::sort 按 fd 排序:移动构造大量发生,fd 集合不变
//   D) 作用域结束:vector 析构,8 个 fd 全部 close,/proc/self/fd 回基线
//   E) reserve(8) 再 emplace_back:扩容次数为零 -> 移动次数为零
//
// 两版构建:默认 noexcept 移动 / -DRAII_MOVE_MAY_THROW 后非 noexcept 移动。
// 对比点:move-only 类型(vector<unique_fd>)扩容时无论如何都走 move,
// std::move_if_noexcept 只有在「可拷贝」时才退回拷贝;非 noexcept 损失的
// 是强异常保证,不是搬迁方式。两版输出应逐字段一致。
//
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I common 04-vector/vector_demo.cpp
//       g++ -std=c++20 -Wall -Wextra -O2 -DRAII_MOVE_MAY_THROW -I common
//           04-vector/vector_demo.cpp -o vector_throwing

#include "procfd.hpp"
#include "raii.hpp"

#include <algorithm>
#include <cstdio>
#include <random>
#include <type_traits>
#include <vector>

#ifdef RAII_MOVE_MAY_THROW
#    define RAII_MOVE_NOEXCEPT
#else
#    define RAII_MOVE_NOEXCEPT noexcept
#endif

// 计数用的观测子类:叠加一个移动构造计数,资源语义全在基类
class counted_fd : public unique_fd {
  public:
    explicit counted_fd(int fd = -1) noexcept : unique_fd(fd) {}

    counted_fd(counted_fd&& other) RAII_MOVE_NOEXCEPT : unique_fd(std::move(other)) { ++s_moves; }

    counted_fd& operator=(counted_fd&& other) RAII_MOVE_NOEXCEPT {
        unique_fd::operator=(std::move(other));
        return *this;
    }

    void swap(counted_fd& other) noexcept { unique_fd::swap(other); }
    friend void swap(counted_fd& a, counted_fd& b) noexcept { a.swap(b); }

    static int moves() noexcept { return s_moves; }
    static void reset_moves() noexcept { s_moves = 0; }

  private:
    static int s_moves;
};

int counted_fd::s_moves = 0;

static int open_null() {
    return sys_call("open", ::open, "/dev/null", O_RDONLY);
}

static void print_fds(const char* tag, const std::vector<counted_fd>& v) {
    std::printf("%s (%zu):", tag, v.size());
    for (const counted_fd& f : v) {
        std::printf(" %d", f.get());
    }
    std::printf("\n");
}

int main() {
    std::printf("counted_fd: copy_constructible=%d "
                "nothrow_move_constructible=%d (RAII_MOVE_MAY_THROW %s)\n",
                std::is_copy_constructible_v<counted_fd>,
                std::is_nothrow_move_constructible_v<counted_fd>,
#ifdef RAII_MOVE_MAY_THROW
                "defined"
#else
                "not defined"
#endif
    );

    const auto baseline = list_open_fds();

    {
        std::printf("== A) emplace_back x 8, no reserve ==\n");
        std::vector<counted_fd> v;
        for (int i = 0; i < 8; ++i) {
            v.emplace_back(open_null());
        }
        std::printf("   size=%zu capacity=%zu moves=%d\n", v.size(), v.capacity(),
                    counted_fd::moves());
        print_fds("   fds", v);

        std::vector<int> fd_set;
        for (const counted_fd& f : v) {
            fd_set.push_back(f.get());
        }
        std::sort(fd_set.begin(), fd_set.end());

        std::printf("== B) shuffle (mt19937, fixed seed) ==\n");
        counted_fd::reset_moves();
        std::shuffle(v.begin(), v.end(), std::mt19937{20261002});
        print_fds("   fds", v);
        std::printf("   moves during shuffle=%d (swap moves no bytes)\n", counted_fd::moves());

        std::printf("== C) sort by fd ==\n");
        counted_fd::reset_moves();
        std::sort(v.begin(), v.end(),
                  [](const counted_fd& a, const counted_fd& b) { return a.get() < b.get(); });
        print_fds("   fds", v);
        std::printf("   moves during sort=%d\n", counted_fd::moves());

        std::vector<int> fd_set2;
        for (const counted_fd& f : v) {
            fd_set2.push_back(f.get());
        }
        std::sort(fd_set2.begin(), fd_set2.end());
        std::printf("   fd multiset unchanged: %s\n", fd_set == fd_set2 ? "yes" : "NO");

        std::printf("== D) scope end: vector destructs, 8 fds close ==\n");
        std::fflush(stdout);
    }

    const auto after = list_open_fds();
    std::printf("fds: before=%zu after=%zu (back to baseline: %s)\n", baseline.size(), after.size(),
                baseline == after ? "yes" : "NO");

    {
        std::printf("== E) reserve(8) first, then emplace_back x 8 ==\n");
        counted_fd::reset_moves();
        std::vector<counted_fd> v;
        v.reserve(8);
        for (int i = 0; i < 8; ++i) {
            v.emplace_back(open_null());
        }
        std::printf("   size=%zu capacity=%zu moves=%d\n", v.size(), v.capacity(),
                    counted_fd::moves());
        print_fds("   fds", v);
    }

    std::printf("== done ==\n");
}
