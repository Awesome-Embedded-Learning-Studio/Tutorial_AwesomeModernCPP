// lock_scope_tracker.cpp -- if 初始化器里 RAII 对象的持锁范围追踪
// Standard: C++17

#include <cstdio>

struct LockTracker {
    LockTracker() { std::puts("  >> 锁获取"); }
    ~LockTracker() { std::puts("  << 锁释放"); }
};

int main() {
    std::puts("进入 if/else 块");
    if (LockTracker lock; false) {
        // if 分支，不执行
    } else {
        std::puts("else 分支执行中（此时锁仍被持有）");
    }
    std::puts("已离开 if/else 块");
    return 0;
}
