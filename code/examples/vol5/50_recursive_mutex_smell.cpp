// recursive_mutex 能放行同一线程的重复拿锁，但它只是把症状压住了；
// 对照的重构版：拆出「假设调用方已持锁」的私有实现，一把锁只拿一次
#include <iostream>
#include <mutex>
#include <thread>

std::recursive_mutex recursive_m;
std::mutex plain_m;
int data_a = 0;
int data_b = 0;

// 递归版：walk 自己拿锁，又调用拿同一把锁的 step —— recursive_mutex 放行
void step_recursive(int& out) {
    std::lock_guard<std::recursive_mutex> lk(recursive_m);
    out += 1;
}

void walk_recursive(int depth, int& out) {
    std::lock_guard<std::recursive_mutex> lk(recursive_m);
    step_recursive(out); // 同一把锁，同一线程，第二次拿
    if (depth > 0) {
        walk_recursive(depth - 1, out);
    }
}

// 重构版：公开入口拿一次锁，内部的 _step 假设调用方已持锁
void step_locked(int& out) {
    out += 1; // 不拿锁：调用方已经拿了
}

void walk_refactored(int depth, int& out) {
    std::lock_guard<std::mutex> lk(plain_m);
    step_locked(out);
    for (int i = 0; i < depth; ++i) {
        step_locked(out);
    }
}

int main() {
    walk_recursive(2, data_a); // 三层递归，每层都进了临界区
    std::cout << "递归版：三层都进了临界区，data_a = " << data_a << "\n";

    walk_refactored(2, data_b); // 一把锁，只拿一次
    std::cout << "重构版：一把锁只拿一次，data_b = " << data_b << "\n";
    return 0;
}
