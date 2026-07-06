# vol6 ch05 · 多核性能 — 代码示例

对应文章:`documents/vol6-performance/ch05-multicore-performance/`

三个程序,对应 ch05 三篇。同步原语的**正确性/内存序/无锁实现归 vol5**,这里只测**性能成本**。

## 构建

```bash
g++ -O2 -std=c++17 -pthread false_sharing.cpp -o false_sharing && ./false_sharing
g++ -O2 -std=c++17 -pthread scalability.cpp   -o scalability   && ./scalability
g++ -O2 -std=c++17 -pthread lock_cost.cpp     -o lock_cost     && ./lock_cost
# 或 cmake -B build && cmake --build build
```

## 程序对照

| 程序 | 文章 | 核心数字 |
|---|---|---|
| `false_sharing` | 05-01 | 伪共享 vs `alignas(64)`:**18×** |
| `scalability` | 05-02 | 1→2→4→8 线程:1→1.70→2.33→2.53×(亚线性,内存带宽天花板)|
| `lock_cost` | 05-03 | mutex/atomic_relaxed **3.6×**(无竞争);非原子基线 0 ns |

## 局限(诚实)

- **NUMA 跨节点惩罚测不了**:本机 WSL2 单 socket 单 NUMA 节点(`numactl --hardware` 只 node0)。05-02 的 NUMA 内容引自 Bakhvalov + 多 socket 服务器实践,要测得找双路机器。
- **扩展性曲线在 WSL2 上**:5800H 是 7 物理核 / 14 线程,但 WSL2 的 CPU 暴露可能和裸机不同;加速比数字随环境变,**关心「亚线性拐点」这个趋势,不关心某个具体倍数**。
- `atomic relaxed` vs `seq_cst` 单线程几乎一样(1.86 vs 1.84 ns)——它们的差别在跨线程 ordering,单线程看不出。
