---
title: "卷四 · 进阶 课后练习参考答案（Homework）"
description: "卷四课后练习的逐题详细解答：87 道题（42 章 × 2 + 跨章综合与 L5 挑战）每题给出解题思路、逐步解答（每步标注知识点链接回教材章节）与真实验证输出。所有命令与输出在 WSL Arch（g++ 16.1.1 / clang++ 22.1.8）真实运行得到；编译错误类题目的报错为真实节选（注明截断），本机不支持的特性如实标注。"
chapter: 4
order: 2
tags:
  - host
  - advanced
  - cpp-modern
  - 模板
  - 泛型
  - 模板元编程
  - concepts
difficulty: advanced
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 99
prerequisites: []
related: []
---

# 卷四 · 进阶 课后练习参考答案（Homework）

> 所有命令与输出在 WSL Arch（g++ 16.1.1 / clang++ 22.1.8）真实运行得到。编译错误类的输出为真实报错**节选**（注明已截断），模板报错普遍很长，摘关键帧即可。涉及 C++26 特性处本机不支持会如实标注，绝不编造。

## 4.1-A {#hw-4-1-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-4-1-a)

**思路**：配对表的关键是分清 `promise_type` 的六个接口与 awaitable 的三个接口属于两层协议；惰性来自 `initial_suspend` 返回 `suspend_always`。

1. 配对：`co_return` → `return_void()`（无值）/`return_value(T)`（有值）；`co_yield T` → `yield_value(T)`；`co_await expr` → awaitable 的 `await_ready()`、`await_suspend(H)`、`await_resume()`。而 `get_return_object()`、`initial_suspend()`、`final_suspend()` 是 `promise_type` 的接口，但**不是**被关键字直接调用的——它们由协程机制在「创建时 / 首次挂起时 / 结束前」调。陷阱答案：`await_ready/await_suspend/await_resume` 不属于 `promise_type`，属于 awaitable 对象（本题的 `Generator` 里就是 `std::suspend_always` 这类标准库现成实现）。→ 知识点：[协程基础](../01-coroutine-basics.md)「6 个必要的协程帧对象接口」「3 个 Awaitable 接口函数」
2. `main` 里先打印 `before first next`，再逐次 `next()`；输出里 `before first next` 出现在所有 `got` 之前，且构造 `gen3()` 后一行协程体都没执行——证据就是第一行输出。→ 知识点：[协程基础](../01-coroutine-basics.md)「initial_suspend：初始暂停点」一节

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra gen3.cpp -o gen3 && ./gen3
before first next
got 1
got 2
got 3
exhausted
```

惰性证据：`before first next` 之后才出现 `got 1`——`initial_suspend()` 返回 `std::suspend_always`，协程创建后停在第一个挂起点，`next()` 调 `resume()` 才真正跑起来。

## 4.1-B {#hw-4-1-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-1-b)

**思路**：协程体结束（显式 `co_return;` 或坠落）时，机制必须调用 `return_void()` 或 `return_value()` 之一——一个都没声明就是格式错误；`final_suspend` 挂起后，收尸的是持有句柄的返回对象析构。

1. 删掉 `return_void` 后，`co_return;` 这一行直接点名缺了谁。→ 知识点：[协程基础](../01-coroutine-basics.md)「return_void() 或 return_value(V)」一节
2. `countdown(3)` 跑完后协程处于 final_suspend 挂起态，`handle.done()` 为真（不会再被 resume）。因为 `final_suspend` 返回 `suspend_always`，协程**不自动销毁**，销毁责任落到 `Generator` 的析构：`~Generator() { if (handle_) handle_.destroy(); }`——谁持有句柄谁收尸，析构顺序天然正确。→ 知识点：[协程基础](../01-coroutine-basics.md)「final_suspend：最终暂停点」一节、[迭代器模式](../vol4-generics-patterns/18-iterator.md)（生成器同样的生命周期套路）

**验证输出**：

```text
$ g++ -std=c++20 no_return_void.cpp -o nrv
no_return_void.cpp:18:5: error: no member named 'return_void' in
'std::__n4861::__coroutine_traits_impl<G, void>::promise_type' {aka 'G::promise_type'}
   18 |     co_return;
      |     ^~~~~~~~~
$ g++ -std=c++20 -Wall -Wextra countdown.cpp -o countdown && ./countdown
3 2 1
liftoff
```

## 4.2-A {#hw-4-2-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-2-a)

**思路**：`Task<int>` 自己实现 awaitable 三接口，`co_await` 时把结果从 `promise_type::cached_value` 取回来；关键点是「谁等谁」和「谁收尸」。前提约束：`initial_suspend` 必须返回 `suspend_never`（照教材 Task 设计）——`co_add` 才会在调用点立刻开跑、把 42 缓存进 promise；若写成 `suspend_always` 又没人去 resume，协程体停在初始挂起点不开跑，`await_resume` 取到的是默认初始化的 0（实测输出 `result = 0`，见文末反例）。

1. `await_ready()` 返回 `false` 的意思是「我要接管等待逻辑」——哪怕结果早就躺在缓存里，也要先挂起、走 `await_suspend` 再恢复，这样执行流和取数时机都由我们控制。`await_suspend` 里直接 `h.resume()` 是「立即把父协程放回就绪」的同步实现。→ 知识点：[协程调度器](../02-coroutine-scheduler.md)「Task 的 awaitable 接口」一节
2. `final_suspend` 返回 `suspend_always` 让协程停在终点不自杀；`Task` 析构里 `handle_.destroy()` 收尸——挂起权在上层、销毁权也在上层。→ 知识点：[协程调度器](../02-coroutine-scheduler.md)「生命周期上返回类型和协程句柄谁更长」一段

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra task_add.cpp -o task_add && ./task_add
result = 42
done
```

（反例实测：把 `initial_suspend` 改成 `suspend_always`、其余不动，`main` 里显式 `resume()` 拉起 `main_task`——`co_add` 仍停在初始挂起点没开跑，`await_resume` 取到默认初始化的 0。）

```text
$ g++ -std=c++20 -Wall -Wextra task_add_bad.cpp -o task_add_bad && ./task_add_bad
result = 0
done
```

## 4.2-B {#hw-4-2-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-2-b)

**思路**：单线程调度器 = 就绪队列 + 按唤醒时间排序的睡眠堆；每个协程「打一次睡 1ms」，睡眠期间调度器自然去跑另一个，于是输出交替。真正的坑在协程参数的引用悬垂上。

1. 调度循环三段式：先把就绪队列清空（每 `resume` 一次），再看睡眠堆里有没有到点的（搬运回就绪），都没有才 `sleep_until` 睡到最近一个唤醒点。→ 知识点：[协程调度器](../02-coroutine-scheduler.md)「实现调度逻辑」一节
2. 交替的原因：A 打一次 `co_await SleepAwaiter{1ms}` 就把自己挂进睡眠堆，调度器转头跑 B；B 同样挂起后本线程睡到 1ms 点，两个一起到点、按入队顺序交错恢复。这就是协作式调度——不是时间片抢占，是「谁让出谁排队」。→ 知识点：[协程调度器](../02-coroutine-scheduler.md)「优先级：优先处理活跃的协程」
3. 参数按值传的原因：协程参数在 `initial_suspend` **之前**拷贝进协程帧；写成 `const std::string&` 时帧里存的是指向调用方临时对象的引用，调用方语句结束、临时销毁，第二个任务又在同一个栈槽上构造了 "B"——A 的悬垂引用就指到了 B 的内容上，真实输出是 `B0 B0 B1 B1 ...`（A 打出了 B）。这一坑是我写初版时真踩出来的，贴的就是当时的输出。→ 知识点：[协程基础](../01-coroutine-basics.md)「协程帧保存局部变量与参数」、[C++20 范围库基础与视图](../vol2-modern-cpp17/07-ranges-basics-and-views.md)（同款「引用悬垂」坑的协程版）

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra sched.cpp -o sched && ./sched
A0
B0
A1
B1
A2
B2
A3
B3
A4
B4
done
```

（初版 `const std::string&` 参数的真实翻车现场：`B0 B0 B1 B1 B2 B2 B3 B3 B4 B4`，A 从头到尾没出现——悬垂引用指向了被复用的栈槽。）

## 4.3-A {#hw-4-3-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-4-3-a)

**思路**：最派生对象必须非零大小（所以 `Empty` 是 1 字节），但基类子对象可以被压成 0——EBO 只对基类和 `[[no_unique_address]]` 成员开放。

1. 成员版：`Empty e` 是数据成员，按语言规则必须占非零字节，加上 `int` 的对齐要求被 pad 成 8。→ 知识点：[空基类优化](../03-empty-base-optimization.md)「空成员变量默认不能被 EBO 压缩」
2. 继承版和 `[[no_unique_address]]` 版：编译器把空子对象挤进 `int` 的开头，不占额外字节，于是 sizeof 都等于 `sizeof(int)`。→ 知识点：[空基类优化](../03-empty-base-optimization.md)「C++20 让事情更干净」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra ebo.cpp -o ebo && ./ebo
sizeof(Empty)       = 1
sizeof(AsMember)    = 8
sizeof(AsBase)      = 4
sizeof(AsNoUnique)  = 4
```

## 4.3-B {#hw-4-3-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-3-b)

**思路**：私有继承让 `Deleter` 变成基类子对象，EBO 直接适用；成员版哪怕空类也要占 1 字节，再被对齐 pad 到 8，总大小翻倍。

1. `CompressedPair : private D` 里 `second()` 返回 `*this` 转型成的 `D&`——继承同时提供了「存储」和「访问」两个能力。→ 知识点：[空基类优化](../03-empty-base-optimization.md)「compressed pair 技巧」、[类模板](../vol1-basics-cpp11-14/03-class-templates.md)
2. 带 `int` 成员的有状态删除器：两版大小都变成 16（指针 8 + int 4 + pad 4）——EBO 只对**空**类生效，一旦有成员就老老实实占地方。先预测后验证即可。→ 知识点：[空基类优化](../03-empty-base-optimization.md)「有状态的策略类不吃这套」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra cpair.cpp -o cpair && ./cpair
sizeof(CompressedPair) = 8
sizeof(NaivePair)      = 16
sizeof(int*)           = 8
value = 42
```

## 4.4-A {#hw-4-4-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-4-4-a)

**思路**：default `<=>` 按成员声明顺序做字典序比较，并把 `==` 一并生成；只写 `<=>` 时 `==`/`!=` 是白送的，反过来不成立。

1. 六个运算符全部可用，排序按 `(sensor_id, value)` 字典序——`(1,9) (1,23.5) (2,10)`。→ 知识点：[三路比较运算符](../05-spaceship-operator.md)「使用 = default 自动生成」
2. 只留 `<=>` 删掉 `==`：`==` 和 `!=` 仍然能用（P1185 后 `<=>` 是「上游」，顺流送出所有运算符）。→ 知识点：[三路比较运算符](../05-spaceship-operator.md)「坑1：默认 == 不会反向生成 <=>」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra sensor.cpp -o sensor && ./sensor
== true, != false, < true, <= true, > false, >= false
(1,9) (1,23.5) (2,10)
$ g++ -std=c++20 -Wall -Wextra spaceship_only.cpp -o sp_only && ./sp_only
a == b : true
a != b : false
a < c  : true
```

## 4.4-B {#hw-4-4-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-4-b)

**思路**：①`==` 只生成 `!=`，不带任何关系运算符；②大小写不敏感是「等价但不相等」的典型，必须 `weak_ordering`。

1. 只 default `==` 的类型没有 `<`，编译器直接「no match」。→ 知识点：[三路比较运算符](../05-spaceship-operator.md)「坑1」一节
2. `CIString` 的 `<=>` 先转小写再比，`==` 委托给 `<=>`；`"Hello" == "HELLO"` 为真但两者内容不同——等价不相等，这正是 weak 与 strong 的分野（strong 要求等价即相等，大小写不敏感字符串做不到，硬用 strong 会让 `==` 与 `<=>` 语义打架）。→ 知识点：[三路比较运算符](../05-spaceship-operator.md)「weak_ordering：弱序」一节

**验证输出**：

```text
$ g++ -std=c++20 eq_only.cpp -o eq_only
eq_only.cpp:12:16: error: no match for 'operator<' (operand types are
'HasEqualityOnly' and 'HasEqualityOnly')
   12 |     bool r = a < b;
$ g++ -std=c++20 -Wall -Wextra ci.cpp -o ci && ./ci
s1 == s2: true
s1 <=> s2 equivalent: true
apple Banana Hello
```

## 4.5-A {#hw-4-5-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-4-5-a)

**思路**：配对表按「四个问题 → 四个机制」一一对应；本机实测验证 g++ 的模块支持现状。

1. 配对：编译速度灾难 → BMI/IFC 缓存（模块编译一次、接口信息序列化复用）；宏污染 → 宏不跨模块传播；传染式依赖 → 接口与实现解耦（`export` 只暴露该暴露的）；ODR 隐式规则 → 编译器真正理解模块边界。→ 知识点：[C++ Modules（MSVC）](../msvc-cpp-modules.md)「我们为什么需要 Modules」「C++ Modules 的核心思想」
2. 本机 g++ 16 实测：最小命名模块**可以**编译运行（`-fmodules-ts`），产物是 gcm.cache 里的缓存文件，与 MSVC 的 `.ifc` 同构——都是「编译接口的序列化描述」。若你的 g++ 版本不支持，如实报告即可，本卷的模块实践以教材的 MSVC/VS2026 流程为准。→ 知识点：[C++ Modules（MSVC）](../msvc-cpp-modules.md)「模块的最小单位：BMIs」

**验证输出**：

```text
$ g++ -std=c++20 -fmodules-ts mymath.cppm usemod.cpp -o usemod && ./usemod
add(2, 3) = 5
```

## 4.5-B {#hw-4-5-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-5-b)

**思路**：三个问答全部围绕「模块与预处理器的根本差别」展开。

1. 宏在预处理阶段做纯文本替换，没有作用域概念——定义后一直活到文件尾或 `#undef`。模块是编译期的结构，模块接口默认**不导出宏**，`import` 只拿编译好的接口信息，宏天然跨不过去。→ 知识点：[C++ Modules（MSVC）](../msvc-cpp-modules.md)「问题二：宏污染是不可控的」
2. `import std;` 四步：查标准库模块 → 加载预编译 `.ifc` → 注入导出符号 → 不引入任何宏。类比 Java `.class`：对的部分是「编译一次、到处复用」；不对的部分是 `.ifc` 不是字节码，里面是 AST/接口的结构化描述，依赖编译器前端。→ 知识点：[C++ Modules（MSVC）](../msvc-cpp-modules.md)「import std; 到底发生了什么」
3. 四档决策：`import std;` 强烈推荐（价值高、风险极低）；新项目内部模块化推荐（消除传染式依赖）；公共跨平台库 API 谨慎（编译器实现差异）；高一致性项目谨慎（行为一致性优先，头文件成熟数十年）。宏污染实例：`<windows.h>` 的 `min`/`max` 宏会把你的 `std::min` 调用替换成宏展开，编译报错或语义全变，只能 `#undef` 或 `NOMINMAX`。→ 知识点：[C++ Modules（MSVC）](../msvc-cpp-modules.md)「今日要在什么时候使用 MSVC Modules」

## 4.6-A {#hw-4-6-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-4-6-a)

**思路**：四种实体各司其职；变量模板出现前的绕路是「类模板 + 静态成员」。

1. 函数/类/变量/别名四种模板一次亮齐，注意变量模板 `e<T>` 的用法像常量、别名模板 `Vec<int>` 就是 `std::vector<int>`。→ 知识点：[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)「C++ 里有四种模板实体」
2. 老写法 `pi_trait<T>::value` 要 `typename`+`::value`；`numeric_limits<T>::max()` 是函数而非变量，因为它诞生时变量模板还不存在（C++14 才引入）。→ 知识点：[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)「变量模板」一段

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra four_entities.cpp -o four_entities && ./four_entities
square(3)   = 9
square(2.5) = 6.25
Holder      = 7
e<double>   = 2.71828
Vec<int>    = 3 elements
```

## 4.6-B {#hw-4-6-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-6-b)

**思路**：模板递归 + 两个基线特化，`static_assert` 是「编译期已算好」的一号证据，运行期打印只做佐证。

1. `Fib<N> = Fib<N-1> + Fib<N-2>`、`Fib<0>=0`、`Fib<1>=1`，变量模板 `fib_v` 顺手用上。→ 知识点：[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)「模板是编译期图灵完备的」
2. 证据一：`static_assert` 在编译期就验过，编不过根本到不了运行；证据二：运行时打印的只是常量，汇编里就是 `mov` 一个立即数。图灵完备意味着编译期能做任意计算，代价是编译慢、报错难读、代码可读性差。→ 知识点：[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)「模板元编程的根」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra fib.cpp -o fib && ./fib
Fib<10> = 55
fib_v<20> = 6765
```

## 4.7-A {#hw-4-7-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-7-a)

**思路**：包含模型要求定义在实例化点可见；声明与定义分居两个 TU 时，两边谁都补不上对方缺的那一半。

1. `main.cpp` 里编译器只见声明、没法实例化 `gcd<int>`，留下未解析符号；`gcd.cpp` 里没人用 `gcd`，编译器压根不实例化——链接时 undefined reference。→ 知识点：[函数模板深化](../vol1-basics-cpp11-14/02-function-templates-deep.md)「包含模型」一节
2. 修复 = 定义搬进头文件。`extern template` 只是「本 TU 别再生成、去别处找」，实例化点仍然必须存在，与真正的分离编译是两回事。→ 知识点：[函数模板深化](../vol1-basics-cpp11-14/02-function-templates-deep.md)「extern template 不是分离编译」预警块

**验证输出**：

```text
$ g++ -std=c++20 main_gcd.cpp gcd.cpp -o gcd_bad
/usr/bin/ld: /tmp/cc20ksVg.o: in function `main':
main_gcd.cpp:(.text+0x30): undefined reference to `int gcd<int>(int, int)'
collect2: error: ld returned 1 exit status
$ g++ -std=c++20 -Wall -Wextra main_gcd_fix.cpp -o gcd_fix && ./gcd_fix
gcd(36, 48) = 12
```

## 4.7-B {#hw-4-7-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-7-b)

**思路**：函数模板的偏特化是标准明令禁止的；三条合法出路是重载、`if constexpr`、SFINAE。

1. 偏特化报错措辞直接把规则说了出来：只有 class 和 variable 能偏特化。函数有重载这个更灵活的机制，标准不想让两套语义打架。→ 知识点：[函数模板深化](../vol1-basics-cpp11-14/02-function-templates-deep.md)「函数模板不能偏特化」一节
2. 指针重载版靠重载决议赢（`T*` 更匹配）；`if constexpr` 版在编译期丢掉不成立的分支，不会留下「解引用非指针」的硬错误。→ 知识点：[函数模板深化](../vol1-basics-cpp11-14/02-function-templates-deep.md)「重载」「if constexpr 的预告」

**验证输出**：

```text
$ g++ -std=c++20 fn_partial.cpp -o fp
fn_partial.cpp:8:3: error: non-class, non-variable partial specialization
'identity<T*>' is not allowed
    8 | T identity<T*>(T* x)
$ g++ -std=c++20 -Wall -Wextra fn_ok.cpp -o fn_ok && ./fn_ok
42
42
integer: 42
pointer, deref = 42
other
```

## 4.8-A {#hw-4-8-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-8-a)

**思路**：惰性实例化让没被调用的成员连「看都不看」；显式实例化会把所有成员一次性逼出来。

1. 只调 `show()` 时 `broken()` 从未实例化，里面的胡话编译器看不见。→ 知识点：[类模板](../vol1-basics-cpp11-14/03-class-templates.md)「惰性实例化」一节
2. `template struct Box<int>;` 强制实例化整个类，`broken()` 当场现形。工程纪律：写类模板要显式做一次「全类型自检」或测试覆盖每个成员，否则错误藏到有人真用那一天。→ 知识点：[类模板](../vol1-basics-cpp11-14/03-class-templates.md)「错误藏得深」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra lazy.cpp -o lazy && ./lazy
42
$ g++ -std=c++20 lazy_force.cpp -o lf
lazy_force.cpp:11:17:   required from here
   11 | template struct Box<int>;
lazy_force.cpp:8:46: error: request for member 'nonexistent_member' in
'((const Box<int>*)this)->Box<int>::value', which is of non-class type 'const int'
```

## 4.8-B {#hw-4-8-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-8-b)

**思路**：三件套的共同根源是两阶段查找——第一阶段（定义点）看不到依赖名背后的东西，得靠语法把查找推迟或限定到第二阶段。

1. `typename Container::value_type`：告诉编译器「这是类型」，否则默认当变量/函数。→ 知识点：[类模板](../vol1-basics-cpp11-14/03-class-templates.md)「依赖名 vs 非依赖名」
2. `this->data`/`this->greet()`：`Base<T>` 是 dependent base，第一阶段不查它，`this->` 让查找进入实例化阶段。`using Base<T>::kDefault;` 是同一问题的另一解，适合频繁访问时集中声明。→ 知识点：[类模板](../vol1-basics-cpp11-14/03-class-templates.md)「dependent base 和 this->」、[别名模板与 using 声明](../vol1-basics-cpp11-14/08-alias-and-using.md)「using 在模板继承里」
3. 那个机制就是两阶段查找：定义点查非依赖名、实例化点查依赖名（靠 ADL）。→ 知识点：[名字查找与 ADL](../vol1-basics-cpp11-14/06-name-lookup-and-adl.md)「模板不一样：分两阶段」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra dependent.cpp -o dependent && ./dependent
first = 10
Base::greet
fetch = 7
```

## 4.9-A {#hw-4-9-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-9-a)

**思路**：主模板给默认值、特化针对目标类型覆盖，是 `<type_traits>` 的整套骨架。

1. 三个全特化钉死 `float`/`double`/`long double`，其余类型走主模板的 `false`。→ 知识点：[模板特化与偏特化](../vol1-basics-cpp11-14/04-specialization-partial.md)「全特化」
2. 全特化不再是模板（所有参数钉死），定义只能在一个 TU，否则违反 ODR（主模板实例化可以多 TU 合并，全特化不行）。→ 知识点：[模板特化与偏特化](../vol1-basics-cpp11-14/04-specialization-partial.md)「全特化有两个特点」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra isfp.cpp -o isfp && ./isfp
int          : false
float        : true
double       : true
long double  : true
int*         : false
```

## 4.9-B {#hw-4-9-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-9-b)

**思路**：①`vector<bool>` 位压缩后给不了真引用，只能给代理；②引用偏特化是「模式匹配」最直接的演示。

1. `auto& x = v[0];` 的报错点名了 `_Bit_reference` 这个代理类型——它是右值临时对象，绑不了非 const 左值引用。这是偏特化的反面教材：实现可以完全重写，代价是不再满足主模板隐含的接口契约。→ 知识点：[模板特化与偏特化](../vol1-basics-cpp11-14/04-specialization-partial.md)「std::vector\<bool\> 的偏特化」
2. `IsRef<T&>` 一个偏特化同时覆盖 `int&` 和 `const int&`，因为 `T` 能绑定 `int` 和 `const int`；`T&&` 同理覆盖右值引用。→ 知识点：[模板特化与偏特化](../vol1-basics-cpp11-14/04-specialization-partial.md)「偏特化能匹配哪些模式」

**验证输出**：

```text
$ g++ -std=c++20 vecbool.cpp -o vb
vecbool.cpp:6:18: error: cannot bind non-const lvalue reference of type
'std::_Bit_reference&' to an rvalue of type 'std::vector<bool>::reference'
$ g++ -std=c++20 -Wall -Wextra isref.cpp -o isref && ./isref
int   : false
int&  : true
int&& : true
int*  : false
```

## 4.10-A {#hw-4-10-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-10-a)

**思路**：`auto` 非类型参数把「类型 + 值」两个参数合而为一，但「编译期确定」的铁律不变。

1. 推导结果：`42`→`int`、`true`→`bool`、`'a'`→`char`（输出里 bool 打印成 `true`、char 打印成 `a`）。→ 知识点：[非类型模板参数](../vol1-basics-cpp11-14/05-non-type-parameters.md)「C++17 的 auto」
2. 铁律：实参必须是常量表达式。运行时的变量、函数局部量传不进去——`int n = read(); Constant<n> c;` 会编译失败，因为 `n` 不是编译期常量，编译器要用它生成类型。→ 知识点：[非类型模板参数](../vol1-basics-cpp11-14/05-non-type-parameters.md)「实参必须是常量表达式」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra ntp_auto.cpp -o ntp_auto && ./ntp_auto
42
true
a
```

## 4.10-B {#hw-4-10-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-10-b)

**思路**：①structural 类型的等价性是成员级比较；②浮点 NTTP 的等价性是位级比较，所以 `0.0` 与 `-0.0` 是不同类型。

1. structural 三条件：literal class（有 constexpr 构造）、全部非静态成员 public 且非 mutable、成员与基类也都是 structural。值相同 → 同一类型。→ 知识点：[非类型模板参数](../vol1-basics-cpp11-14/05-non-type-parameters.md)「C++20 的两大放宽」
2. `Tag<0.0>` 与 `Tag<-0.0>` 位级不同（符号位），是不同类型；`3.14` 与 `3.14000` 词法折叠后位级相同，是同一类型。判据是位级不是数值，就是为了躲开浮点等价的精度泥潭。→ 知识点：[非类型模板参数](../vol1-basics-cpp11-14/05-non-type-parameters.md)「等价性：哪些实参算同一个」
3. 字符串字面量是 `const char[N]`，会 decay 成指针、地址随 TU 变化，等价性没法处理；C++20 的出路是 structural 的 fixed_string 类把字符串包起来当 NTTP。→ 知识点：[非类型模板参数](../vol1-basics-cpp11-14/05-non-type-parameters.md)「字符串字面量从来不能直接做非类型参数」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra ntp_struct.cpp -o ntp_struct && ./ntp_struct
Pixel<Point{1,1}> == Pixel<Point{1,1}> : true
Pixel<Point{1,1}> == Pixel<Point{1,2}> : false
Tag<0.0> == Tag<-0.0>   : false
Tag<3.14> == Tag<3.14000> : true
pos = (3,4)
```

## 4.11-A {#hw-4-11-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-11-a)

**思路**：ADL 在实参类型的命名空间里找候选；swap 惯用法先用 `using std::swap` 垫底再裸调。

1. `draw(p)` 的普通查找在全局找不到，但实参 `p` 是 `geo::Point`，ADL 去 `geo` 里捞到了 `geo::draw`。→ 知识点：[名字查找与 ADL](../vol1-basics-cpp11-14/06-name-lookup-and-adl.md)「ADL：实参依赖查找」
2. `using std::swap;` 把 `std::swap` 引入候选做兜底，裸调 `swap(a,b)` 时 ADL 若找到更高效的 `geo::swap` 就优先用——给自定义类型留优化口子，`begin/end/size/data` 同款。→ 知识点：[名字查找与 ADL](../vol1-basics-cpp11-14/06-name-lookup-and-adl.md)「swap 惯用法」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra adl.cpp -o adl && ./adl
geo::draw(1,2)
geo::swap called
a = (2,2)
```

## 4.11-B {#hw-4-11-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-11-b)

**思路**：`classify` 是非依赖名，第一阶段（定义点）就绑定到当时唯一可见的 `classify(int)`；`double` 是内置类型没有关联命名空间，第二阶段 ADL 帮不上忙。

1. `call_classify(3.14)` 走 `classify(int)`，`3.14` 被截成 3——输出实锤。→ 知识点：[名字查找与 ADL](../vol1-basics-cpp11-14/06-name-lookup-and-adl.md)「两阶段查找」
2. 想救它：把 `classify` 放进命名空间、与实参类型同空间（或实参是自定义类型），让调用变成依赖调用、ADL 在实例化点介入。→ 知识点：[名字查找与 ADL](../vol1-basics-cpp11-14/06-name-lookup-and-adl.md)「定义点之后加的重载模板看不到」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra twophase.cpp -o twophase && ./twophase
classify(int), defined before template
```

## 4.12-A {#hw-4-12-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-12-a)

**思路**：类内定义的友元随实例化生成**非模板**函数，不在命名空间作用域可见，只能被 ADL 精确捞到。

1. `operator<<` 能被 `cout << a` 找到，全靠 ADL 通过 `a` 的类型把隐藏友元拉进候选。→ 知识点：[模板友元与 Barton-Nackman](../vol1-basics-cpp11-14/07-friends-and-barton-nackman.md)「友元注入」
2. `Interval<int> == Interval<double>` 编译失败：`Box<int>` 的 `operator==` 只接受 `Box<int>`，不跨界。这就是隐藏友元的安全性——只在实参类型精确匹配时出现，别的时候完全透明。→ 知识点：[模板友元与 Barton-Nackman](../vol1-basics-cpp11-14/07-friends-and-barton-nackman.md)「隐藏友元的『不跨界』是特性不是 bug」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra hidden.cpp -o hidden && ./hidden
true false
[1,5]
$ g++ -std=c++20 hidden_bad.cpp -o hb
hidden_bad.cpp:21:20: error: no match for 'operator==' (operand types are
'Interval<int>' and 'Interval<double>')
```

## 4.12-B {#hw-4-12-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-12-b)

**思路**：CRTP 提供「派生类类型」，友元注入提供「随实例化生成的运算符」——一对搭档。

1. `Comparable<Derived>` 里的四个 `friend` 没有独立模板头，写在类内部、参数用 `const Derived&`（类内简写）；实例化 `Comparable<Version>` 时生成四个接收 `const Version&` 的普通函数。→ 知识点：[模板友元与 Barton-Nackman](../vol1-basics-cpp11-14/07-friends-and-barton-nackman.md)、[CRTP](../vol1-basics-cpp11-14/09-crtp.md)「Mixin（混入）」
2. `Version` 只写了 `<` 和 `==`，其余四个自动补齐——验证输出四连。→ 知识点：[CRTP](../vol1-basics-cpp11-14/09-crtp.md)「Comparable mixin」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra comparable.cpp -o comparable && ./comparable
false true true true
```

## 4.13-A {#hw-4-13-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-4-13-a)

**思路**：别名模板是「透明的」——别名就是原类型；`_t`/`_v` 是标准库给的简写。

1. `is_same_v` 实锤 `Vec<int>` 与 `std::vector<int>` 同类型；`remove_reference_t` 与 `is_integral_v` 都是简写。→ 知识点：[别名模板与 using 声明](../vol1-basics-cpp11-14/08-alias-and-using.md)「别名模板不是新类型」
2. `_t` 别名模板（C++14 起）省掉 `typename ...::type`，`_v` 变量模板（C++17 起）省掉 `...::value`。→ 知识点：[别名模板与 using 声明](../vol1-basics-cpp11-14/08-alias-and-using.md)「C++14 的 \_t 和 \_v」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra alias.cpp -o alias && ./alias
v.size = 3
a[1] = 5
Vec<int> is vector<int>: true
remove_reference_t<int&> is int: true
is_integral_v<int>: true
```

## 4.13-B {#hw-4-13-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-13-b)

**思路**：①别名模板定位「纯转发」，不做分发，所以不许特化；②`using` 声明把基类名字绑定进派生类作用域。

1. `template <> using V<int> = long;` 报错措辞抽象但意思明确：别名模板不接受特化。真需要「按类型给不同别名」，用类模板（可特化）包一层嵌套 `using`。→ 知识点：[别名模板与 using 声明](../vol1-basics-cpp11-14/08-alias-and-using.md)「别名模板不能特化」
2. `using Base<T>::kDefault;` 之后裸名直接可用，不用每次 `this->`；它还能把基类的类型别名（`value_type` 这类）「继承」下来。→ 知识点：[别名模板与 using 声明](../vol1-basics-cpp11-14/08-alias-and-using.md)「using 在模板继承里」

**验证输出**：

```text
$ g++ -std=c++20 alias_bad.cpp -o ab
alias_bad.cpp:5:1: error: expected unqualified-id before 'using'
$ g++ -std=c++20 -Wall -Wextra using_base.cpp -o using_base && ./using_base
Base::greet
fetch = 42
```

## 4.14-A {#hw-4-14-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-14-a)

**思路**：`struct Dog : Animal<Dog>` 把派生类自己传进基类，基类里的 `Derived` 就是 Dog，`static_cast` 直接命中。

1. 安全性来自类型系统：老老实实写 `Animal<Dog>` 时转换一定合法；写歪成 `Animal<Other>` 时 `static_cast<Other*>` 通常直接编译失败（无关类型拒绝转换）。→ 知识点：[CRTP](../vol1-basics-cpp11-14/09-crtp.md)「static_cast 的安全假设」
2. CRTP 是编译期多态（实例化时钉死、直接调用甚至内联），虚函数是运行期多态（vtable 间接调用）；CRTP 干不了「运行时异构集合」——`Circle*` 和 `Square*` 塞不进同一个 `Shape*` 数组。→ 知识点：[CRTP](../vol1-basics-cpp11-14/09-crtp.md)「静态多态：编译期就知道具体类型」「虚函数和 CRTP 不互通」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra crtp.cpp -o crtp && ./crtp
Dog: Woof
Cat: Meow
```

## 4.14-B {#hw-4-14-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-14-b)

**思路**：CRTP 版被 -O2 内联成两条指令；虚函数版要过两次内存解引用加推测去虚化。

1. 逐行对比：CRTP 版 `mov $0x2a,%eax; ret`——两条指令零内存访问；虚函数版先 `mov (%rdi)` 取 vtable、再 `mov (%rax)` 取函数指针、比较、条件跳转，七条起跳加两次内存访问。「零开销」零在内联彻底、没有 vtable 解引用。→ 知识点：[CRTP](../vol1-basics-cpp11-14/09-crtp.md)「零开销的汇编证据」
2. 基类构造/析构期间派生类部分还没成形/已先亡，`static_cast<Derived*>(this)` 拿到的指针指向的是半成品或残骸，访问派生成员是未定义行为。真实场景：基类构造里想做「按派生类注册自己」的静态分派，或在析构里发一条带派生类型名的日志——都得改成虚函数或把调用推迟到对象完整后。→ 知识点：[CRTP](../vol1-basics-cpp11-14/09-crtp.md)「基类构造时派生类未完成」

**验证输出**：

```text
$ g++ -std=c++20 -O2 -c crtp_asm.cpp -o crtp_asm.o && objdump -d crtp_asm.o
0000000000000000 <_Z8use_crtpv>:
   0:   b8 2a 00 00 00   mov    $0x2a,%eax
   5:   c3               ret
```

## 4.15-A {#hw-4-15-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-15-a)

**思路**：`std::array` 定长存储 + `size_` 计数；裸指针就是合格的随机访问迭代器。

1. `sizeof == 40`：8 个 int 占 32 字节 + `size_` 8 字节，**没有任何堆指针**——对比 `std::vector` 至少三个指针加一次堆分配。→ 知识点：[综合项目:fixed_vector](../vol1-basics-cpp11-14/10-fixed-vector.md)「完整代码与实测」
2. `T*` 天生支持 `*`、`++`、`+n`、比较，`begin()/end()` 直接返回指针就满足了标准算法要的迭代器接口——「迭代器统一容器和算法」的直接体现。→ 知识点：[综合项目:fixed_vector](../vol1-basics-cpp11-14/10-fixed-vector.md)「元素访问与迭代器」
3. 零动态分配的价值：性能可预测（无分配器开销与碎片）、异常安全边界清晰（只在容量耗尽时抛）、缓存友好（连续存储且在对象自身内）。→ 知识点：[综合项目:fixed_vector](../vol1-basics-cpp11-14/10-fixed-vector.md)「零动态分配，为什么重要」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra fvec.cpp -o fvec && ./fvec
size = 5 capacity = 8
elements: 10 20 30 40 50
v[2] = 30
sizeof(FixedVector<int,8>) = 40
```

## 4.15-B {#hw-4-15-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-15-b)

**思路**：constexpr 成员让整个容器能在编译期跑；`try_push_back` 用 `optional` 把「满了」编码进返回类型。

1. `constexpr FixedVector<int,4> kVec = build();` 加两条 `static_assert`——容器在编译期就完成插入与随机访问。`throw` 能出现在 constexpr 函数里，只要**求值路径上不真正抛出**（真抛了常量求值就是编译错误）。→ 知识点：[综合项目:fixed_vector](../vol1-basics-cpp11-14/10-fixed-vector.md)「push_back 与边界处理」、[非类型模板参数](../vol1-basics-cpp11-14/05-non-type-parameters.md)
2. `try_push_back` 满了返回 `nullopt`，否则返回指向新元素的指针——对标 `inplace_vector` 的「抛异常 / try / unchecked」三档 API 里 try 那一档。→ 知识点：[综合项目:fixed_vector](../vol1-basics-cpp11-14/10-fixed-vector.md)「和 std::inplace_vector(C++26)对比」
3. **如实标注**：`std::inplace_vector` 是 **C++26** 标准库组件（特性宏 `__cpp_lib_inplace_vector`）。本机 libstdc++ 16（g++ 16.1.1 / clang++ 22.1.8）**已提供**该组件：`#include <inplace_vector>` 直接编译运行，特性宏实测值为 `202603`（与教材 fixed_vector 章写的现行值一致），第 ③ 问可本机直接实测。探测方法注意：旧版答案用的 `echo | g++ -std=c++26 -dM -E -x c++ - | grep inplace_vector` 是**坏探测**——`-dM -E` 只列出编译器预定义宏，库特性宏定义在 `<inplace_vector>` 头文件里，不包含头文件永远看不到，会误判成「本机未提供」。→ 知识点：[综合项目:fixed_vector](../vol1-basics-cpp11-14/10-fixed-vector.md)

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra fvec2.cpp -o fvec2 && ./fvec2
kVec.size() = 3
kVec[2] = 3
p1=10 p2=20 p3=nullopt
size = 2
$ g++ -std=c++26 -Wall -Wextra probe.cpp -o probe && ./probe
__cpp_lib_inplace_vector = 202603
size=3 cap=4 [0]=10
$ echo | g++ -std=c++26 -dM -E -x c++ - | grep -i inplace_vector || echo "(旧探测法无输出)"
(旧探测法无输出)
```

（`probe.cpp` 是 `#include <inplace_vector>` 后打印特性宏与一个 `inplace_vector<int,4>` 实际塞入结果的小程序；clang++ 22.1.8 同款运行宏值同为 `202603`。最后一行是旧探测法复现：`-dM -E` 只列编译器预定义宏，库特性宏藏在头文件里，所以 grep 无输出。）

## 4.16-A {#hw-4-16-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-4-16-a)

**思路**：部分初始化时未指定的字段走默认成员初始化器（没有就走值初始化）；**C++20 要求指定初始化器按声明顺序**——这一条与教材示例的 C99 写法冲突，实测是硬标准。

1. `NetConfig a{.port = 443, .use_tls = true};` 未指定的 `host` 走默认值 `"localhost"`；无默认值的 `Raw` 里未指定的 `t` 被零初始化成 `false`（编译器同时给出 `-Wmissing-field-initializers` 提醒——这正是「隐式零初始化可能不是你的本意」的注脚）。→ 知识点：[指定初始化器](../vol2-modern-cpp17/06-designated-initializers.md)「部分初始化和默认值」「警惕隐式的零初始化」
2. 乱序初始化被 C++20 硬拒：教材「乱序也没问题」是 C99 的语义，C++20（P0329）规定 designator 必须按声明顺序。**真实结果与教材表述不一致，按验证铁律如实报告并以此为准**。编译器差异注：上面「乱序硬拒」是 g++ 的行为；clang++ 22 默认只发 `-Wreorder-init-list` **警告**、编译照常通过（exit=0）——clang 用户请加 `-pedantic-errors` 复现标准要求的报错，或至少知晓这一差异。→ 知识点：[指定初始化器](../vol2-modern-cpp17/06-designated-initializers.md)（C99 与 C++20 差异，本题为教材纠正）

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra desig.cpp -o desig && ./desig
a: localhost:443 tls=true
r: p=80 t=false
$ g++ -std=c++20 desig_oo.cpp -o doo
desig_oo.cpp:12:59: error: designator order for field 'UART::baudrate'
does not match declaration order in 'UART'
   12 |     UART c{.parity = 0, .baudrate = 115200, .data_bits = 8};
$ clang++ -std=c++20 desig_oo.cpp -o doo_c   （clang++ 22 默认：仅警告、编译通过 exit=0）
desig_oo.cpp:12:37: warning: ISO C++ requires field designators to be specified in declaration
order; field 'parity' will be initialized after field 'baudrate' [-Wreorder-init-list]
   12 |     UART c{.parity = 0, .baudrate = 115200, .data_bits = 8};
      |                         ~~~~~~~~~~~~^~~~~~
$ clang++ -std=c++20 -pedantic-errors desig_oo.cpp -o doo_c2   （加 -pedantic-errors 后变 error）
desig_oo.cpp:12:37: error: ISO C++ requires field designators to be specified in declaration
order; field 'parity' will be initialized after field 'baudrate' [-Werror,-Wreorder-init-list]
   12 |     UART c{.parity = 0, .baudrate = 115200, .data_bits = 8};
      |                         ~~~~~~~~~~~~^~~~~~
```

## 4.16-B {#hw-4-16-b}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-16-b)

**思路**：①用户构造函数一出现，聚合定义被破坏；②「显式指定 > 默认成员初始化器 > 值初始化」的优先级链。

1. `Config` 有用户构造 → 非聚合 → 指定初始化被拒，报错点名「non-aggregate」。→ 知识点：[指定初始化器](../vol2-modern-cpp17/06-designated-initializers.md)「聚合类型要求」「与构造函数的配合」
2. `{.rate = 115200}` 覆盖 `rate`，`bits` 走默认 8——显式指定的值覆盖默认成员初始化器，未指定的走默认（没有默认才零初始化）。→ 知识点：[指定初始化器](../vol2-modern-cpp17/06-designated-initializers.md)「坑5：非静态成员初始化器的优先级」

**验证输出**：

```text
$ g++ -std=c++20 desig_bad.cpp -o db
desig_bad.cpp:12:28: error: designated initializers cannot be used with
a non-aggregate type 'Config'
$ g++ -std=c++20 -Wall -Wextra desig2.cpp -o desig2 && ./desig2
rate=115200 bits=8
```

## 4.17-A {#hw-4-17-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-17-a)

**思路**：视图是「懒、不拥有、可组合、O(1) 拷贝」的数据透镜，四个特征都能在这段代码里找到落点。

1. `filter | transform` 组合出偏移量序列；`iota | take`、`iota | drop` 各验证一个适配器。→ 知识点：[C++20 范围库基础与视图](../vol2-modern-cpp17/07-ranges-basics-and-views.md)「常用视图工厂函数」
2. 四个特征的落点：懒 = 定义 `valid` 时零遍历（下面没有任何输出）；不拥有 = 视图只引用 `temps`，改 `temps` 视图跟着变；可组合 = `|` 链；O(1) 拷贝 = 视图只存几个迭代器。→ 知识点：[C++20 范围库基础与视图](../vol2-modern-cpp17/07-ranges-basics-and-views.md)「视图（View）：零开销的数据透镜」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra views.cpp -o views && ./views
valid: 3 1 4 0 5 2
first3: 0 1 2
dropped: 4 5
```

## 4.17-B {#hw-4-17-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-17-b)

**思路**：①视图不拥有数据，底层容器先死视图就悬垂——ASan 一抓一个准；②转成容器后数据归容器所有，反复迭代无虞。

1. ASan 报告 `stack-use-after-return`：局部 vector 随函数返回销毁，返回的视图还攥着指向它的迭代器。→ 知识点：[C++20 范围库基础与视图](../vol2-modern-cpp17/07-ranges-basics-and-views.md)「坑1：视图的生命周期」
2. `std::ranges::to<std::vector<int>>()`（**C++23**）把惰性视图物化成真容器，两遍遍历各自完整。→ 知识点：[管道操作与 Ranges 实战](../vol2-modern-cpp17/08-ranges-pipeline-in-practice.md)「坑1：不要多次迭代同一管道」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -O1 -g -fsanitize=address dangling.cpp -o dangling && ./dangling
==951==ERROR: AddressSanitizer: stack-use-after-return on address ...
READ of size 8 at 0x... thread T0
    #5 ... in begin /usr/include/c++/16/ranges:1923
    #6 ... in main /tmp/cpp-v4-hw2/dangling.cpp:14
$ g++ -std=c++23 -Wall -Wextra to_vec.cpp -o to_vec && ./to_vec
first pass:  2 4 6 8
second pass: 2 4 6 8
```

## 4.18-A {#hw-4-18-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-18-a)

**思路**：管道把「过滤 → 转换 → 截取」读成一句话，全程零中间容器。

1. `|` 左边是 Range、右边是视图适配器（range adaptor object），返回新的视图；链条只是「处理链条」的描述，迭代才发生计算。→ 知识点：[管道操作与 Ranges 实战](../vol2-modern-cpp17/08-ranges-pipeline-in-practice.md)「管道操作符：Unix 哲学在 C++ 中的体现」
2. 先 filter 再 take 与先 take 再 filter **不一定相同**：用题面数据 `{120, 45, 230, 67, 340, 89, 56, 180}` 配本题 50~300 过滤器、take(4)——先 filter 再 take：过滤得 `{120, 230, 67, 89, 56, 180}`、截 4 个再 ×2 得 `{240, 460, 134, 178}`；先 take 再 filter：先截下 `{120, 45, 230, 67}`、过滤掉 45 再 ×2 得 `{240, 460, 134}`。两种顺序输出不同——`take` 先截时把 45 留在窗口里占掉了一个名额。顺序就是语义。→ 知识点：[管道操作与 Ranges 实战](../vol2-modern-cpp17/08-ranges-pipeline-in-practice.md)「基础管道」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra pipe.cpp -o pipe && ./pipe
240 460 134 178
$ g++ -std=c++20 -Wall -Wextra pipe2.cpp -o pipe2 && ./pipe2
filter-then-take: 240 460 134 178
take-then-filter: 240 460 134
```

## 4.18-B {#hw-4-18-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-18-b)

**思路**：自定义 range 想进管道，得先过 `std::ranges::input_range` 概念——而它的一条暗线要求迭代器可默认构造（`semiregular`）。

1. 没有默认构造的版本：`static_assert(std::ranges::input_range<Squares>)` 直接炸，报错深不见底、一路指向 `ranges::end` 无效（`sentinel_for` 检查卡在 `semiregular` 上）——这就是「range-for 能过、概念过不了」的典型。（知识点标注：「缺默认构造卡 `semiregular`」是教材外的延伸——教材该节只演示了缺后缀 `++` 的报错；本题结论来自实测。）→ 知识点：[迭代器模式](../vol4-generics-patterns/18-iterator.md)「这里先验证一下：它真能进 ranges 吗」、[使用 Concepts 约束模板](../vol3-metaprogramming-cpp20-23/02-constraining-templates.md)
2. 补 `SquareIterator() = default;` 与四个关联类型、前后缀 `++` 后，`input_range` 为真、管道正常出数。哨兵（end 迭代器）可能被默认构造出来再拷贝，所以「只用初值构造」的迭代器也被要求可默认构造。→ 知识点：[迭代器模式](../vol4-generics-patterns/18-iterator.md)「补全 concept 配套」一节

**验证输出**（坏版本报错节选，已截断）：

```text
$ g++ -std=c++20 -Wall -Wextra custom_range.cpp
custom_range.cpp:34:32: error: static assertion failed
   34 |     static_assert(std::ranges::input_range<Squares>);
  • the required expression 'std::ranges::_Cpo::end(__t)' is invalid
```

（补上默认构造后：）

```text
$ g++ -std=c++20 -Wall -Wextra custom_range.cpp -o custom_range && ./custom_range
input_range<Squares>: true
40 160 360 640
```

## 4.19-A {#hw-4-19-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-4-19-a)

**思路**：四种形式语义等价，差别只在写法与适用场景；`requires requires` 的外层是子句、内层是表达式。

1. 形式①最直观（约束就是现成 concept），②最灵活（可组合多条件），③最像普通函数，④当场描述要求、不用先起名。→ 知识点：[Concepts:把模板约束写进签名](../vol3-metaprogramming-cpp20-23/01-concepts.md)「四种语法形式」
2. `plus_self(std::string)` 能过形式④，因为 string 有 `operator+`——内层 requires 表达式对 `x + x` 成立。→ 知识点：[Concepts:把模板约束写进签名](../vol3-metaprogramming-cpp20-23/01-concepts.md)「形式④」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra four_forms.cpp -o four_forms && ./four_forms
6 9 12
abab
```

## 4.19-B {#hw-4-19-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-19-b)

**思路**：两份报错放一起，差别一目了然——enable_if 在讲它自己的内部机制，concept 在讲你的约束。

1. enable_if 版报错核心是「substitution of `enable_if_t` [with `_Cond = false`]」——通篇在讲 SFINAE 的内部展开，你得懂 `enable_if<false>` 没有 `type` 才能倒推出「string 不是数值」。→ 知识点：[Concepts:把模板约束写进签名](../vol3-metaprogramming-cpp20-23/01-concepts.md)「报错信息的对比」
2. concept 版点名两件事：`constraints not satisfied` + `required for the satisfaction of 'Numeric<T>' [with T = ...basic_string<char>]`——约束名和失败类型都直接给你。优势不在行数，在信息指向约束本身。→ 知识点：[Concepts:把模板约束写进签名](../vol3-metaprogramming-cpp20-23/01-concepts.md)「别拿行数当唯一标准」

**验证输出**（节选，已截断）：

```text
$ g++ -std=c++20 add_ei.cpp -o ei
add_ei.cpp:13:17: error: no matching function for call to 'add(std::string&, std::string&)'
  • candidate 1: 'template<class T, class> T add(T, T)'
      • template argument deduction/substitution failed:
        • /usr/include/c++/16/type_traits: In substitution of
          'template<bool _Cond, class _Tp> using std::enable_if_t = ...
          [with bool _Cond = false; _Tp = void]':
$ g++ -std=c++20 add_c.cpp -o ac
add_c.cpp:16:17: error: no matching function for call to 'add(std::string&, std::string&)'
  • candidate 1: 'template<class T>  requires  Numeric<T> T add(T, T)'
      • template argument deduction/substitution failed:
        • constraints not satisfied
          • add_c.cpp: In substitution of ... [with T = std::__cxx11::basic_string<char>]:
```

## 4.20-A {#hw-4-20-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-20-a)

**思路**：`Car` 的原子约束集合是 `Vehicle` 的真超集，所以 `Car` 蕴含 `Vehicle`，双满足时编译器选窄的。

1. `Bike` 只满足 `Vehicle` 走宽重载；`Sedan` 两个都满足但 `Car` 更特定（它把 `Vehicle` 的要求全包进去还多要一个 `honk`），选 `Car`。→ 知识点：[使用 Concepts 约束模板](../vol3-metaprogramming-cpp20-23/02-constraining-templates.md)「subsumption：编译器靠约束蕴含挑重载」
2. 互不蕴含的两个约束（比如 Swimmable 和 Flyable）碰上一个都满足的类型，编译器报 ambiguous——subsumption 只解决有包含关系的消歧。→ 知识点：[使用 Concepts 约束模板](../vol3-metaprogramming-cpp20-23/02-constraining-templates.md)「两个互不蕴含的约束：会歧义」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra subs.cpp -o subs && ./subs
a vehicle
a car
```

## 4.20-B {#hw-4-20-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-20-b)

**思路**：①subsumption 比的是规范化后的**原子约束集合**，不是名字；②`&&` 的真实角色是把两边原子约束**并集**进同一集合。

1. `C2 = C1<T>` 规范化后原子约束就是 `C1<T>` 本身，两个重载的集合相等、互不真包含，退回普通歧义。→ 知识点：[使用 Concepts 约束模板](../vol3-metaprogramming-cpp20-23/02-constraining-templates.md)「原子约束：决定 subsumption 的真正单位」
2. `C = A && B` 的原子集合 `{A, B}` 同时是 `{A}`、`{B}` 的超集，三选一命中 `C`（输出实锤）。→ 知识点：[使用 Concepts 约束模板](../vol3-metaprogramming-cpp20-23/02-constraining-templates.md)「&& 的真实角色」

**验证输出**：

```text
$ g++ -std=c++20 atomic.cpp -o at
atomic.cpp:22:6: error: call of overloaded 'g(int)' is ambiguous
  • there are 2 candidates
$ g++ -std=c++20 -Wall -Wextra conj.cpp -o conj && ./conj
C
```

## 4.21-A {#hw-4-21-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-21-a)

**思路**：四种成分各查一件事：操作存在、类型存在、返回类型满足约束、额外的编译期布尔。

1. `vector<int>` 四关全过；`int` 挂在第一关（没有 begin/end）；`vector<double>` 挂在嵌套要求（`value_type` 不是整型）。→ 知识点：[Requires 表达式深度解析](../vol3-metaprogramming-cpp20-23/03-requires-expressions.md)「requires 表达式的四种成分」
2. `std::integral<char>` 是 **true**（char 属于整数类型族），所以 `vector<char>` 满足这个 `Container`——想让 value_type 恰好是 int 得换 `std::same_as<typename T::value_type, int>`。→ 知识点：[Requires 表达式深度解析](../vol3-metaprogramming-cpp20-23/03-requires-expressions.md)「顺带提一个容易看走眼的点」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra container.cpp -o container && ./container
vector<int>:      true
int:              false
vector<double>:   false
```

## 4.21-B {#hw-4-21-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-21-b)

**思路**：①requires 表达式与 `decltype`/`sizeof` 同属不求值上下文；②对**具体类型**直接写是「立即求值」，失败就是硬错误，包进 concept 才走 SFINAE 友好路径。

1. `MentionsIncrement<int>` 求值时 `increment()` 根本没执行（counter 还是 0），真正的调用才 +1。→ 知识点：[Requires 表达式深度解析](../vol3-metaprogramming-cpp20-23/03-requires-expressions.md)「坑一：requires 表达式不求值」
2. `requires(std::string s) { s.nope(); }` 里 string 是具体类型，编译器直接去它身上找 `nope`，找不到就硬报错；包进 `HasNope` 概念后 T 是模板参数，失败优雅地变成 `false`。→ 知识点：[Requires 表达式深度解析](../vol3-metaprogramming-cpp20-23/03-requires-expressions.md)「坑二：对具体类型直接写，会硬错误」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra uneval.cpp -o uneval && ./uneval
concept 求值完毕,counter = 0
[副作用] increment 被调用了
真正调用后,counter = 1
$ g++ -std=c++20 neg_raw.cpp -o nr
neg_raw.cpp:3:44: error: 'std::string' ... has no member named 'nope'
$ g++ -std=c++20 -Wall -Wextra neg_ok.cpp -o neg_ok && ./neg_ok
全部断言通过
```

## 4.22-A {#hw-4-22-a}

**难度 L1** · 题面见 [homework](./01-homework.md#hw-4-22-a)

**思路**：Meyer's Singleton = 私有构造 + 函数内 static + delete 拷贝移动，一行锁不写。

1. `&a == &b` 为真、`a.next()`/`b.next()` 是同一个计数器的 1 和 2——实锤全程序只有一个实例。→ 知识点：[单例模式](../vol4-generics-patterns/01-singleton.md)「Meyer's Singleton」
2. 不 delete 拷贝/赋值的话，`Config c2 = c1;` 这种正常赋值就能造出第二个实例，「全局唯一」瞬间破功；C++11 起 magic statics 保证多线程首次进入时恰好一个线程初始化、其余阻塞——这就是一行锁不写的底气。→ 知识点：[单例模式](../vol4-generics-patterns/01-singleton.md)「第二步」「第三步」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra meyers.cpp -o meyers && ./meyers
true
1 2
```

## 4.22-B {#hw-4-22-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-22-b)

**思路**：magic statics 用语言机制兜住「只初始化一次」，300 线程齐抢也构造不了一次以上。

1. 构造计数稳定 1。机制是 C++11 [stmt.dcl] 的 static 局部变量线程安全初始化，不是锁、不是 `call_once`。→ 知识点：[单例模式](../vol4-generics-patterns/01-singleton.md)「这里先验证一下：magic statics 真的线程安全吗」
2. 构造器有重活时，其余线程会**阻塞等待**初始化完成——初始化慢则大家一起等。DCLP 被劝退：magic statics 已包办；且 `memory_order_consume` 被主流编译器降级成 acquire、语义对不上，可移植性稀烂。→ 知识点：[单例模式](../vol4-generics-patterns/01-singleton.md)「踩坑预警：DCLP 的老黄历」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -pthread mt_singleton.cpp -o mt_singleton && ./mt_singleton
construct_count = 1 (expect 1)
```

## 4.23-A {#hw-4-23-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-23-a)

**思路**：`with_*` 返回 `*this` 让链流动；`optional` 同时充当「容器」和「填没填的标志」；缺必填在 `build()` 拦下。

1. 完整链构造成功；缺 `size`/`base` 的链被 `build()` 抛异常拦住。→ 知识点：[构建器模式](../vol4-generics-patterns/02-builder.md)「第四步：流式构建器」
2. `optional` 双角色：`if (size_)` 判断有没有填、`*size_` 取值——省掉一堆 `is_xxx_set` 标志位。`return p;` 零拷贝：C++17 起 mandatory copy elision，同名局部对象直接在调用方栈帧上构造。构建器有可变状态，两个线程共用一个 `with_*` 就是数据竞争。→ 知识点：[构建器模式](../vol4-generics-patterns/02-builder.md)「std::optional 是个极好用的工具类」「RVO 真的省掉了那次拷贝吗」「别在多线程里复用同一个构建器」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra builder.cpp -o builder && ./builder
Pizza{厚底,12寸,芝士}
caught: missing required field
```

## 4.23-B {#hw-4-23-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-23-b)

**思路**：每填一个必填项就让构建器「变身」成新类型，`build()` 只存在于走完全部阶段的类型上——漏填与乱序都成了编译期错误。

1. 正常链 `create().with_a(1).with_b(2).build()` 得 3；漏填时报错在 `StageB` 没有 `build`（你还没走到 `StageFinal`）；乱序时报错在 `StageA` 没有 `with_b`。→ 知识点：[构建器模式](../vol4-generics-patterns/02-builder.md)「第五步：阶段式构建器」
2. 流式构建器的所有中间状态是同一个类型，编译器分不清「填了几个字段」，校验只能运行时；阶段式把状态写进类型流。代价：每个必填阶段一个 struct、字段在阶段间 move——必填项一多阶段爆炸。→ 知识点：[构建器模式](../vol4-generics-patterns/02-builder.md)「阶段式构建器的代价」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra staged.cpp -o staged && ./staged
3
$ g++ -std=c++20 staged_missing.cpp -o sm
staged_missing.cpp:28:41: error: 'struct StageB' has no member named 'build'
$ g++ -std=c++20 staged_order.cpp -o so
staged_order.cpp:28:32: error: 'struct StageA' has no member named 'with_b'; did you mean 'with_a'?
```

## 4.24-A {#hw-4-24-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-24-a)

**思路**：简单工厂把「new 哪个子类」的 switch 从调用方抽进一个静态方法，返回所有权干净的 `unique_ptr<基类>`。

1. 遍历两种形状各造一个，输出名字与面积。→ 知识点：[工厂方法与抽象工厂](../vol4-generics-patterns/03-factory-method-abstract-factory.md)「第一步：简单工厂」
2. 返回 `unique_ptr<基类>` 三理由：裸指针把 delete 甩给调用方（泄漏/悬空）；`unique_ptr<派生类>` 转不了基类方向；虚析构保证通过基类指针析构正确（`unique_ptr<Burger>` 析构时正走这条路）。简单工厂违反 OCP：加一种新形状要打开工厂改 switch。→ 知识点：[工厂方法与抽象工厂](../vol4-generics-patterns/03-factory-method-abstract-factory.md)「踩坑预警」「简单工厂违反开闭原则」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra factory.cpp -o factory && ./factory
got Circle, area=12.5664
got Square, area=9
```

## 4.24-B {#hw-4-24-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-24-b)

**思路**：工厂退化成 `key → lambda` 注册表，OCP 做到最轻；类型安全随之降级到运行时。

1. `"beef"` 命中返回对象；`"nope"` 查表失败安全返回 `nullptr`，调用侧必须处理空指针。→ 知识点：[工厂方法与抽象工厂](../vol4-generics-patterns/03-factory-method-abstract-factory.md)「第四步：函数式工厂」
2. 加新产品 = 注册表加一条 lambda，工厂类一行不改；但 key 拼错编译期查不出，要等运行时 `find` 失败——这就是函数式工厂的类型安全降级。注册表用 Meyer's Singleton 持有，**初始化**线程安全（magic statics）；运行期多线程注册还得加锁（`shared_mutex` 读多写少）。→ 知识点：[工厂方法与抽象工厂](../vol4-generics-patterns/03-factory-method-abstract-factory.md)「函数式工厂的类型安全是降级的」「把工厂注册表和静态局部变量一起用」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra ffunc.cpp -o ffunc && ./ffunc
'beef' -> Beef
'nope' -> null
```

## 4.25-A {#hw-4-25-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-25-a)

**思路**：切片是「用基类拷贝构造拷派生」的头号杀手；多态 `clone()` 让动态类型决定复制成谁。

1. `p->clone()` 走虚派发命中 `Spreadsheet::clone`，克隆体动态类型是 `Spreadsheet`（`typeid` 输出 mangled 名 `11Spreadsheet`），`rows` 完整保留。→ 知识点：[原型模式](../vol4-generics-patterns/04-prototype.md)「第三步：把克隆内置进类——多态 clone()」
2. 若写 `new Document(*p)`，`*p` 静态类型是 `Document`，派生部分（`rows`）被无声切掉——这就是 slicing。协变返回类型允许派生类 override 时返回更具体的指针/引用，`Spreadsheet* clone()` 合法。→ 知识点：[原型模式](../vol4-generics-patterns/04-prototype.md)「第二步：事情出问题了——切片」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra clone.cpp -o clone && ./clone
dynamic type = 11Spreadsheet
title = budget
rows = 12
```

## 4.25-B {#hw-4-25-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-25-b)

**思路**：①`unique_ptr` 版 clone 的隐式转换在函数体里找回具体类型；②默认拷贝构造对 `shared_ptr` 成员是共享，不是深拷。

1. `make_unique<Button>(*this)` 返回的 `unique_ptr<Button>` 隐式转成 `unique_ptr<Widget>`（派生到基类的转换模板），动态类型保留。`unique_ptr<Derived>` 与 `unique_ptr<Base>` 是平级的独立类型，所以智能指针之间不支持协变返回。→ 知识点：[原型模式](../vol4-generics-patterns/04-prototype.md)「那个返回类型：为什么我推荐你写 `std::unique_ptr<Base>`」
2. `b.get() == 999`：默认拷贝构造只复制 `shared_ptr` 的引用计数，两个对象底层同一块。clone 的实现者要逐个成员过「值 / 共享 / 所有权」三问。→ 知识点：[原型模式](../vol4-generics-patterns/04-prototype.md)「真正的坑在后面：clone 不是无脑 new」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra clone_up.cpp -o clone_up && ./clone_up
dynamic type = 6Button
Button: OK
$ g++ -std=c++20 -Wall -Wextra shallow.cpp -o shallow && ./shallow
b.get() = 999 (跟着 a 一起变了)
```

## 4.26-A {#hw-4-26-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-26-a)

**思路**：对象适配器靠组合把「逻辑坐标流」翻译成「屏幕坐标流」，两边谁都不用改。

1. 两个逻辑点缩 2 倍转成屏幕点，Renderer 画了 2 个——`drawn == 2`。→ 知识点：[适配器模式](../vol4-generics-patterns/05-adapter.md)「第三步：对象适配器」
2. 三件套：Target = `Renderer` 期望的「屏幕点迭代器区间」（适配器对外的 `points()`），Adaptee = `Renderer` 本身，Adapter = `PointAdapter`。对象适配器三优势：运行时可换实现、不要求能继承 Adaptee、组合比多重继承更少惊喜。构造时翻译（急）后续访问零开销但源数据变化就过时；惰性翻译省内存但每次访问要算。→ 知识点：[适配器模式](../vol4-generics-patterns/05-adapter.md)「第一步：先看清三件套」「类适配器为什么不推荐」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra adapter.cpp -o adapter && ./adapter
drawn = 2 (expect 2)
```

## 4.26-B {#hw-4-26-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-26-b)

**思路**：持引用的适配器把「源数据生命周期」这个隐式约束强加给每个调用方，编译器毫无检查；ASan 替你把它钉在案发现场。

1. `RefAdapter bad(std::vector<int>{1,2,3});` 的临时 vector 在语句结束就销毁，`bad.sum()` 读到悬垂引用——ASan 报 `stack-use-after-scope`，精确到 `RefAdapter::sum()` 那一行。→ 知识点：[适配器模式](../vol4-generics-patterns/05-adapter.md)「对象适配器的生命周期坑」
2. `CopyAdapter` 构造时复制，与源数据生命周期解耦，同场景安然输出 6。持引用只该在「能用文档/类型系统保证源数据长寿」时用（比如源是长寿单例），默认走拷贝。→ 知识点：[适配器模式](../vol4-generics-patterns/05-adapter.md)「默认就走构造时复制这条安全路」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -O1 -g -fsanitize=address refadapter.cpp -o refadapter && ./refadapter
==784==ERROR: AddressSanitizer: stack-use-after-scope on address ...
    #2 ... in RefAdapter::sum() const /tmp/cpp-v4-hw3/refadapter.cpp:11
    #3 ... in main /tmp/cpp-v4-hw3/refadapter.cpp:37
$ g++ -std=c++20 -Wall -Wextra refgood.cpp -o refgood && ./refgood
good.sum() = 6
```

## 4.27-A {#hw-4-27-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-27-a)

**思路**：抽象（消息）持实现（通道），两个维度各自扩展、运行时随便组合。

1. 同一句话走两条通道输出不同前缀——类数量从「消息数 × 通道数」退回「消息数 + 通道数」。→ 知识点：[桥接模式(pImpl)](../vol4-generics-patterns/06-bridge.md)「第二步：抽出实现接口——两条腿走路」
2. 「桥」是 `Message` 里的 `std::unique_ptr<Channel> ch_`。Bridge 是事前设计（一开始就拆两条继承链），Adapter 是事后补救（两个现成的对不上的类之间垫翻译层）。→ 知识点：[桥接模式(pImpl)](../vol4-generics-patterns/06-bridge.md)「Bridge vs Adapter」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra bridge.cpp -o bridge && ./bridge
[tcp] hi
[udp] hi
```

## 4.27-B {#hw-4-27-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-27-b)

**思路**：`unique_ptr<不完整类型>` 的析构需要完整类型，所以析构/move 都得挪到 `Impl` 完整的地方；noexcept move 是 vector 敢用 move 搬家的前提。

1. `~Widget() = default;` 写在头文件里，编译器在「Impl 还不完整」处实例化 `unique_ptr` 的析构，`static_assert(sizeof(_Tp)>0)` 当场炸。→ 知识点：[桥接模式(pImpl)](../vol4-generics-patterns/06-bridge.md)「pImpl 的第二步：用 std::unique_ptr 接管生命周期」
2. 完整版：`sizeof(Widget) == 8`（一个指针）；拷贝走 `Impl::clone()` + copy-and-swap（强异常安全）；`b.get() == 42` 深拷贝独立。→ 知识点：[桥接模式(pImpl)](../vol4-generics-patterns/06-bridge.md)「pImpl 的第三步」「pImpl 到底换来了什么」
3. noexcept 对照：move 标 `noexcept` 的版本 push_back 1000 个只发生 1 次拷贝（`b = a` 那一次）；不标的版本 **1023** 次——vector 的强异常安全要求「move 可能抛就退回 copy」，每次 copy 就是一次 `clone()` 堆分配。→ 知识点：[桥接模式(pImpl)](../vol4-generics-patterns/06-bridge.md)「pImpl 的第四步：给 move 标上 noexcept」
4. 三件好处：编译依赖骤降（头文件不再传染重型头）、ABI 稳定（sizeof 恒 8，内部随便改）、真正的封装（私有成员的**类型**都藏进 cpp）。→ 知识点：[桥接模式(pImpl)](../vol4-generics-patterns/06-bridge.md)「收口」

**验证输出**：

```text
$ g++ -std=c++20 main_bad.cpp -o wb
/usr/include/c++/16/bits/unique_ptr.h:90:23: error: invalid application of
'sizeof' to incomplete type 'Widget::Impl'
   90 |         static_assert(sizeof(_Tp)>0,
$ g++ -std=c++20 -Wall -Wextra main_ok.cpp widget.cpp -o w_ok && ./w_ok
sizeof(Widget) = 8
sizeof(void*)  = 8
b.get() = 42
Impl 拷贝次数 = 1 (noexcept move:扩容走 move,应只有 b=a 的 1 次)
$ g++ -std=c++20 -Wall -Wextra main_nx.cpp widget_nx.cpp -o w_nx && ./w_nx
Impl 拷贝次数 = 1023 (无 noexcept:扩容退化为拷贝)
```

## 4.28-A {#hw-4-28-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-28-a)

**思路**：装饰器与被装饰对象同一接口 → 无限嵌套；转发链上外层先动手。

1. `Star(Quote(Plain))` 链：Star 在最外层先动手，输出 `***"hi"***`（星号包最外、引号在内）。→ 知识点：[装饰器模式](../vol4-generics-patterns/07-decorator.md)「第五步：把装饰器串成一条链」
2. 同一接口是「装饰器可以直接替换被装饰对象、外部感觉不到」的前提。`shared_ptr` 三理由：多态必须指针/引用；层数运行时才定，需要一个能指向任何实现的句柄；多个装饰器共享同一底层对象、谁都不该独占。最隐蔽笔误：算出改造后的 `result` 却转发成原始 `text`——编译器不报、装饰静默失效。→ 知识点：[装饰器模式](../vol4-generics-patterns/07-decorator.md)「第二步」「为什么非要用 shared_ptr 不可」「一个极易踩的笔误」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra decorator.cpp -o decorator && ./decorator
***"hi"***
```

## 4.28-B {#hw-4-28-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-28-b)

**思路**：mixin 用继承把链在编译期拍成一个具体类型，转发全部内联；代价搬进类型系统。

1. `Decorated` 无虚函数（`is_polymorphic_v` 为 false）、输出与动态版一致。→ 知识点：[装饰器模式](../vol4-generics-patterns/07-decorator.md)「第六步：编译期组合——模板 mixin」
2. `objdump` 里对 `simple_print`/`Mixin` 的 call 是 **0 次**——整条链被内联成字符串构造的机器码，运行时不存在「装饰」这个动作，这就是「零开销抽象」。→ 知识点：[装饰器模式](../vol4-generics-patterns/07-decorator.md)「整条装饰链在编译期就被内联掉了」
3. 类型爆炸两条表现：每种组合是互不兼容的新类型（`StarMixin<QuoteMixin<...>>` ≠ `QuoteMixin<StarMixin<...>>`），塞不进统一容器、运行时换不了链。→ 知识点：[装饰器模式](../vol4-generics-patterns/07-decorator.md)「mixin 的代价：类型爆炸」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -O2 mixin.cpp -o mixin && ./mixin
***"hi"***
$ objdump -d -C mixin.o | grep -cE "call.*simple_print|call.*Mixin"
0
```

## 4.29-A {#hw-4-29-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-29-a)

**思路**：`Dir` 自己也是 `Node`，`print` 里那句 `child->print(depth+1)` 就是隐式递归——整棵树对调用方就是一个对象。

1. 调用方只调了一次 `root.print(0)`，子树自动展开、缩进逐层加深。→ 知识点：[组合模式](../vol4-generics-patterns/08-composite.md)「第二步：Group 自己也是个 Graphic」
2. `unique_ptr` 所有权：`add` 收 `unique_ptr` 转交所有权，`Dir` 析构时整棵子树递归释放——GoF 原版裸指针 + `new` 是实打实的泄漏。递归遍历在「恶意深嵌套」场景会爆栈，改成显式栈遍历。→ 知识点：[组合模式](../vol4-generics-patterns/08-composite.md)「三件事别忘」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra composite.cpp -o composite && ./composite
+ root
  - a.txt
  + src
    - main.cpp
    - util.cpp
  - README
```

## 4.29-B {#hw-4-29-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-29-b)

**思路**：透明式把错误推到运行期、接口统一；安全式把错误压在编译期、调用方要 `dynamic_cast`。

1. 透明式：叶子的 `add` 继承基类默认实现，调用时抛 `logic_error`，catch 拿到明确信息。→ 知识点：[组合模式](../vol4-generics-patterns/08-composite.md)「透明式：add 进基类，叶子抛异常」
2. 安全式：`add` 只在 `Group` 上，`Circle c; c.add(...)` 直接编译失败。→ 知识点：[组合模式](../vol4-generics-patterns/08-composite.md)「安全式：add 只在 Group 上」
3. 无论选哪种，叶子的 `add` 绝不能**静默忽略**——调用方以为加进去了、其实什么都没发生，是最阴险的 bug；要暴露就抛异常或 assert。选择标准：多数时候只「用」树（调 draw）选安全式，频繁增删节点选透明式。→ 知识点：[组合模式](../vol4-generics-patterns/08-composite.md)「怎么选」「第二，叶子上加孩子这种无意义操作」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra transp.cpp -o transp && ./transp
caught: add() not supported on a leaf
$ g++ -std=c++20 safe_bad.cpp -o sb
safe_bad.cpp:19:7: error: 'class Circle' has no member named 'add'
```

## 4.30-A {#hw-4-30-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-30-a)

**思路**：门面把「顺序编排 + 生命周期 + 错误处理」收进一个入口，客户端只喊一声。

1. `brew()` 正序全开、`shutdown()` 逆序全关——后开的先关：渲染/管路还有数据依赖时，先把下游关掉再关上游，防止谁先死谁悬空。→ 知识点：[外观模式](../vol4-generics-patterns/09-facade.md)「第二步：把错误处理和资源清理也收进来」
2. 外观的本质是「把已有子系统按职责封装成统一入口」，不是发明新能力。门面里不应出现业务决策代码（「投影仪预热 30 秒」是 Projector 的职责，门面只负责按顺序喊它）。判断 God Object 的标准：门面里开始出现具体业务参数/决策，就是变质。→ 知识点：[外观模式](../vol4-generics-patterns/09-facade.md)「别走极端：facade 不是用来重写子系统的」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra facade.cpp -o facade && ./facade
Heater on
Pump on
Lamp on
brewing...
Lamp off
Pump off
Heater off
```

## 4.30-B {#hw-4-30-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-30-b)

**思路**：闭集场景（子系统种类编译期定死）用 variant 替代多态，值语义 + 编译期分派。

1. `vector<Part>` 连续内存零堆分配（对象层面），`std::visit` 编译期生成所有分支、无虚表查找。→ 知识点：[外观模式](../vol4-generics-patterns/09-facade.md)「进阶：用 std::variant 替代多态容器」
2. 代价：`Part` 的类型集合编译期钉死，运行时想插第五种组件得改 variant 列表重新编译；要运行时动态扩展（插件）就退回多态 + 基类指针。→ 知识点：[外观模式](../vol4-generics-patterns/09-facade.md)「取舍很清晰」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra facade_v.cpp -o facade_v && ./facade_v
Heater on
Pump on
Lamp on
```

## 4.31-A {#hw-4-31-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-31-a)

**思路**：find-or-insert 池保证同一 key 只造一份；共享所有权交给 `shared_ptr`。

1. 两次 `get("你")` 指针相等、`"你"` 与 `"好"` 不等、池大小 2——共享是真实指针层面的事。→ 知识点：[享元模式](../vol4-generics-patterns/10-flyweight.md)「第三步：享元工厂」「这里先验证一下：共享是真的吗」
2. 内部状态 = 字的内容（共享），外部状态 = 出现位置（不存进对象，用的时候传）。`shared_ptr` 让工厂与调用方共享所有权、对象生命周期跟着引用走；GoF 原版裸指针既泄漏又悬空；`weak_ptr` 会让对象频繁回收重建，把「省构造」的收益抹掉。甜区：共享状态足够重、对象数量足够多——1 字节 char 上享元（8 字节指针）得不偿失。→ 知识点：[享元模式](../vol4-generics-patterns/10-flyweight.md)「第二步：把状态拆开」「为什么用 shared_ptr」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra fly.cpp -o fly && ./fly
a1.get() == a2.get() : true
a1.get() == b1.get() : false
pool size : 2
```

## 4.31-B {#hw-4-31-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-31-b)

**思路**：find-or-insert 的「查」和「插」之间没有同步，64 线程齐抢同一个 key 就是 TOCTOU 竞态；一把锁把整个操作包成原子即可。

1. 无锁版本机实测构造 **6** 次（理想 1）。「池子终态大小还是 1」会骗人——`operator[]` 互相覆盖收敛了条目，但构造的副作用（加载纹理、解析配置、分配内存）已经重复发生，那才是享元最想省的。→ 知识点：[享元模式](../vol4-generics-patterns/10-flyweight.md)「踩坑预警：这个工厂不是线程安全的」
2. 加锁版构造 1 次。不推荐「锁外无锁 find 一次」：对 `unordered_map` 的并发读写本身就是数据竞争（未定义行为），要无锁读得换并发容器或 `atomic<shared_ptr>`。争用低（热点 key 只构造一次），一把锁最划算。→ 知识点：[享元模式](../vol4-generics-patterns/10-flyweight.md)「改对：加一把锁的线程安全工厂」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -pthread fly_race.cpp -o fly_race && ./fly_race
构造次数 = 6 (理想 1)
$ g++ -std=c++20 -Wall -Wextra -pthread fly_safe.cpp -o fly_safe && ./fly_safe
构造次数 = 1 (理想 1)
```

## 4.32-A {#hw-4-32-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-32-a)

**思路**：虚拟代理把昂贵构造推迟到第一次真正使用；`load_count` 计数是懒加载的直接证据。

1. `display()` 前计数为 0（没加载）、两次 display 后计数 1——只加载一次。→ 知识点：[代理模式](../vol4-generics-patterns/11-proxy.md)「第二步：虚拟代理」
2. 代理与真实对象同形（都实现 `Image`、都有 `display()`），调用方无感。单线程 `if (!real_)` 在多线程是数据竞争：两个线程同时读到空、同时构造、指针互相覆盖、前一个泄漏；正确姿势是 `std::call_once`（与 magic statics 同族机制）。→ 知识点：[代理模式](../vol4-generics-patterns/11-proxy.md)「踩坑预警：并发下的懒加载」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra proxy.cpp -o proxy && ./proxy
before display, load_count = 0
Loading cat.png (expensive)
Displaying cat.png
Displaying cat.png
after 2 displays, load_count = 1 (expect 1)
```

## 4.32-B {#hw-4-32-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-32-b)

**思路**：①`use_count()` 会变，「读到 1」和「动手改」之间的窗口里别人可能把计数撑到 2——TOCTOU；②`unique()` 按标准已移除，本机能过只是扩展。

1. 一个线程疯狂拷贝/释放、另一个线程观察计数，本机实测观察到 `use_count > 1` 约 **36913** 次（**单次观测值**：数量级随循环结构浮动，审查用更紧的循环复跑约 820 万次；现象本身稳定）。`shared_ptr` 的原子引用计数只保证计数本身正确，不保证「计数为 1 时不会马上有人来拷贝」。→ 知识点：[代理模式](../vol4-generics-patterns/11-proxy.md)「写时复制（COW）」
2. `p.unique()` 在本机 libstdc++ 16 的 `-std=c++20`/`-std=c++23` 下**都能编译通过**（实现把它当扩展保留，无警告）——但这不代表可移植：标准 C++17 起弃用、C++20 起移除，换实现或未来版本就是编译错误。现代代码别写。→ 知识点：[代理模式](../vol4-generics-patterns/11-proxy.md)「shared_ptr::unique() 的老黄历」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra -pthread cow_race.cpp -o cow_race && ./cow_race
saw use_count > 1 about 36913 times
$ g++ -std=c++20 uni.cpp -o uni; echo "c++20 exit=$?"
c++20 exit=0
$ g++ -std=c++23 uni.cpp -o uni23; echo "c++23 exit=$?"
c++23 exit=0
```

（`36913` 是本机单次观测值：两线程各自的循环体结构不同，具体数字会在一个到几个数量级间浮动，审查用更紧的循环实测约 820 万次——本题验证的是「能看到 `>1`」这个现象，不是具体数字。）

（如实报告：本机 libstdc++ 16 仍把 `shared_ptr::unique()` 保留为扩展、编译通过；按 ISO 标准它 C++20 起已移除，本题以标准为准。）

## 4.33-A {#hw-4-33-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-33-a)

**思路**：策略对象化后，Context 只依赖接口，运行期换策略 = 换一个对象。

1. 先大写后小写，`set()` 换策略、`run()` 行为跟着变——「运行时切换」就是那行 `set`。→ 知识点：[策略模式](../vol4-generics-patterns/12-strategy.md)「第二步：抽出策略接口」
2. 动态策略的代价藏在 `->` 里：虚调用先取 vptr 再查槽位再跳转，击穿内联。相对 if/else 的隐藏红利：塞假策略即可单测 Context 的流程，可测试性白送。→ 知识点：[策略模式](../vol4-generics-patterns/12-strategy.md)「代价一：虚调用的间接跳转」「共性收益」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra strategy.cpp -o strategy && ./strategy
HELLO
hello
```

## 4.33-B {#hw-4-33-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-33-b)

**思路**：模板策略零开销但编译期定死；concept 把「策略契约」从模板深处拉到调用点。

1. `UpperPolicy` 满足 `Formatter`，正常跑。→ 知识点：[策略模式](../vol4-generics-patterns/12-strategy.md)「第三步：把策略搬进编译期」
2. `BadPolicy::format` 返回 `const char*`，不满足返回类型要求 `-> std::same_as<std::string>`，报错**指向实例化那一行**并点名 `Formatter<F>` 约束失败——这就是 concept 相对裸模板的价值。（注意：若把返回类型约束退成 `-> std::convertible_to<std::string>`，`const char*` 能隐式转成 `string`、编译直接通过，题面要求的报错根本不会出现——`same_as` 必须点名写死。）→ 知识点：[策略模式](../vol4-generics-patterns/12-strategy.md)「这里先验证一下：concept 怎么给策略上编译期约束」
3. 模板策略零开销：`Policy::format` 被内联进 `run`，无虚表、无间接调用、无堆分配；硬伤：`Processor2<UpperPolicy>` 永远大写，运行期换不了。concept 把「策略该长什么样」从「写错了在模板深处爆炸」提升成「调用点一眼可见的契约违约」。→ 知识点：[策略模式](../vol4-generics-patterns/12-strategy.md)「三条路怎么选」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra policy.cpp -o policy && ./policy
HI
$ g++ -std=c++20 policy_bad.cpp -o pb
policy_bad.cpp:24:25: error: template constraint failure for
'template<class F>  requires  Formatter<F> class Processor2'
   24 |     Processor2<BadPolicy> bad;
  • required for the satisfaction of 'Formatter<F>' [with F = BadPolicy]
```

## 4.34-A {#hw-4-34-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-34-a)

**思路**：命令把「执行」和「撤销」打包成一个有状态的对象；撤销栈 LIFO 弹反操作。

1. +5、+3 后值 8，撤销两次回到 5、0。→ 知识点：[命令模式](../vol4-generics-patterns/13-command.md)「第二步：把动作封装成对象」
2. `execute()` 不能 const：它要顺手记「撤销所需的信息」进自己的成员，const 就把自己堵死了。动作从「一闪即逝的函数调用」变成「有身份、有状态、可存储的对象」——于是撤销、排队、回放都成为可能。命令持接收者引用，接收者必须比命令长寿，否则撤销时 use-after-free。→ 知识点：[命令模式](../vol4-generics-patterns/13-command.md)「execute() 不是 const」「命令持有接收者的引用」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra command.cpp -o command && ./command
value = 8
after undo = 5
after undo = 0
```

## 4.34-B {#hw-4-34-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-34-b)

**思路**：①宏命令是组合模式的命令版，undo 必须逆序；②move_only_function 匹配「一个命令只执行一次、撤销一次」的独占语义。

1. 三个 `AddCommand(1/2/3)` 执行后 6、宏撤销后 0。undo 逆序是栈的 LIFO：最后执行的改动了最新状态，必须先撤；正序 undo 会砍到不存在的量、状态悄悄错乱。→ 知识点：[命令模式](../vol4-generics-patterns/13-command.md)「第三步：把多个动作打包——宏命令」
2. 闭包版命令用 `std::move_only_function`（C++23）：`std::function` 要求被包装目标可拷贝，而命令常要捕获 `unique_ptr`/文件句柄这类 move-only 资源。→ 知识点：[命令模式](../vol4-generics-patterns/13-command.md)「第四步：函数式命令——闭包就是命令」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra macro.cpp -o macro && ./macro
after execute = 6
after undo = 0
$ g++ -std=c++23 -Wall -Wextra funccmd.cpp -o funccmd && ./funccmd
after execute: 'World'
after undo: ''
```

## 4.35-A {#hw-4-35-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-35-a)

**思路**：`variant` 存状态、`visit` 的访问者按当前状态算出下一个状态——转换被局部化在「从这个状态出发」的重载里。

1. 四次 tick 走出完整环。→ 知识点：[状态机模式](../vol4-generics-patterns/14-state.md)「第三步：状态集合编译期定死——variant + visit」
2. 编译期强保证：访问者必须为 variant 里**每种**类型提供重载，漏一个状态 = 编译错误（对比 enum + switch 漏 case 只沉默）。相比 `shared_ptr` 版省掉了每次转换的堆分配（variant 按值存）。状态带数据：给 `Yellow` 加 `int remain`，visit 的重载里必须处理它，编译器盯住完整性。→ 知识点：[状态机模式](../vol4-generics-patterns/14-state.md)「visit 的闭集在这里变成了一条编译期契约」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra traffic.cpp -o traffic && ./traffic
Red -> Green
Green -> Yellow
Yellow -> Red
Red -> Green
```

## 4.35-B {#hw-4-35-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-35-b)

**思路**：`std::visit` 的覆盖检查发生在模板实例化里——漏一个类型，报错点虽然深，但会明确告诉你是哪个类型没有对应的调用。

1. 报错出现在 `<variant>` 深处的 `__check_visitor_results`，`_Idxs = {0, 1, 2}` 三个状态它逐个检查，漏掉的 `C` 让整个 visit 编不过。这检查是编译期模板约束，绝不可能漏到运行时。→ 知识点：[状态机模式](../vol4-generics-patterns/14-state.md)「覆盖检查是编译期完成的」、[访问者模式](../vol4-generics-patterns/16-visitor.md)「它最大的杀手锏：编译期穷举检查」
2. 默认兜底用泛型 lambda（`operator()` 是模板，对任何类型可推导）。对比经典访问者「漏写 visit 让类变抽象、报错绕几层」，variant 版把「该补的地方」一次性点全。→ 知识点：[访问者模式](../vol4-generics-patterns/16-visitor.md)「想要默认分支怎么办」

**验证输出**（报错节选，已截断）：

```text
$ g++ -std=c++20 visit_missing.cpp -o vm
/usr/include/c++/16/variant: In instantiation of 'constexpr bool
std::__detail::__variant::__check_visitor_results(std::index_sequence<_Idx ...>)
[with _Visitor = Overloaded<...lambda(const A&)..., ...lambda(const B&)...>;
_Variant = std::variant<A, B, C>&; long unsigned int ..._Idxs = {0, 1, 2}]':
visit_missing.cpp:21:15:   required from here
```

## 4.36-A {#hw-4-36-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-36-a)

**思路**：备忘录 = 发起者的嵌套类 + 私有构造/字段 + `friend class 发起者`，对外是不透明黑盒。

1. 快照-恢复流程完整：`"Hello, world"` 回到 `"Hello"`。→ 知识点：[备忘录模式](../vol4-generics-patterns/15-memento.md)「第二步：把备忘录做成黑盒」
2. 外部读 `snap->content_` 被编译器拦下（`is private within this context`）。宽接口 = 只有发起者能读写状态；窄接口 = 管理者拿到的是不透明句柄。核心承诺是「不可能被改」而不是「希望大家别改」——公开字段的备忘录等于把内部表示向全程序摊开。`make_shared` 为什么不行：它内部的 `allocator_traits::construct` 不是 `friend`，下一题实证。→ 知识点：[备忘录模式](../vol4-generics-patterns/15-memento.md)「这里有个常被忽略的封装坑」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra memento.cpp -o memento && ./memento
before restore: "Hello, world"
after restore:  "Hello"
$ g++ -std=c++20 memento_break.cpp -o mb
memento_break.cpp:34:24: error: 'std::string Editor::Memento::content_'
is private within this context
```

## 4.36-B {#hw-4-36-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-36-b)

**思路**：①`make_shared` 的构造调用发生在标准库分配基础设施里，不在 friend 白名单；②历史栈是一条带游标的线性序列，非末尾插入丢弃 redo 分支。

1. `make_shared<Memento>(...)` 报错堆栈一路穿过 `_Construct → allocator_traits::construct → _Sp_counted_ptr_inplace`——私有构造在这些上下文里不可见。修复用 `shared_ptr<Memento>(new Memento(...))`，发起者自己直接调私有构造（它就是 friend）。→ 知识点：[备忘录模式](../vol4-generics-patterns/15-memento.md)「踩坑预警：make_shared 撞上私有构造」
2. 会话轨迹：`Hello, world` → undo 到 `Hello` → redo 回 `Hello, world` → 打 `!!!` 并快照 → `can_redo = 0`（redo 分支被丢）、`can_undo = 1`。非末尾插入新快照 = 时间线开新岔路，旧的重做未来不该存在——否则重做栈与实际状态对不上。→ 知识点：[备忘录模式](../vol4-generics-patterns/15-memento.md)「实战：带撤销/重做的历史栈」

**验证输出**：

```text
$ g++ -std=c++20 memento_make.cpp -o mm
/usr/include/c++/16/bits/stl_construct.h: In instantiation of 'constexpr void
std::_Construct(_Tp*, _Args&& ...) [with _Tp = Editor::Memento; ...]':
  845 |         { std::_Construct(__p, std::forward<_Args>(__args)...); }
（报错节选:构造发生在标准库分配路径上,不在 friend 白名单内）
$ g++ -std=c++20 -Wall -Wextra history.cpp -o history && ./history
now:     "Hello, world"
undo:    "Hello"
redo:    "Hello, world"
edit:    "Hello, world!!!"
can_redo = 0 (expect 0)
can_undo = 1 (expect 1)
```

## 4.37-A {#hw-4-37-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-37-a)

**思路**：类型集合闭合时，variant + visit 是零侵入、编译期穷举、无虚函数的现代默认。

1. `Overloaded` 把两个 lambda 捏成重载组，visit 按判别式分派，总面积 24.5664。→ 知识点：[访问者模式](../vol4-generics-patterns/16-visitor.md)「第三步：用 std::variant + std::visit 换一条路」
2. 分发机制是「读判别式字节 + 比较跳转」，没有虚表查找。`Overloaded` 依赖 C++17 的变长 using 声明（`using Ts::operator()...;`）和 CTAD 推导指引。类型集合闭合 = 编译期就能列全所有形状；运行时动态加类型两者都给不了，得靠类型擦除 + 注册表。→ 知识点：[访问者模式](../vol4-generics-patterns/16-visitor.md)「它是怎么分发的」「Overloaded helper 需要 C++17 的推导指引」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra vvisit.cpp -o vvisit && ./vvisit
total = 24.5664
```

## 4.37-B {#hw-4-37-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-37-b)

**思路**：双分发 = 虚函数选 accept → `*this` 静态类型选 visit 重载 → 虚函数选 visit 实现，三段接力。

1. `PerimeterVisitor` 对 `{Circle(2), Rect(3,4)}` 得 26.5664。→ 知识点：[访问者模式](../vol4-generics-patterns/16-visitor.md)「第二步：经典访问者」
2. 基类写 `accept` 时 `*this` 静态类型是 `Shape&`，而 Visitor 只有派生类的 visit 重载，重载决议找不到 `visit(const ShapeB&)`——报错实锤。`accept` 必须在每个派生类各自 override 的根本目的是**修正 `*this` 的静态类型**，不是多态本身。→ 知识点：[访问者模式](../vol4-generics-patterns/16-visitor.md)「为什么 accept 必须在每个派生类里各自 override」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra visitor.cpp -o visitor && ./visitor
total perimeter = 26.5664
$ g++ -std=c++20 visitor_bad.cpp -o vb2
visitor_bad.cpp:13:52: error: no matching function for call to
'Visitor::visit(const ShapeB&)'
  • no known conversion for argument 1 from 'const ShapeB' to 'const CircleB&'
```

## 4.38-A {#hw-4-38-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-38-a)

**思路**：`weak_ptr` 的语义恰是「不拥有、但知道」——不延长寿命、能随时探活。

1. 观察者在作用域内被通知一次；离开后 `lock()` 失败、跳过并顺手 `erase`——第二次通知无输出、无崩溃。→ 知识点：[观察者模式](../vol4-generics-patterns/17-observer.md)「第三步：weak_ptr 防悬挂」
2. 对比：裸指针是「随便死、死了不知道」（悬空）；`shared_ptr` 是「强行接管、想死死不掉」（僵尸）。`lock()` 原子地把 weak 升级成临时 shared，只要临时引用在手，对象在回调执行期间绝不会析构——这是硬保证。→ 知识点：[观察者模式](../vol4-generics-patterns/17-observer.md)「weak_ptr 防悬挂——观察不拥有，死了就知道」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra observer.cpp -o observer && ./observer
Loud 1 got 10
done (no crash)
```

## 4.38-B {#hw-4-38-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-38-b)

**思路**：①裸指针观察者的悬垂是 ASan 的经典猎物；②snapshot 通知让回调里的增删碰不到正在遍历的副本。

1. `Loud obs` 离开作用域后 `notify(20)` 解引用悬垂指针，ASan 报 `stack-use-after-scope` 精确到 `Subject::notify` 那行。普通构建下它可能读到垃圾值、可能段错误、也可能「看起来没事」——最难复现的一类 bug。→ 知识点：[观察者模式](../vol4-generics-patterns/17-observer.md)「这里先验证一下：悬空指针真的会炸吗」
2. snapshot：锁内拷一份回调、锁外遍历副本，回调里增删原表互不干扰，两个观察者各被精确调一次。原地遍历时回调里 `erase`/`push_back` 会触发迭代器失效（崩、漏调、重复调）。代价是每次通知拷贝一份回调列表。→ 知识点：[观察者模式](../vol4-generics-patterns/17-observer.md)「踩坑预警：在 notify 里改订阅列表」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -O0 -g -fsanitize=address observer_dangle.cpp -o od && ./od
==463==ERROR: AddressSanitizer: stack-use-after-scope on address ...
    #0 ... in Subject::notify(int) /tmp/cpp-v4-hw4a/observer_dangle.cpp:15
    #1 ... in main /tmp/cpp-v4-hw4a/observer_dangle.cpp:42
$ g++ -std=c++20 -Wall -Wextra snapshot.cpp -o snapshot && ./snapshot
A got 1
B got 1
total hits = 2 (expect 2)
```

## 4.39-A {#hw-4-39-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-39-a)

**思路**：中序的栈模拟 = 一路压左链当起点，弹栈访问、转右子树再压左链。

1. range-for 输出 1..7 的中序。→ 知识点：[迭代器模式](../vol4-generics-patterns/18-iterator.md)「第二步：外部迭代器」
2. 「pop 之后看右子树」正是递归中序「左、根、右」的栈版：当前节点访问完，下一个是右子树的最左后代（或弹回祖先）。相比「先压平再遍历」，迭代器把「随时取下一个」的主动权交给调用方——惰性、可提前终止、不用物化整棵树。标准算法只依赖迭代器接口，所以 `find_if`/`count_if` 免费可用。→ 知识点：[迭代器模式](../vol4-generics-patterns/18-iterator.md)「拿算法套上去：迭代器真正的价值」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra inorder.cpp -o inorder && ./inorder
1 2 3 4 5 6 7
```

## 4.39-B {#hw-4-39-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-39-b)

**思路**：range-for 是语法糖不是概念检查；`weakly_incrementable` 隐式要求后缀 `++`，只写前缀过不了 ranges 的门。

1. 只有前缀 `++` 的迭代器：`weakly_incrementable` 和 `input_iterator` 都是 `false`——range-for 能过，概念不过。→ 知识点：[迭代器模式](../vol4-generics-patterns/18-iterator.md)「这里先验证一下：它真能进 ranges 吗」
2. 补上后缀 `++`（`void operator++(int) { ++(*this); }`）、四个关联类型、默认构造后全绿，`views::filter` 管道直接可用（2 4 6）。`weakly_incrementable` 要后缀 ++ 是因为 ranges 内部实现按「`i++` 也得合法」写，不做语法回退。`iterator_concept` 是 C++20 新别名，`iterator_category` 是兼容老算法的旧别名。→ 知识点：[迭代器模式](../vol4-generics-patterns/18-iterator.md)「真正的坑：少了那个后缀 operator++」「补全 concept 配套」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra iter_bad.cpp -o iter_bad && ./iter_bad
weakly_incrementable: false
input_iterator:       false
$ g++ -std=c++20 -Wall -Wextra iter_ok.cpp -o iter_ok && ./iter_ok
input_iterator:              true
input_range<CountingRange>:  true
2 4 6
```

## 4.40-A {#hw-4-40-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-40-a)

**思路**：非虚 `handle` 焊死转发骨架、纯虚 `process` 只回答「我处理了吗」——转发逻辑只写一次。

1. `"cache"` 被 Cache 吃、`"auth"` 被 Auth 吃、`"x"` 走到链尾报 nobody handled。→ 知识点：[责任链模式](../vol4-generics-patterns/19-chain-of-responsibility.md)「第二步：经典指针链」
2. `handle`/`process` 拆分是模板方法 + 责任链的组合：每个节点只需填 `process`，转发骨架改不了也漏不掉。指针链的耦合搬到了节点之间：插中间节点要重连、链形状散落在 `next_` 里、`shared_ptr` 互持易成环。节点动态增减换 vector 调度器。→ 知识点：[责任链模式](../vol4-generics-patterns/19-chain-of-responsibility.md)「这套设计的精巧之处」「next_ 指针链没你想的那么解耦」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra chain.cpp -o chain && ./chain
Cache handled
Auth handled
[chain end] nobody handled: x
```

## 4.40-B {#hw-4-40-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-40-b)

**思路**：洋葱模型让节点拿到「怎么继续」的控制权：调 next 前后干前置/后置、不调 next 即短路。

1. 输出顺序：before auth → before log → final → after log → after auth——请求从外往里穿、响应从里往外穿。→ 知识点：[责任链模式](../vol4-generics-patterns/19-chain-of-responsibility.md)「第四步：中间件洋葱」
2. 认证拒绝的中间件不调 `c.next()`，链停在那里，后面的「不该出现」没出现——短路是「什么都不做」就实现的，没有 return false、没有 break。`index_` 游标的一次性暗坑：一条链跑完后游标顶到头，复用同一条链第二个请求什么都不发生，得重置游标。→ 知识点：[责任链模式](../vol4-generics-patterns/19-chain-of-responsibility.md)「想短路?不调 next() 就行」「别把 index_ 游标当万能的」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra onion.cpp -o onion && ./onion
before: auth
before: log
final
after: log
after: auth
--
A: denied
```

## 4.41-A {#hw-4-41-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-41-a)

**思路**：AST 求值 = 叶子给值、非叶子合并两个子结果，递归从根走到叶再层层回来。

1. $(1+2)\times 3-4$ 手搭成四层嵌套，求值得 5。→ 知识点：[解释器模式](../vol4-generics-patterns/20-interpreter.md)「第三步：从解释到求值——引入 AST」
2. `unique_ptr` 所有权对应 AST 的树形结构：父节点独占子节点，整树销毁时递归析构自动回收，一行 delete 不用写。「统一接口」指所有节点实现同一个 `evaluate()`，调用方只面向 `Node&`——组合模式思想的直接应用。`Bin::evaluate` 把「带优先级的求值」拆成「每层只负责合并两个子结果」的简单问题。→ 知识点：[解释器模式](../vol4-generics-patterns/20-interpreter.md)「解释器模式的命门是统一接口」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra ast.cpp -o ast && ./ast
5
```

## 4.41-B {#hw-4-41-b}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-41-b)

**思路**：在教材三层文法上把 `%` 加进 `term` 的循环条件（与 `*`、`/` 同级），优先级与左结合自动继承。

1. 六个表达式全对：优先级（$1+2\times 3 = 7$）、括号（$(1+2)\times 3 = 9$）、`%` 同级（`10 % 4 + 1 = 3`）、多层（$(2+3)\times (4-1) = 15$）。除零与缺括号走异常路径。→ 知识点：[解释器模式](../vol4-generics-patterns/20-interpreter.md)「第四步：把文本变成树——递归下降解析器」
2. 优先级靠「谁调用谁」：`expression` 调 `term`、`term` 调 `factor`——解析加号之前，乘除模早就被更里层的 `term` 抓走。左结合靠 while 循环：$1-2-3$ 朴素右递归会得 $1-(2-3)=2$，循环重组得 $(1-2)-3=-4$。`parse_number` 的一元负号严格说该单独立层（`factor := '-' factor | atom`），塞进 number 会容忍 `1--2` 这类输入。→ 知识点：[解释器模式](../vol4-generics-patterns/20-interpreter.md)「这里有两处设计上的取舍」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra parser.cpp -o parser && ./parser
"1+2*3" = 7
"(1+2)*3" = 9
"10 - 4 / 2" = 8
"7 % 3" = 1
"10 % 4 + 1" = 3
"(2+3)*(4-1)" = 15
"1/0" -> ERROR: division by zero
"(1+2" -> ERROR: missing )
```

## 4.42-A {#hw-4-42-a}

**难度 L2** · 题面见 [homework](./01-homework.md#hw-4-42-a)

**思路**：同事只认抽象中介者接口，谁也不认识谁——网状耦合收成星形。

1. 私聊命中 Bob、广播给 Alice/Carol（不回送自己）、私聊不存在的 Dave 由中介者兜底。→ 知识点：[中介者模式](../vol4-generics-patterns/21-mediator.md)「第二步：引入中介者接口——星形耦合」
2. 星形体现在：`User` 只持 `IMediator*`，没有 `send_to(const User&)` 这种签名。抽象接口绝不能反过来 include 所有同事类，否则网状耦合只是搬了家（中介者变上帝对象的前奏）。中介者最大的反噬是膨胀成上帝对象；三条缓解：按领域拆多个中介者、把规则抽成策略对象、优先事件总线而不是巨型 `notify`。→ 知识点：[中介者模式](../vol4-generics-patterns/21-mediator.md)「中介者什么时候会反过来咬你」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra chat.cpp -o chat && ./chat
[Bob] recv from Alice: hi Bob!
[Carol] recv from Bob: hello everyone
[Alice] recv from Bob: hello everyone
[room] Dave not online
```

## 4.42-B {#hw-4-42-b}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-42-b)

**思路**：事件总线把「协议」从中介者成员函数签名降级成事件值类型，`std::any` + `type_index` 做类型擦除。

1. `MsgSent` 两个订阅者各收到两次（logger 两行、计数 2）、`UserLogin` 一次；`bad_any_cast` 被 catch。→ 知识点：[中介者模式](../vol4-generics-patterns/21-mediator.md)「第四步：事件总线——类型擦除的中介者」
2. 类型擦除发生在 `subscribe` 内部：外面注册强类型 handler，包一层 lambda 转成吃 `any` 的统一签名；发布订阅两端看到的都是强类型。新增事件类型 = struct 一个新事件，总线一行不改（协议从成员函数签名降级成值类型）。代价：订阅端类型匹配的检查从编译期推迟到运行期（`bad_any_cast`）。无人订阅的事件被 `find` 落空安全忽略。→ 知识点：[中介者模式](../vol4-generics-patterns/21-mediator.md)「类型擦除不是免费的」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra eventbus.cpp -o eventbus && ./eventbus
[logger] Alice -> hi
[presence] Bob online
[logger] Alice -> again
count = 2 (expect 2)
caught: bad any_cast
```

## 4.C-1 {#hw-4-c-1}

**难度 L3** · 题面见 [homework](./01-homework.md#hw-4-c-1)

**思路**：concept 约束 `mean` 的输入契约，迭代器协议统一三种容器——concepts、ranges、迭代器三章在这里合流。

1. `IntegralRange = input_range && integral<range_value_t>`；`mean` 里 `double sum` 累计、`size_t n` 计数，最后 `sum / static_cast<double>(n)`——总分若是 int，`n` 整除时小数在除法那步就被截掉（整数除法坑）。→ 知识点：[使用 Concepts 约束模板](../vol3-metaprogramming-cpp20-23/02-constraining-templates.md)、[C++20 范围库基础与视图](../vol2-modern-cpp17/07-ranges-basics-and-views.md)
2. `Squares` 迭代器要过 `input_range`：前后缀 `++`、`==`、`value_type/difference_type/iterator_concept/iterator_category`、默认构造（`semiregular` 暗线）。三容器输出 4、25、7.5——同一个 `mean` 吃掉三种来源。→ 知识点：[迭代器模式](../vol4-generics-patterns/18-iterator.md)「补全 concept 配套」、[管道操作与 Ranges 实战](../vol2-modern-cpp17/08-ranges-pipeline-in-practice.md)

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra mean.cpp -o mean && ./mean
mean(vector)  = 4
mean(array)   = 25
mean(Squares) = 7.5
```

## 4.C-2 {#hw-4-c-2}

**难度 L4** · 题面见 [homework](./01-homework.md#hw-4-c-2)

**思路**：`sorted_copy` 只向类型系统要两样东西——`input_range` 和「元素可三路比较」；两个来源迥异的类型、一个自定义容器，全都满足这两条契约就能进来。

1. `SortableRange = input_range<R> && three_way_comparable<range_value_t<R>>`；`Score` 的比较能力来自 mixin（隐藏友元 `operator<=>` 转发 `cmp()`），`Version` 来自 default `<=>`——一个比较逻辑写在派生类、一个交给编译器按成员生成。→ 知识点：[使用 Concepts 约束模板](../vol3-metaprogramming-cpp20-23/02-constraining-templates.md)、[三路比较运算符](../05-spaceship-operator.md)、[模板友元与 Barton-Nackman](../vol1-basics-cpp11-14/07-friends-and-barton-nackman.md)
2. `FixedVector` 一行不改就能被 `sorted_copy` 用：它的 `begin()/end()` 返回裸指针，而裸指针天然满足 `input_range`（乃至 contiguous_range）的迭代器要求。→ 知识点：[综合项目:fixed_vector](../vol1-basics-cpp11-14/10-fixed-vector.md)、[迭代器模式](../vol4-generics-patterns/18-iterator.md)
3. `three_way_comparable` 替你检查了 `<=>` 与 `==` 的一致性（同一比较语义），`ranges::sort` 再用 `ranges::less`（基于 `<=>` 重写）排序。→ 知识点：[三路比较运算符](../05-spaceship-operator.md)「常用比较运算符的重写」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra sorted.cpp -o sorted && ./sorted
scores: 1 2 3
versions: 1.2 1.5 2.0
```

## 4.C-3 {#hw-4-c-3}

**难度 L5** · 题面见 [homework](./01-homework.md#hw-4-c-3)

**思路**：typelist 是模板元编程的积木（源自 Alexandrescu《Modern C++ Design》第 3 章 / Boost.MPL 思路）；选择排序的编译期版 = 每轮挑 `sizeof` 最大的类型放最前，递归处理剩余。

1. 五个元函数全用「主模板 + 特化」递归：`Length` 数节点、`MaxSize` 求最大值、`MaxType` 挑出最大元素、`RemoveFirstOfSize` 删掉第一个匹配、`SortBySize` 拼成降序列表；`Reverse` 用尾递归 + 累加器。→ 知识点：[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)「编译期图灵完备」、[模板特化与偏特化](../vol1-basics-cpp11-14/04-specialization-partial.md)、[非类型模板参数](../vol1-basics-cpp11-14/05-non-type-parameters.md)（`sizeof` 即编译期常量）
2. 全部断言过：长度 5、最大元素 `double`、排序后头三个 `double/int/short`、反转后头是 `short`；运行期打印 `1 4 1 8 2 → 8 4 2 1 1`。→ 知识点：[模板特化与偏特化](../vol1-basics-cpp11-14/04-specialization-partial.md)「优先级：全特化 > 偏特化 > 主模板」
3. 附加思考：选择排序的编译期复杂度是 O(n²)（每轮全表扫最大 + 删除）；两个同 `sizeof` 的元素（两个 `char`）时 `RemoveFirstOfSize` 每轮只删第一个，两个 char 的相对顺序保持原样——选择排序天然稳定，你的输出里两个 1 的顺序与输入一致，正是验证。→ 知识点：[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)「代价：实例化、代码膨胀」

**验证输出**：

```text
$ g++ -std=c++20 -Wall -Wextra typelist.cpp -o typelist && ./typelist
original sizes: 1 4 1 8 2
sorted sizes:   8 4 2 1 1
reversed head size = 2
```

（来源标注：typelist 与编译期算法技术源自 Andrei Alexandrescu《Modern C++ Design》（Loki 库）与 Boost.MPL 的思路，本题按「模板元编程实现编译期排序」的竞赛挑战规格重出；L5 档位口径见[练习总览](../exercises/index.md)。）
