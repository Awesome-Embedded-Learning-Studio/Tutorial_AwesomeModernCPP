# vol6 ch06 · C++ 抽象的性能成本 — 代码示例

对应文章:`documents/vol6-performance/ch06-cpp-abstraction-cost/`

四个程序,对应 ch06 的几篇。**核心精神:每个 C++ 抽象都对应一个硬件成本,但「有成本」不等于「每次都发生」——编译器常替你消除(去虚化、零成本异常、RVO)。先测再优化。**

## 构建

```bash
g++ -O2 -std=c++20 exception_cost.cpp   -o exception_cost   && taskset -c 0 ./exception_cost
g++ -O2 -std=c++20 function_sbo.cpp     -o function_sbo     && taskset -c 0 ./function_sbo
g++ -O2 -std=c++20 abstraction_sizeof.cpp -o abstraction_sizeof && ./abstraction_sizeof
g++ -O2 -std=c++20 rvo_move.cpp         -o rvo_move         && ./rvo_move
# 对照:关掉 copy elision 看 RVO 消失的样子
g++ -O2 -std=c++20 -fno-elide-constructors rvo_move.cpp -o rvo_move_noelide && ./rvo_move_noelide
# 或 cmake -B build && cmake --build build
```

> 06-01 虚函数与去虚拟化的实验(`virtual_devirt.cpp`)与 ch04-04 共用,在 `code/.../ch04/`。

## 程序对照

| 程序 | 文章 | 核心数字 |
|---|---|---|
| `exception_cost` | 06-02 | 正常路径 **0.25 ns(零成本,和纯函数一样)**;throw+catch **857 ns(~3400×)** |
| `function_sbo` | 06-03 | 调用 function 比直接 lambda 慢 **~6×**;构造 SBO 2.3ns / 堆分配 19.6ns(**8.5×**)|
| `abstraction_sizeof` | 06-04 | optional/variant/span/string_view/string/shared_ptr 的 sizeof |
| `rvo_move` | 06-05 | **copy/move 计数**(编译器打不穿):URVO/NRVO=0/0,`std::move`=0/1,copy=1/0 |

## 两个诚实点

- **rvo_move 计时版会被编译器打穿**(copy elision 跨迭代优化让 copy 看起来比 RVO 快),所以改用 **copy/move 构造函数计数**——直接数发生几次拷贝/移动,编译器改不了你的计数器。这是 RVO 教学唯一可靠的方法。
- `abstraction_sizeof` 的数字是 libstdc++ C++20 实现;libc++/MSVC 会不同(`string` 32B vs 48B 等),**关心 sizeof 时以你自己的工具链为准**。
