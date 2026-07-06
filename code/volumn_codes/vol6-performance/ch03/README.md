# vol6 ch03 · 归因方法论 — 代码示例

对应文章:`documents/vol6-performance/ch03-attribution-methodology/`

ch03 是方法论章,大部分工具(`perf` / `toplev` / 火焰图 / eBPF)是系统级 profiler,**本机 WSL2 没装这些**(文章里如实标注,profiler 输出引自 Bakhvalov/easyperf)。所以本章代码只有两个,**强调诚实**:

- `roofline.cpp` —— **能本机跑**:解析地算 dot / axpy / 加权点积 / 矩阵乘 的算术强度,标注每个落在 Roofline 哪一侧。纯算术,验证 ch03-01 的判读逻辑。
- `profile-commands.sh` —— **命令速查参考,不是可跑脚本**:列出 USE / perf / toplev / off-CPU / COZ 的标准命令,每段标注场景和对应文章。在装好工具的裸机 Linux 上逐段用。

## 构建

```bash
# roofline(本机可跑)
g++ -O2 -std=c++17 roofline.cpp -o roofline && ./roofline

# profile-commands.sh 不构建,直接阅读 / 在目标环境逐段复制
```

## 怎么读

- `roofline` 输出会告诉你 dot/axpy 是**带宽受限**(AI ~0.2,远低于脊点 ~6)、矩阵乘是**算力受限**(AI 高)。这直接对应 ch03-01 的优化方向判读:带宽受限减访存,算力受限加 SIMD。
- `profile-commands.sh` 是你拿到一台装好 perf 的 Linux 上的工作手册。先 USE 排除系统问题,再 perf 火焰图看代码,toplev 下钻流水线,off-CPU 看等待。
