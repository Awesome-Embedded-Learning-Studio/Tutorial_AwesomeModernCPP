// size_demo.cpp — vol6 ch07-04 体积优化
// 含一些未被调用的函数(死代码)+ 模板多实例,用于演示 --gc-sections / 模板膨胀控制。
// 编译对照(看二进制大小):
//   g++ -O2 size_demo.cpp -o size_o2
//   g++ -Os size_demo.cpp -o size_os
//   g++ -O2 -ffunction-sections -fdata-sections size_demo.cpp -o size_gc -Wl,--gc-sections
//   size size_o2 size_os size_gc
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// 死代码:这个函数从不被调用,-ffunction-sections + --gc-sections 能回收
[[maybe_unused]] int big_dead_function() {
    int s = 0;
    for (int i = 0; i < 1000; ++i)
        s += i * (i + 1);
    return s;
}
[[maybe_unused]] double another_dead(double x) {
    return x * 3.14 + 2.71;
}

// 模板:对不同类型各实例化一份代码
template <class T> T compute(T a, T b) {
    T s = a;
    for (int i = 0; i < 10; ++i) {
        s = s * b + a;
    }
    return s;
}

int main() {
    // 用到的实例:int, double,long —— 3 份代码
    volatile int a = compute(2, 3);
    volatile double b = compute(2.0, 3.0);
    volatile long c = compute(2L, 3L);
    (void)a;
    (void)b;
    (void)c;
    std::vector<int> v = {3, 1, 4, 1, 5, 9, 2, 6};
    std::sort(v.begin(), v.end());
    std::printf("size demo: v[0]=%d (看二进制大小,不看这行输出)\n", v[0]);
    return 0;
}
