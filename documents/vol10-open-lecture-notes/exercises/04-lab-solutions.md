---
title: "卷 10 Lab 实验参考"
description: "卷 10 Lab（零开销安检台）的实验参考：六个步骤加 L5 SSE 挑战的逐步解答，每步标注知识点链接，所有输出在 WSL Arch（g++ 16.1.1）真实运行得到；步骤 3 的假阳性做了「我们的形态 vs 讲座原形态」对照，L5 指出讲座内联汇编在 x86-64 无法汇编并给出修正版，均已如实标注。"
chapter: 10
order: 4
tags: [host, advanced, cpp-modern, 调试, 优化, 类型安全, Ranges]
difficulty: advanced
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 17
prerequisites: []
related: []
---

# 卷 10 Lab 实验参考

> 所有输出在 WSL Arch（g++ 16.1.1）真实运行得到。建议卡住时先看「思路」逐步对照。有两处与讲座原文表述相关的实测结果需要先说清楚（步骤 3 的假阳性随代码形态变化、L5 讲座内联汇编在 x86-64 无法汇编），都已在对应步骤里如实说明——这正是这个 Lab 想教你的：别信口头结论，跑一遍。

## 步骤 1：迭代器类别双探针 {#lab-1}

**题面见 [03-lab](./03-lab.md#lab-1)**

**思路**：`legacy_tag` 走 `iterator_traits<Iter>::iterator_category` 的继承链，`cpp20_concept` 走一组正交 concept 的 `if constexpr` 探测。两套体系唯一的系统性分歧就是「连续」这个档位。

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -O2 itercat.cpp -o itercat && ./itercat
std::array<int,5>            legacy=random_access  cpp20=contiguous_iterator
std::vector<int>             legacy=random_access  cpp20=contiguous_iterator
std::string                  legacy=random_access  cpp20=contiguous_iterator
std::deque<int>              legacy=random_access  cpp20=random_access_iterator
std::list<int>               legacy=bidirectional  cpp20=bidirectional_iterator
std::forward_list<int>       legacy=forward        cpp20=forward_iterator
std::map<int,int>            legacy=bidirectional  cpp20=bidirectional_iterator
int* (raw pointer)           legacy=random_access  cpp20=contiguous_iterator
```

不一致的行有三处：`array`/`vector`/`string` 和裸指针的 legacy tag 都是 `random_access`，concept 却是 `contiguous_iterator`。旧 tag 体系里根本没有「连续」这个档位（`contiguous_iterator_tag` 是 C++20 才加的），所以 C++20 之前 `int*` 只能被笼统标成随机访问。→ 知识点：[从循环到迭代器](../cppcon/2025/03-back-to-basics-ranges/01-from-loops-to-iterators.md)「迭代器类别体系」一节（deque 分块存储、concepts 是正交约束）

`deque` 的答案：它支持 `it + n`（随机可访问），但内部是一段一段的分块存储，物理上不连续——所以「随机可访问」≠「连续」，两个性质是独立的。

## 步骤 2：ABI 读汇编 {#lab-2}

**题面见 [03-lab](./03-lab.md#lab-2)**

**思路**：`-O2 -S` 输出去掉 `.cfi_*` 后每个函数只剩骨架；System V 下前六个整数参数 rdi/rsi/rdx/rcx/r8/r9，第七个上栈且偏移从 8 开始（`[rsp]` 是返回地址）。

**验证输出**（`g++ -std=c++20 -O2 -S abi.cpp`，去噪后）：

```asm
_Z6squarei:
    imull   %edi, %edi
    movl    %edi, %eax
    ret
_Z9add_threelll:
    addq    %rsi, %rdi
    leaq    (%rdi,%rdx), %rax
    ret
_Z9sum_sevenlllllll:
    addq    %rsi, %rdi
    addq    %rdx, %rdi
    addq    %rcx, %rdi
    addq    %r8, %rdi
    leaq    (%rdi,%r9), %rax
    addq    8(%rsp), %rax
    ret
main:
    movl    $1156, %eax
    ret
```

1. `square`：`imull %edi, %edi` 两操作数形式把结果写回第一个操作数（`edi`），返回值必须在 `eax`，所以那条 `movl` 省不掉。→ 知识点：[阅读汇编与寄存器 ABI](../cppcon/2025/02-some-assembly-required/02-reading-assembly-and-registers-abi.md)「x86-64 的版本」一节
2. 第七个参数在 `8(%rsp)`：`call` 把返回地址压进 `[rsp]`，栈上第一个参数从偏移 8 开始。→ 知识点：同上「动手验证一下」一节
3. `main` 被折叠成 `movl $1156, %eax`（28+6=34、34²=1156）——这就是「看优化必须开 -O2」的原因。→ 知识点：同上「优化等级会彻底改变你看到的东西」一节

## 步骤 3：位查找表的守卫 {#lab-3}

**题面见 [03-lab](./03-lab.md#lab-3)**

**思路**：无守卫版的行为**取决于代码形态**——本实验把两种形态都跑了。直接把 `'5'`/`'a'`/`'p'` 写成常量实参调用（我们的形态）时，g++ 在 `-O2` 下把 UB 的常量移位折叠掉了，`'p'` 返回 0、没有出现假阳性；换成讲座原形态（循环里喂运行时字符值，移位真正在运行期执行）后，`'p'`..`'y'` 照常被误判成数字，讲座描述的假阳性完整复现（clang++ 同）。UB 随代码生成变化，正是本题要你看的东西。

**关键源码**（位表构造同讲座：bit 48..57 置位）：

```cpp
// 无守卫版:直接拿 uc 当移位量,uc >= 64 时是 UB
bool is_digit_unguarded(char c)
{
    unsigned char uc = static_cast<unsigned char>(c);
    return (kDigitTable >> uc) & 1;
}
// 守卫版:先拦下 uc >= 64
bool is_digit_guarded(char c)
{
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 64)
    {
        return false;
    }
    return (kDigitTable >> uc) & 1;
}

int main()
{
    // 我们的形态:直接常量调用——编译器在 -O2 下把 UB 的常量移位折叠掉了
    std::printf("直接常量调用(我们的形态): '5'=%d 'a'=%d 'p'=%d\n",
                is_digit_unguarded('5'), is_digit_unguarded('a'), is_digit_unguarded('p'));
    // 讲座原形态:循环里的运行时值——移位在运行期执行,走 x86 硬件掩码路径
    for (int i = 32; i < 127; ++i)
    {
        char c = static_cast<char>(i);
        if (is_digit_unguarded(c) != is_digit_naive(c))
        {
            std::printf("讲座原形态假阳性: '%c' (ASCII %d): bitlookup=%d naive=%d\n",
                        c, i, is_digit_unguarded(c), is_digit_naive(c));
        }
    }
    // 守卫版对 ASCII 0..127 全量对照朴素写法,全部一致
}
```

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -O2 diglook.cpp -o diglook && ./diglook
直接常量调用(我们的形态): '5'=1 'a'=0 'p'=0
讲座原形态假阳性: 'p' (ASCII 112): bitlookup=1 naive=0
讲座原形态假阳性: 'q' (ASCII 113): bitlookup=1 naive=0
（… 'r' 到 'y' 同,共 10 个假阳性 …）
守卫版 ASCII 0..127 全对: yes
$ clang++ -std=c++20 -Wall -Wextra -O2 diglook.cpp -o diglook_clang && ./diglook_clang
直接常量调用(我们的形态): '5'=1 'a'=-1785794040 'p'=-585011800
讲座原形态假阳性: 'p' (ASCII 112): bitlookup=1 naive=0
（… 同 g++,共 10 个假阳性 …）
守卫版 ASCII 0..127 全对: yes
$ g++ -std=c++20 -O1 -g -fsanitize=undefined -fno-sanitize-recover=all \
      diglook.cpp -o diglook_ub && ./diglook_ub
diglook.cpp:20:25: runtime error: shift exponent 112 is too large for 64-bit type 'long unsigned int'
```

1. 普通构建（g++）：**我们的形态下未复现**——常量实参让编译器在 `-O2` 下把 `kDigitTable >> 112` 这个 UB 移位折叠成 0，`'p'` 返回 0；**讲座原形态照常复现**——循环里的运行时移位走 x86 硬件「移位量掩码 6 位」的路径，`112 & 63 = 48` 恰好命中 bit 48（`'0'`），`'p'`..`'y'` 全被误判。同一份源码、同一个编译器，两种调用形态给出两种答案——这正是「UB 随代码生成变化」的活标本。→ 知识点：[Compiler Explorer 与 AI 辅助](../cppcon/2025/02-some-assembly-required/03-compiler-explorer-and-ai-assisted.md)「位查找表技巧的原理」一节（含讲座自己标注的「x86 掩码行为是 UB、不可依赖」）
2. clang++ 交叉验证：讲座原形态同样复现假阳性；我们的形态下 clang 把常量移位折叠成了**未定义值**——`'a'`/`'p'` 打印出一串垃圾数，而且每次运行数值都不一样，连布尔返回值 0/1 的规范化都不保证。→ 知识点：同上（多编译器交叉验证：同一 UB 在 GCC 和 Clang 下的表现可以完全不同）
3. UBSan 构建：`shift exponent 112 is too large` 精确到行列——`'p'`（112）的移位量越界被抓现行（`printf` 实参求值顺序未指定，本机 g++ 从右往左，`'p'` 先被求值、先触发；libubsan 对同一源码位置的重复报告默认只报一次，所以 `'a'`=97 与循环里的 64..121 都被去重吞掉）。`-fno-sanitize-recover=all` 下直接终止，前面的 `printf` 因缓冲未刷新没显示（信号终止不冲 stdout，这是个经典小坑）。→ 知识点：[WG21 标准化与 x86/RISC-V 汇编哲学](../cppcon/2025/02-some-assembly-required/07-wg21-standardization-and-assembly-philosophy.md)「应对策略：不猜，用工具」一节
4. 加 `uc >= 64` 守卫后全对。结论：**别拿「x86 会掩码移位量」当依据，那是不保证的**；守卫是唯一正确的写法。→ 知识点：[Compiler Explorer 与 AI 辅助](../cppcon/2025/02-some-assembly-required/03-compiler-explorer-and-ai-assisted.md)（位移量超位宽是 UB）

## 步骤 4：checked_span 与负数下标 {#lab-4}

**题面见 [03-lab](./03-lab.md#lab-4)**

**思路**：下标用 `ptrdiff_t`，负数在第一步就被抓住；CTAD 指引让 `checked_span s(data, 5)` 自动推出 `checked_span<int>`。

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra cspan.cpp -o cspan && ./cspan
s[2] = 30 (size=5)
改写后 s[0] = 42
s[10] 捕获: 下标越界了兄弟
s[-3] 捕获: 负数下标,你想干嘛
```

1. `operator[]` 先查 `index < 0` 再查上界——用 `size_t` 的话 -3 会被隐式转成天文数字，要么读到垃圾要么报一个误导性的错。→ 知识点：[类型安全、Number 约束与边界检查](../cppcon/2025/01-concept-based-generic-programming/01-type-safety-and-number-concept.md)「负数下标的问题——有符号无符号的坑」一节
2. CTAD 指引（`checked_span(T*, std::size_t) -> checked_span<T>`）让编译器从实参推导元素类型，少写一处重复信息，改类型时只改一处。→ 知识点：同上「类型推导：别再重复编译器已经知道的事」一节

## 步骤 5：惰性管道与短路 {#lab-5}

**题面见 [03-lab](./03-lab.md#lab-5)**

**思路**：恒真谓词下，`take(5)` 的谓词调用次数就是「找首元素 + 5 次推进 + 最后一次发现耗尽」——个位数，与讲座实测一致。

**验证输出**：

```text
$ g++ -std=c++23 -Wall -Wextra -O2 lazybench.cpp -o lazybench && ./lazybench
filter 谓词调用次数: 全量=10000000  加 take(5)=6
sum eager=37499992500000 lazy=37499992500000
eager (ranges::to + 求和): 14 ms
lazy  (直接遍历 view):    4 ms
```

1. 恒真谓词 + `take(5)` 只有 6 次调用：`begin()` 找首个元素 1 次、每次 `++` 1 次、第 5 个元素后再 `++` 一次发现计数耗尽——一千万次求值被惰性短路到 6 次。→ 知识点：[Ranges、Views 与管道组合](../cppcon/2025/03-back-to-basics-ranges/03-ranges-views-and-composition.md)「管道短路：lazy 带来的效率」一节
2. eager 物化 14ms vs lazy 4ms，快 3 倍多且不分配临时容器；两者和一致（37499992500000）。→ 知识点：同上「实验：eager vs lazy，到底差多少」一节

## 步骤 6：noexcept 决定 vector 扩容路径 {#lab-6}

**题面见 [03-lab](./03-lab.md#lab-6)**

**思路**：`vector` 的强异常安全保证要求扩容失败可回滚；移动是破坏性的（资源被偷走后回不去），所以只有 `noexcept` 移动构造才被允许上扩容路径。

**验证输出**：

```text
$ g++ -std=c++17 -O2 noexceptv.cpp -o noex0 && ./noex0
reserve(2) 塞 3 个后: copies=2 moves=0 (元素=AAA BBB CCC)
$ g++ -std=c++17 -O2 -DNOEXCEPT noexceptv.cpp -o noex1 && ./noex1
reserve(2) 塞 3 个后: copies=0 moves=2 (元素=AAA BBB CCC)
```

1. 无 `noexcept`：扩容 2 → 4 时前两个元素走拷贝（2 次拷贝、0 次移动）。→ 知识点：[移动操作、std::move 与拷贝消除](../cppcon/2025/04-back-to-basics-move-semantics/03-move-ops-stdmove-and-elision.md)「noexcept 的重要性：vector 扩容的隐藏陷阱」一节
2. 加 `noexcept`：2 次移动、0 次拷贝。理由链：移动中途抛异常 → 已移动元素无法恢复 → 强异常安全保证破产 → 所以 `vector` 宁可用拷贝（原数据还在、可回滚）。→ 知识点：同上

## 附加挑战（L5）：手写 SSE 对决编译器 {#lab-l5}

**题面见 [03-lab](./03-lab.md#lab-l5)**

**思路**：`abs(x) = (x ^ mask) - mask`，其中 `mask = x >> 31`（算术右移做符号扩展）。SSE2 一次处理 4 个 `int32`；`-O2` 下编译器不自动向量化，手写版有优势；`-O3 -march=x86-64-v2` 下编译器自己用 `pabsd` 追平。数据要排除 `INT_MIN`（`abs(INT_MIN)` 本身是 UB）。

**先说一个必须修的坑：讲座 abs_array 的内联汇编在 x86-64 上无法汇编**。讲座原代码用 `%%eax` 作寻址索引（`movdqu (%1, %%eax, 4), %%xmm0`），但 x86-64 寻址的 base/index 寄存器必须是 64 位，GAS 直接报错：

```text
$ g++ -std=c++20 -O2 -Wall -Wextra -c lec_asm_fail.cpp -o /dev/null
lec_asm_fail.cpp:10: Error: `(%rsi,%eax,4)' is not a valid base/index expression
lec_asm_fail.cpp:15: Error: `(%rdi,%eax,4)' is not a valid base/index expression
```

下面是能编译的版本，与讲座原代码有**两处差异**：①寻址索引 `%%eax` → `%%rax`；②clobber 列表 `"eax"` → `"rax"`（循环计数器实际占用的是完整 64 位寄存器，上半截也要告诉编译器被踩了）：

```cpp
// 与讲座 abs_array 原代码的差异:索引寄存器 %%eax -> %%rax,
// clobber 由 "eax" 改为 "rax"(x86-64 下 32 位寄存器不能作 base/index)
__attribute__((noinline)) void abs_sse(int32_t* dst, const int32_t* src, int n)
{
    __asm__ volatile (
        "xor %%eax, %%eax\n\t"
        "1:\n\t"
        "cmp %2, %%eax\n\t"
        "jge 2f\n\t"
        "movdqu (%1, %%rax, 4), %%xmm0\n\t"
        "movdqa %%xmm0, %%xmm1\n\t"
        "psrad $31, %%xmm1\n\t"
        "pxor %%xmm1, %%xmm0\n\t"
        "psubd %%xmm1, %%xmm0\n\t"
        "movdqu %%xmm0, (%0, %%rax, 4)\n\t"
        "add $4, %%eax\n\t"
        "jmp 1b\n\t"
        "2:\n\t"
        :
        : "r"(dst), "r"(src), "r"(n)
        : "rax", "xmm0", "xmm1", "memory", "cc");
}
```

**验证输出**（标量版 `abs_c` 朴素三目、两个函数都 `noinline`、1M 个随机 `int32` 且排除 `INT_MIN`）：

```text
$ g++ -std=c++20 -O2 -Wall -Wextra absbench.cpp -o absb2 && ./absb2
正确性对比: PASS
标量 abs_c:   149 ms
手写 SSE:     79 ms
加速比:       1.89x
$ g++ -std=c++20 -O3 -march=x86-64-v2 -Wall -Wextra absbench.cpp -o absb3 && ./absb3
正确性对比: PASS
标量 abs_c:   85 ms
手写 SSE:     80 ms
加速比:       1.06x
$ g++ -std=c++20 -O3 -march=x86-64-v2 -S absbench.cpp && grep -c pabsd absbench.s
5
```

1. `-O2`：手写 SSE 快约 1.9 倍（149 vs 79ms）——编译器没有自动向量化，手写 SIMD 依然有它的位置。→ 知识点：[WG21 标准化与 x86/RISC-V 汇编哲学](../cppcon/2025/02-some-assembly-required/07-wg21-standardization-and-assembly-philosophy.md)「动手验证：手写汇编 vs 编译器输出」一节（讲座的 abs_array 示例，本实验按上面的修正版跑通）
2. `-O3 -march=x86-64-v2`：编译器用 `pabsd` 自动向量化（汇编里 5 处），差距基本归零（1.06x，6% 的差距在噪声范围内）。手写版折腾半天只换来「和编译器打平」——这就是 2026 年手写 SIMD 的真实处境。→ 知识点：[从汇编到 C++](../cppcon/2025/02-some-assembly-required/01-personal-journey-and-from-assembly-to-cpp.md)「90 年代的编译器不行……但现在是 2026 年了」一节
3. 计时陷阱（本次实验真实踩过、如实记录）：**第一版没给两个函数加 `noinline`，`-O3` 下 `abs_c` 计时是 0ms**——编译器看出计时循环里 500 次调用都在重复计算同一批数据、只保留最后一次，把整条计时循环消除了。基准里被 benchmark 的函数必须 `noinline`（或每轮换数据），否则数字全是幻觉。→ 知识点：[WG21 标准化与 x86/RISC-V 汇编哲学](../cppcon/2025/02-some-assembly-required/07-wg21-standardization-and-assembly-philosophy.md)「编译器『聪明』与 UB 的模糊边界」一节（不猜、用工具、看汇编）

到这里，「零开销安检台」就有了实物：tag 和 concept 验类型、ABI 验参数、UBSan 验移位、checked_span 验下标、计数器和基准验惰性与移动、汇编验向量化——每一层的结论都有真实的终端输出背书，而不是谁嘴上说说。
