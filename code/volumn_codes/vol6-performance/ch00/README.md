# vol6 ch00 · 性能思维 — 代码示例

对应文章:`documents/vol6-performance/ch00-performance-mindset/01-efficiency-vs-performance.md`

## vector_vs_set

验证本卷开篇命题 **efficiency ≠ performance**:`std::vector` + `std::lower_bound`(二分查找)与 `std::set::find` 都是 $O(\log n)$,但在真实硬件上,当 N 超出缓存后,连续内存的 `vector` 把节点分散的 `set` 甩开好几倍。

### 构建

```bash
# 直接编译(最快)
g++ -O2 -std=c++17 vector_vs_set.cpp -o vector_vs_set
./vector_vs_set

# 或用 CMake
cmake -B build && cmake --build build && ./build/vector_vs_set
```

### 怎么读结果

关心**趋势**(N 增大后 `set/vector` 比值上升)和**命题**(同复杂度差几倍),不要把某个具体倍数当普适结论。绝对数字随 CPU / 编译器 / libc++ 实现而变。

代码里几处防失真细节(`volatile global_sink` 防死代码消除、全部命中消除偏差、多轮取中位数压离群值)是 vol6 ch01 *Benchmark 方法论* 的伏笔,文章里逐条有讲。
