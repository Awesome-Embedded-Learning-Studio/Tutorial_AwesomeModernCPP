---
title: "卷 10 课后练习（Homework）"
description: "CppCon 2025 五场讲座精读的课后练习：每讲座基础+进阶两题，另加两道跨讲座综合与一道 L5 边界挑战（窄化检测的浮点 UB 修复，改编自 Stroustrup 演讲 Number 实现）。难度覆盖 L1~L5，题题变式，参考答案独立成文件、逐步解答附知识点链接；10.6-B 与 10.C-1 为 C++26 题（以 g++ 16 -std=c++26 实测）。"
chapter: 10
order: 1
tags: [host, advanced, cpp-modern, concepts, Ranges, optional, 移动语义]
difficulty: advanced
platform: host
cpp_standard: [17, 20, 23, 26]
reading_time_minutes: 11
prerequisites: []
related: []
---

# 卷 10 课后练习（Homework）

## 引言

本卷五场讲座有个共同点：它们都声称自己在讲「把想法变成可检查的代码」——Stroustrup 把窄化转换写进 concept，Godbolt 把抽象拆成汇编，Shah 把循环交给 ranges，Saks 把拷贝换成移动，Downey 用二十年把一个「带约束的指针」送进标准。但「看懂」和「做得出来」之间隔着一段路，这段路只能靠你自己走。这里的题按讲座分节，每场两道（基础 + 进阶），最后是两道跨讲座综合和一道 L5 挑战。每题标注难度档位（L1~L5，见[练习总览](./index.md)）和涉及讲座。

题目都做了「变式」处理：换场景、换数据、换推理方向，照抄教材例题是抄不出答案的；每道题都要真编译真跑，把输出贴下来才算完。答案在独立的[参考答案](./02-homework-solutions.md)文件里，按题号对应，每步解答带知识点链接。建议一场讲座做完再看答案。编译环境与讲座笔记一致：WSL Arch，g++ 16.1.1（clang++ 22.1.8 可交叉验证），个别题要求 `-std=c++23` 或 `-std=c++26`，题面会写明。

## 10.1 Concept-based Generic Programming

### 10.1-A {#hw-10-1-a}

难度 **L1** · 涉及[Concept-based Generic Programming](../cppcon/2025/01-concept-based-generic-programming/index.md)

写一个 `number` concept（`std::integral` 或 `std::floating_point`），再照讲座的三条分支写出 `narrowing_assign<T, U>` concept（范围更小 / 浮点转整数 / 有符号性不同）。把讲座的六个 `static_assert` 用例换成下面这组变式组合——前五个用 `static_assert` 全部通过，最后一个平台相关、用运行期 `printf` 打印判定结果：

- `bool` ← `int`（想一想 `std::numeric_limits<bool>::max()` 是多少，它为什么成立）
- `float` ← `double`
- `int` ← `double`
- `double` ← `float`（应该不窄化）
- `long long` ← `int`（应该不窄化）
- `char` ← `unsigned char`（平台相关！先预测你的平台会怎么判，再跑）

最后写一段注释回答：`narrowing_assign` 里 `&&` 和 `||` 混用的时候，为什么三个分支必须用括号包成一个整体？

[参考答案 →](./02-homework-solutions.md#hw-10-1-a)

### 10.1-B {#hw-10-1-b}

难度 **L4** · 涉及[Concept-based Generic Programming](../cppcon/2025/01-concept-based-generic-programming/index.md)

复刻讲座里的 `would_narrow` / `narrow_convert` / `Number<T>`（含 `operator+`，用 `std::common_type_t` 定返回类型），跑出讲座「原文错误更正」小节描述的现象：`Number<unsigned int>{3000000000u} + Number<unsigned int>{2000000000u}` **不抛异常**，得到一个回绕值。先预测这个值是多少，再真跑。然后实现讲座给出的 `safe_add`（unsigned 分支用 `max() - b` 检查、signed 分支用 `__builtin_add_overflow`），验证两个溢出场景都被 `std::overflow_error` 拦下。最后回答：为什么 `narrow_convert` 拦不住同类型的算术回绕？它的职责边界到底在哪？

[参考答案 →](./02-homework-solutions.md#hw-10-1-b)

## 10.2 Some Assembly Required

### 10.2-A {#hw-10-2-a}

难度 **L2** · 涉及[C++：底层汇编探秘](../cppcon/2025/02-some-assembly-required/index.md)

写三个函数：`int square(int)`、`long add_three(long, long, long)`、`long sum_seven(long × 7)`（七个参数），用 `g++ -O2 -S` 编译，把三个函数的汇编贴出来（去掉 `.cfi_*` 等指导性噪音）。**先动笔预测**再对照真实输出回答：①`square` 为什么多出一条 `mov` 指令（而不是一条 `imul` 收工）？②`add_three` 的三个参数分别在哪些寄存器（System V AMD64 ABI）？③`sum_seven` 的第七个参数在哪、为什么是 `[rsp+8]` 而不是 `[rsp]`？再预测：这个程序的 `main` 会被优化成什么样，验证一下。

[参考答案 →](./02-homework-solutions.md#hw-10-2-a)

### 10.2-B {#hw-10-2-b}

难度 **L3** · 涉及[C++：底层汇编探秘](../cppcon/2025/02-some-assembly-required/index.md)

把讲座的「数字位查找表」扩展成**十六进制合法性位查找表**：合法的字符是 `0-9`、`A-F`、`a-f`。先试试最直觉的单表写法（`uint64_t{1} << c` 把每个合法字符位置位），把编译结果贴出来——不出意外的话你会撞上一个 constexpr 编译错误，把它贴出来并解释。然后改成**双表**方案（低位表管 ASCII 0..63，高位表管 64..127），加上位移量守卫，对照朴素写法对 ASCII 0..127 全量验证一致。回答：为什么要先 `static_cast<unsigned char>` 再移位？单表方案的错误为什么在 constexpr 语境下直接变成编译错误而不是运行期炸弹？

[参考答案 →](./02-homework-solutions.md#hw-10-2-b)

## 10.3 Back to Basics: C++ Ranges

### 10.3-A {#hw-10-3-a}

难度 **L1** · 涉及[Back to Basics: C++ Ranges](../cppcon/2025/03-back-to-basics-ranges/index.md)

写一个「迭代器类别双探针」程序：对 `std::array<int,5>`、`std::deque<int>`、`std::forward_list<int>`、`std::map<int,int>` 和裸指针 `int*`，分别用 C++98 的 `iterator_traits<T>::iterator_category`（legacy tag）和 C++20 的 concept 探测类别，打印对照表。根据你的真实输出回答：①`deque` 的 legacy tag 和 concept 各是什么？它俩「随机可访问」为什么没有更进一步的「连续」标签？②`array` 的 concept 为什么是 `contiguous_iterator` 而不是 `random_access_iterator`——「连续」这个更强的性质给了你什么能力（结合讲座提到的 C 接口 / `memcpy` 场景）？

[参考答案 →](./02-homework-solutions.md#hw-10-3-a)

### 10.3-B {#hw-10-3-b}

难度 **L3** · 涉及[Back to Basics: C++ Ranges](../cppcon/2025/03-back-to-basics-ranges/index.md)

造一个 1000 万个 `int` 的 `vector`（`iota` 填充），做两组实验，全部真跑。①**短路计数**：变式谓词改成 `x % 7 == 0`，对比「`views::filter(pred)` 全量遍历」和「`views::filter(pred) | views::take(5)`」的谓词调用次数；然后单独对 `take(5)` 版拆解 `begin()`、`end()` 和每次 `++` 各消耗几次谓词调用，解释你测到的总次数是怎么构成的。②**eager vs lazy**：用 `x > N/2` 谓词，对比「`ranges::to<vector>` 物化后再求和」与「直接遍历 view 求和」的耗时与结果，验证两者结果一致。

[参考答案 →](./02-homework-solutions.md#hw-10-3-b)

## 10.4 Back to Basics: Move Semantics

### 10.4-A {#hw-10-4-a}

难度 **L2** · 涉及[Back to Basics: Move Semantics](../cppcon/2025/04-back-to-basics-move-semantics/index.md)

手搓一个带静态计数器（`copies` / `moves`）的简化字符串类 `Str`（裸指针 + 完整五件套：析构、拷贝构造、移动构造、拷贝赋值、移动赋值），实现 `copy_swap`（`T temp(x); x = y; y = temp;`）和 `move_swap`（同样三行但每一步加 `std::move`），各跑 10 万次并计时。验证：拷贝版计数是不是 30 万、移动版拷贝计数是不是 0、移动计数是不是 30 万。回答：①swap 里的「三次拷贝」分别是哪三个操作？②`std::move` 在这三行里各自「做了什么」（它是移动吗）？③为什么 `temp` 明明有名字，编译器却不敢自己把它当右值？

[参考答案 →](./02-homework-solutions.md#hw-10-4-a)

### 10.4-B {#hw-10-4-b}

难度 **L3** · 涉及[Back to Basics: Move Semantics](../cppcon/2025/04-back-to-basics-move-semantics/index.md)

复刻讲座的 `vector` 扩容陷阱：写一个持有 `char*` 的类，移动构造函数用一个宏开关控制是否 `noexcept`，静态计数拷贝/移动次数。`reserve(4)` 后塞 5 个元素（第五个必然触发扩容 4 → 8），分别编译运行两个版本，贴出两次的 `copies/moves`。解释：为什么移动构造没标 `noexcept` 时 `vector` 宁可用拷贝——它要保的「强异常安全保证」到底在怕什么？再回答：真实工程里如果你给一个容器类型写了移动构造却忘了 `noexcept`，会在什么场景下、以什么规模地掉性能？

[参考答案 →](./02-homework-solutions.md#hw-10-4-b)

## 10.6 The Evolution of std::optional

### 10.6-A {#hw-10-6-a}

难度 **L1** · 涉及[The Evolution of std::optional: From Boost to C++26](../cppcon/2025/06-evolution-of-std-optional/index.md)

写一个会打印每次构造/析构的 `Tracer` 塞进 `optional<Tracer>`，按这个顺序操作并贴出完整输出：声明空 optional → `emplace()` → 拷贝构造第二个 → **变式新增一步**：`std::move` 构造第三个，观察移动后源 optional 的 `has_value()` 还是不是 true → `reset()` 第一个，观察第二个、第三个有没有受影响。根据输出回答：①`Tracer` 的构造函数在声明那一刻被调用了吗？②`reset()` 为什么只析构自己的那份？③移动后源 optional 的 `has_value()` 说明 optional 的 moved-from 状态有什么特点？最后用一句话说明 `optional<T>` 在代数上等价于什么（`variant<T, ???>`）。

[参考答案 →](./02-homework-solutions.md#hw-10-6-a)

### 10.6-B {#hw-10-6-b}

难度 **L3** · 涉及[The Evolution of std::optional: From Boost to C++26](../cppcon/2025/06-evolution-of-std-optional/index.md)

**这道题必须用 `-std=c++26`（g++ 16 起支持，提案 P2988），先在 C++23 下编一次把报错贴出来作对照。** 把讲座的 `Cat` 例子换成银行账户：两个 `Account{alice=100, bob=200}`，一个空的 `optional<Account&>` 和一个绑着 alice 的。依次：空 optional 赋 bob、已绑定的赋 bob、通过 `->balance` 加 50，打印四个值；再用 `std::swap` 交换两个 `optional<int&>`，打印交换后的解引用值和原始变量。解释：①赋值为什么永远是**重绑定**而不是穿透赋值（结合 JeanHeyd 的「状态依赖就不可推导」观察）？②swap 换的是指针还是值，你怎么从输出里证明？③顺带验证 `make_optional` 和 CTAD 对引用各退化成什么。

[参考答案 →](./02-homework-solutions.md#hw-10-6-b)

## 10.C 跨讲座综合与挑战

### 10.C-1 {#hw-10-c-1}

难度 **L3** · 涉及[Back to Basics: C++ Ranges](../cppcon/2025/03-back-to-basics-ranges/index.md)、[The Evolution of std::optional](../cppcon/2025/06-evolution-of-std-optional/index.md)

综合题：写一个「分数册查询管线」。`unordered_map<string,int>` 存五个人的分数（含一个不及格的），写 `try_get` 查找函数——C++26 下直接返回 `std::optional<mapped_type&>`，找到 `*r += 10` 直接改表里的值，查一个不存在的 key 走 `nullopt` 分支。再用 ranges 管道统计：`views::values` 求总分人数，`views::values | views::filter(≥60)` 求及格人数与及格总分，最后用手写循环对照一遍，两个结果必须一致。要求 `-std=c++26 -O2` 真跑，贴出全部输出。

[参考答案 →](./02-homework-solutions.md#hw-10-c-1)

### 10.C-2 {#hw-10-c-2}

难度 **L4** · 涉及[C++：底层汇编探秘](../cppcon/2025/02-some-assembly-required/index.md)、[Back to Basics: C++ Ranges](../cppcon/2025/03-back-to-basics-ranges/index.md)、[Back to Basics: Move Semantics](../cppcon/2025/04-back-to-basics-move-semantics/index.md)

零开销抽象汇编审计。写四个 `noinline` 函数：`sum_loop`（下标循环）、`sum_rangefor`（range-based for）、`sum_view`（`views::filter` 恒真谓词 + 循环求和）、`make_big`（返回一个 32 字节的 `Big{long data[4];}`），再加一个 `use_big` 调用它。`-O2 -S` 编译，对着真实汇编回答三个判断：①range-based for 相对下标循环有没有额外开销——你的汇编证据是什么？②恒真 filter 的 view 版本和手写循环编译出一样的循环吗？③`use_big` 里 `make_big` 的返回值发生拷贝了吗——在汇编里找出证据（提示：注意 `call` 之前哪个寄存器装的是什么、函数体往哪写）。每个判断都要贴出对应的汇编片段。

[参考答案 →](./02-homework-solutions.md#hw-10-c-2)

### 10.C-3 {#hw-10-c-3}

难度 **L5** · 涉及[Concept-based Generic Programming](../cppcon/2025/01-concept-based-generic-programming/index.md)

挑战题（受 Bjarne Stroustrup CppCon 2025 演讲 `Number<T>` / `would_narrow` 实现启发、按竞赛挑战强化。早期阶段 L5＝「用该阶段知识可解的最难问题」，档位口径见[练习总览](./index.md)）。讲座的 `would_narrow` 对 `NaN` / `±Inf` / 超出 `long long` 范围的浮点输入是**未定义行为**——那个 `static_cast<long long>(u)` 就是雷。分两步做：①**先证 UB 再修**：写最小复现（`noinline` 函数运行时喂入 `NaN` 与 `1e300`），用 `-fsanitize=undefined,float-cast-overflow` 抓到运行时报告，贴出来；同时贴普通构建下「碰巧返回正确结果」的对照。注意一个坑：**GCC 的 `float-cast-overflow` 不在 `-fsanitize=undefined` 默认集合里**，要显式加。②**修复并全绿**：在浮点转整数分支里先做范围/有限性检查，再用 16 个边界用例（NaN、±Inf、±0.0、±0.5、±1e300、INT_MAX±1 边界、3.0/3.5 等）验证 `int` 版判定表，外加 `long long` 版的关键用例（`(double)LLONG_MAX` 这类转成 double 后恰好越过边界的值），同一份修复代码在 sanitizer 构建下零报告。

[参考答案 →](./02-homework-solutions.md#hw-10-c-3)
