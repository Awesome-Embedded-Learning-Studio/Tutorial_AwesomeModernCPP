// Standard: C++17
// std::reduce 执行策略的代码生成对比：seq vs par
// 只编译看汇编（allow-x86-asm，不运行故无需 -ltbb 链接）：
//   seq 版是一段标量累加循环；
//   par 版是一串 call __gnu_parallel / TBB 运行时——
// 这就是「libstdc++ 并行后端是 TBB」在汇编层的证据。
#include <execution>
#include <numeric>
#include <vector>

long sum_seq(const std::vector<long>& v) {
    return std::reduce(std::execution::seq, v.begin(), v.end(), 0L);
}

long sum_par(const std::vector<long>& v) {
    return std::reduce(std::execution::par, v.begin(), v.end(), 0L);
}

int main() {
    std::vector<long> v(1000, 1);
    volatile long sink = sum_seq(v) + sum_par(v);
    return sink != 0;
}
