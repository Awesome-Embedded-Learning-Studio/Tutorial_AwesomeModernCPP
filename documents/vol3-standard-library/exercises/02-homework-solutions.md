---
title: "卷 3 课后练习参考答案（Homework）"
description: "标准库深入卷课后练习的逐题详细解答：15 道题每题给出解题思路、逐步解答（每步标注知识点链接）与真实验证输出（g++ 16 / clang++ 22 / WSL Arch 实跑，UB 题用 ASan 与 -D_GLIBCXX_DEBUG 实测）。"
chapter: 3
order: 2
tags:
  - host
  - intermediate
  - cpp-modern
  - 容器
  - 类型安全
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20, 23]
reading_time_minutes: 28
prerequisites:
  - "卷 3 课后练习（Homework）"
related:
  - "卷 3 Lab：标准库解剖台"
  - "卷 3 Project：CSV 报表流水线"
---

# 卷 3 课后练习参考答案（Homework）

> 所有命令与输出在 WSL Arch（g++ 16.1.1，libstdc++ 16）下真实运行得到。UB 类题目的输出「只是这台机器这一次的选择」，换编译器、优化级别可能不同——这正是每道题要你体会的东西。此类题目一律在普通构建之外补了 ASan 或 `-D_GLIBCXX_DEBUG` 的实测，报告都是真跑出来的。

## 3.1-A {#hw-3-1-a}

**难度 L1** · 题面见 [homework](01-homework.md#hw-3-1-a)

**思路**：`array` 是聚合类型零开销包住 C 数组；`vector` 本体约等于三个指针；`span` 的动态/静态 extent 差一个存长度用的字。

1. 打印六种类型的 `sizeof`。`array<int,4>` 是 `4 × sizeof(int) = 16`，没有任何额外成员；`vector<int>` 是三个指针 24 字节；`list<int>` 是 24（哨兵节点的前后两个指针 16 字节 + 维护 size 的计数 8 字节）；`string` 是 32（SSO 缓冲）。→ 知识点：[array：编译期固定大小的聚合容器](../containers/02-array.md)「array 到底是什么」一节、[vector 深入：三指针、扩容与迭代器失效](../containers/03-vector-deep-dive.md)「三个指针，撑起整个 vector」一节、[string 深入：SSO、COW 与 resize_and_overwrite](../containers/04-string-memory-deep-dive.md)「SSO 的阈值」一节
2. capacity 跳变 `1 → 2 → 4 → 8 → 16`：每次翻倍，本机 libstdc++ 是约 2× 扩容。这个倍率标准**没规定**（unspecified），只是主流实现的工程选择。→ 知识点：[vector 深入](../containers/03-vector-deep-dive.md)「扩容这件事」一节
3. `span<int>` 是 16 字节（指针 + size 两个字），`span<int,4>` 是 8 字节（长度编译期已知，对象里只存指针）。→ 知识点：[span：非拥有的连续视图](../containers/08-span.md)「动态 extent 与静态 extent」一节

**验证输出**：

```text
$ g++ -std=c++20 -O2 hw31a.cpp -o hw31a && ./hw31a
sizeof(array<int,4>)   = 16
sizeof(vector<int>)    = 24
sizeof(list<int>)      = 24
sizeof(string)         = 32
sizeof(span<int>)      = 16
sizeof(span<int,4>)    = 8
push #1: size=1 capacity=1
push #2: size=2 capacity=2
push #3: size=3 capacity=4
push #5: size=5 capacity=8
push #9: size=9 capacity=16
```

## 3.1-B {#hw-3-1-b}

**难度 L2** · 题面见 [homework](01-homework.md#hw-3-1-b)

**思路**：map 的元素挂在独立树节点上、地址稳定；vector 扩容整块搬家、旧指针全废；splice 是链表特有的 O(1) 节点搬家。

1. 注册表用 map：插 1000 个新元素后 `ref` 和 `it` 都还是 `alpha`——插入不失效任何已有迭代器/引用。对照 vector：`reserve(2)` 后 push 两个、拿 `&v[1]`，再 push 触发扩容，旧指针 `== &v[1]` 打印 **0**，失效实锤。→ 知识点：[map 与 set 深入](../containers/06-map-set-deep-dive.md)「复杂度和迭代器失效」一节、[vector 深入](../containers/03-vector-deep-dive.md)「迭代器失效：一张表讲完所有规矩」一节
2. `a.splice(a.end(), b, first, last)` 把 b 的第 2、3 个节点直接挂到 a 尾部：`a` 变 `1 2 3 4 5 20 30`、`b` 变 `10 40`，全程零拷贝。→ 知识点：[deque、list 与 forward_list](../containers/05-deque-list-forward-list.md)「list：双向链表」一节
3. deque 的分段结构在 erase 时要搬动块指针，标准规定它任何 erase 都让全部迭代器失效；vector 不扩容时 erase 只失效被删点之后的。→ 知识点：[容器选择指南](../containers/01-container-selection-guide.md)「迭代器失效速查」一节

**验证输出**：

```text
$ g++ -std=c++20 -O2 hw31b.cpp -o hw31b && ./hw31b
map 插 1000 个后: ref=alpha it=alpha
vector 扩容后旧指针仍等于 &v[1]? 0
splice 后 a: 1 2 3 4 5 20 30  b: 10 40
```

## 3.2-A {#hw-3-2-a}

**难度 L2** · 题面见 [homework](01-homework.md#hw-3-2-a)

**思路**：插入迭代器把「赋值」翻译成「插入」；`front_inserter` 要 `push_front`；`reverse_iterator` 解引用访问的是 `base()` 的前一位。

1. `back_inserter` 两次追加得 `7 2 5 7 2 5`；`front_inserter` 每个新元素插最前，顺序反转成 `5 2 7`；`inserter` 插在 20 **之前**得 `10 7 2 5 20 30`。→ 知识点：[迭代器适配器](../iterators-algorithms/41-iterator-adapters.md)「插入迭代器」一节
2. `front_inserter` 套 vector 编译失败：`vector` 没有 `push_front`，报错指名 `error: 'class std::vector<int>' has no member named 'push_front'`。→ 知识点：同上「三兄弟各自的容器要求」一节
3. `*rit=40`、`*rit.base()=50`、`*(rit.base()-1)=40`：反向解引用访问的是 `base()` 的前一位。`sort(rbegin, rend)` 把升序反过来写，拿到降序。→ 知识点：同上「反向迭代器」一节与「base() 差一位」提示块

**验证输出**：

```text
$ g++ -std=c++20 -O2 hw32a.cpp -o hw32a && ./hw32a
back_inserter x2: 7 2 5 7 2 5
front_inserter: 5 2 7
inserter(在20前): 10 7 2 5 20 30
*rit=40 *rit.base()=50 *(rit.base()-1)=40
sort(rbegin,rend) 降序: 9 6 5 4 3 2 1 1
```

front_inserter 套 vector 的报错（关键行）：

```text
$ g++ -std=c++20 hw32a_fail.cpp -o hw32a_fail
/usr/include/c++/16/bits/stl_iterator.h:819:20: error: 'class std::vector<int>' has no member named 'push_front'
  819 |         container->push_front(__value);
      |         ~~~~~~~~~~~^~~~~~~~~~
```

## 3.2-B {#hw-3-2-b}

**难度 L3** · 题面见 [homework](01-homework.md#hw-3-2-b)

**思路**：需求越弱、能用的排序算法越快；投影让「按成员排序」不用写 lambda；`accumulate` 的返回类型 = 初始值类型。

1. `nth_element` 把第 4 位钉成 90.5（两边内部无序）——只要中位数就别全排。→ 知识点：[算法总览（下）](../iterators-algorithms/43-algorithm-overview-part2.md)「nth_element」一节
2. `partial_sort` 前 3 名得 `95 91 90.5`。→ 知识点：同上「partial_sort」一节
3. `ranges::sort(asc, {}, &Student::score)` 升序、换 `std::greater{}` 降序；`ranges::stable_sort` 保输入顺序（并列的 bob/frank 与 alice/carol 都保持原序），而普通 `sort` 的并列顺序不可依赖。→ 知识点：同上「C++20 投影」一节、「sort 不保证相等元素的顺序」提示块
4. 数学和 703.5：`accumulate(frac, 0)` 把每个 double 先截断成 int，得 **702**；`accumulate(frac, 0.0)` 全程 double，得 **703.5**。→ 知识点：[numeric](../iterators-algorithms/44-numeric-algorithms.md)「accumulate：累加，以及它最坑的返回类型」一节

**验证输出**：

```text
$ g++ -std=c++20 -O2 hw32b.cpp -o hw32b && ./hw32b
中位数(第4位) = 90.5
前3名: 95 91 90.5
ranges::sort 升序: {erin, 76.5} {heidi, 84} {bob, 88} {frank, 88} {alice, 90.5} {carol, 90.5} {grace, 91} {dave, 95}
ranges::sort 降序: {dave, 95} {grace, 91} {alice, 90.5} {carol, 90.5} {bob, 88} {frank, 88} {heidi, 84} {erin, 76.5}
ranges::stable_sort 升序(并列保输入顺序): {erin, 76.5} {heidi, 84} {bob, 88} {frank, 88} {alice, 90.5} {carol, 90.5} {grace, 91} {dave, 95}
accumulate(frac, 0)   = 702
accumulate(frac, 0.0) = 703.5
```

## 3.3-A {#hw-3-3-a}

**难度 L1** · 题面见 [homework](01-homework.md#hw-3-3-a)

**思路**：`string_view` 是「指针 + 长度」的只读视图，视口操作全 O(1)、全不拷贝。

1. `sizeof(string)` 是 32（SSO 缓冲），`string_view` 是它一半：16 字节。→ 知识点：[string_view](../strings/50-string-view.md)「内部表示」一节
2. 用 `find("://")` + `remove_suffix` 切 scheme；对剩余部分 `find(':')`/`find('/')` 定位后 `substr` 切出 host、port、path。四段 view 都指向原 `url` 那块内存，一个字节没拷。→ 知识点：同上「remove_prefix / remove_suffix / substr」一节
3. `starts_with`/`ends_with`（C++20）与 `contains`（C++23）都是一行谓词。→ 知识点：同上「C++20 / C++23：补上的几个小接口」一节

**验证输出**：

```text
$ g++ -std=c++23 -O2 hw33a.cpp -o hw33a && ./hw33a
sizeof(std::string)      = 32
sizeof(std::string_view) = 16
scheme = ftp
host   = example.com
port   = 21
path   = /pub/file.txt
starts_with("ftp")  = true
ends_with(".txt")   = true
contains("ample")   = true
```

## 3.3-B {#hw-3-3-b}

**难度 L3** · 题面见 [homework](01-homework.md#hw-3-3-b)

**思路**：`from_chars` 是前缀解析——不光要看 `ec`，还要看 `ptr` 是否走到字段末尾；它不跳空白；`to_chars` 不写 `\0`。

1. 三个字段用 `find(',')` 切，`from_chars` 各解析一段。**关键**：`r.ptr != sv.data() + sv.size()` 也要判失败，否则 `"19x9"` 会「成功」解析出 19——前缀解析的陷阱，题面第三个输入 `"carol,19x9,1.5"` 就是来抓这个漏的。→ 知识点：[charconv](../strings/51-charconv.md)「from_chars」一节与「忽略返回值里的 ec」提示块
2. `"bob, 2001,2.718"` 的年份前有空格：`from_chars` 不跳前导空白，先 `remove_prefix` 把空格 trim 掉再解析。→ 知识点：同上「from_chars 不跳前导空白」提示块
3. `to_chars` 写进 `char buf[32]`，结果用 `std::string(buf, r.ptr)` 界定——它不写 `\0`，直接当 C 字符串必踩雷。→ 知识点：同上「to_chars 不写 null 结尾」提示块
4. `std::format` 的 `{:<8}`/`{:>6}`/`{:>8.2f}` 出对齐表格，类型检查全在编译期。→ 知识点：[format](../strings/52-format.md)「格式说明」一节

**验证输出**：

```text
$ g++ -std=c++23 -O2 hw33b.cpp -o hw33b && ./hw33b
|alice   |  1998|    3.14|
  to_chars 结果 = "3.14" (长度 4)
|bob     |  2001|    2.72|
  to_chars 结果 = "2.718" (长度 5)
"carol,19x9,1.5" -> 解析失败
```

## 3.4-A {#hw-3-4-a}

**难度 L2** · 题面见 [homework](01-homework.md#hw-3-4-a)

**思路**：`ofstream` 默认隐含 `trunc`；打开失败不检查就是「默默继续」；`eof()` 只在读越过末尾后才置位。

1. 第二次默认打开写 `"new"`：文件被清空，读回只有 `new`（`OLD` 没了）；`app` 追加后读回 `newPLUS`。→ 知识点：[fstream](../io/56-fstream.md)「三类流与 open 模式」一节
2. 打开不存在的文件不检查：`x` 保持初值 42、`fail()==1`——看着像「读到了 42」，其实压根没读。正确姿势是构造后立刻 `is_open()` 判、失败早退。→ 知识点：同上「错误检查」一节
3. `while(getline)` 数出 3 行；`while(!eof())` 数出 **4** 行——最后一次 `getline` 越过末尾失败，但循环体还是执行了，计数多加 1。读循环永远把读操作放条件里。→ 知识点：同上「错误检查」一节的 `eof()` 坑

**验证输出**：

```text
$ g++ -std=c++20 -O2 hw34a.cpp -o hw34a && ./hw34a
第二次打开后内容 = "new" (OLD 被清空)
app 追加后内容   = "newPLUS"
未检查打开失败: x=42 fail=1
while(getline) 行数 = 3
while(!eof) 计数 = 4 (多 1)
```

## 3.4-B {#hw-3-4-b}

**难度 L3** · 题面见 [homework](01-homework.md#hw-3-4-b)

**思路**：遍历时用 `directory_entry` 成员命中缓存，省掉每条目一次 stat；错误处理走「异常 / error_code」双路径；`operator/` 右边带根会吃掉左边。

1. 造树 + 递归遍历：`e.is_regular_file()` / `e.file_size()` 用 entry 缓存的 stat 信息，统计出 3 个文件 24 字节。若写 `fs::file_size(e.path())` 会每条目多发一次 stat。→ 知识点：[filesystem](../io/57-filesystem.md)「directory_entry 的缓存」一节
2. 不存在的路径：异常版抛 `filesystem_error`（`what()` 带操作名与路径），error_code 版返回 `value=2`（ENOENT）、`message="No such file or directory"`、size 是 `uintmax_t` 的最大值。→ 知识点：同上「错误处理：异常与 error_code 双路径」一节、[error_code](../error-utils/66-error-code.md)「error_code 的构成」一节
3. `base / "/etc/x"` 得 `"/etc/x"`（绝对路径自解释，左边全丢）；`base / "etc/x"` 得 `"/opt/app/etc/x"`。→ 知识点：[filesystem](../io/57-filesystem.md)「operator/ 的反直觉坑」一节

**验证输出**：

```text
$ g++ -std=c++20 -O2 hw34b.cpp -o hw34b && ./hw34b
文件数 = 3 总字节 = 24
异常版 what: filesystem error: cannot get file size: No such file or directory [/tmp/cpp-v3-hw/definitely_missing_xyz]
error_code 版: size=18446744073709551615 value=2 message=No such file or directory
base / "/etc/x" = "/etc/x"
base / "etc/x"  = "/opt/app/etc/x"
```

## 3.5-A {#hw-3-5-a}

**难度 L2** · 题面见 [homework](01-homework.md#hw-3-5-a)

**思路**：测耗时只能用 `steady_clock`（单调、不被 NTP 拨动）；duration 的隐式转换只走无损方向；四种取整语义各管一段。

1. `system_clock` 与 `high_resolution_clock` 的 `is_steady` 都是 false（后者在 GCC 16.1.1 上就是 `system_clock` 的别名），只有 `steady_clock` 是 true。→ 知识点：[chrono](../time-numeric/58-chrono.md)「三种 clock 的真实属性」一节与「high_resolution_clock 是别名」提示块
2. `seconds s = milliseconds{500};` **编译失败**——500ms 变秒要丢精度，有损方向禁隐式转换，报错 `conversion from 'duration<...ratio<...,1000>>' to non-scalar type 'duration<...ratio<...,1>>' requested`。→ 知识点：同上「隐式转换」一节
3. 1999ms：`duration_cast` 截断得 1s、`floor` 得 1s、`ceil` 得 2s、`round` 得 2s（1999 过半秒，舍入进位；`round` 是银行家舍入，半数取偶）。→ 知识点：同上「duration_cast 与 ceil / floor / round」一节
4. 忙等 100ms 用 `steady_clock` 量出正好 100 ms。→ 知识点：同上「测耗时为什么只能用 steady_clock」一节

**验证输出**：

```text
$ g++ -std=c++20 -O2 hw35a.cpp -o hw35a && ./hw35a
system_clock::is_steady           = false
steady_clock::is_steady           = true
high_resolution_clock::is_steady  = false
1999ms:
  duration_cast<seconds> = 1 s (截断)
  floor<seconds>         = 1 s
  ceil<seconds>          = 2 s
  round<seconds>         = 2 s
steady_clock 测忙等 100ms = 100 ms
```

编译失败的报错：

```text
$ g++ -std=c++20 hw35a_fail.cpp -o hw35a_fail
hw35a_fail.cpp:7:17: error: conversion from 'duration<[...],ratio<[...],1000>>' to non-scalar type 'duration<[...],ratio<[...],1>>' requested
    7 |     seconds s = milliseconds{500};   // 隐式有损转换, 编译失败
      |                 ^~~~~~~~~~~~~~~~~
```

## 3.5-B {#hw-3-5-b}

**难度 L3** · 题面见 [homework](01-homework.md#hw-3-5-b)

**思路**：`uniform_int_distribution` 是**闭区间**且用拒绝采样消除取模偏差；`hypot` 防中间溢出；NaN 不等于自己；0.1 累加有舍入误差。

1. `uniform_int_distribution<int>(1,6)` 的区间是 `[1,6]` 闭区间，两个端点都抽得到；`eng() % N` 的取模偏差（$2^{32}$ 不被 N 整除时余数桶多一个候选）被内部拒绝采样丢弃尾巴重抽，保证严格均匀。60000 次下来六面都在 1/6 附近（0.164~0.168），`rand()%6+1` 对照也在同量级。→ 知识点：[random](../time-numeric/60-random.md)「分布」一节与「uniform_int_distribution 是闭区间」提示块
2. `sqrt(1e200*1e200+...)` 中间平方溢出成 `inf`；`hypot(1e200,1e200)` 用防溢出算法给出 1.41421e+200。→ 知识点：[cmath](../time-numeric/59-cmath.md)「hypot」一节
3. `NaN == NaN` 是 **false**（IEEE 754 规定 NaN 与任何值比较都为 false），判 NaN 用 `std::isnan`。→ 知识点：同上「NaN 不等于自己」一节
4. 10 次 0.1 累加得 0.99999999999999989，`== 1.0` 为 false——浮点没有精确的 0.1。→ 知识点：同上「== 为什么失灵」一节

**验证输出**：

```text
$ g++ -std=c++20 -O2 hw35b.cpp -o hw35b && ./hw35b
mt19937(42) + uniform(1,6) 掷 60000 次:
  面 1:  9890 次 (0.1648)
  面 2: 10037 次 (0.1673)
  面 3:  9991 次 (0.1665)
  面 4: 10075 次 (0.1679)
  面 5: 10096 次 (0.1683)
  面 6:  9911 次 (0.1652)
srand(42) + rand()%6+1 掷 60000 次:
  面 1: 10018 次 (0.1670)
  面 2:  9870 次 (0.1645)
  面 3: 10010 次 (0.1668)
  面 4: 10091 次 (0.1682)
  面 5: 10161 次 (0.1694)
  面 6:  9850 次 (0.1642)
sqrt(x*x+y*y)      = inf
hypot(1e200,1e200) = 1.41421e+200
NaN == NaN? false  isnan? true
10*0.1 累加 = 0.99999999999999989  == 1.0? false
```

## 3.6-A {#hw-3-6-a}

**难度 L2** · 题面见 [homework](01-homework.md#hw-3-6-a)

**思路**：optional 是值类型、自管 `T` 的生命周期；`value()` 空时抛异常、`*` 空时是 UB 且 sanitizer 抓不住、`_GLIBCXX_ASSERTIONS` 能抓。

1. `emplace` 就地构造；再 `emplace` 先析构旧的再构造新的；`reset()` 析构当前值——日志顺序：alice 构造 → alice 析构 → bob 构造 → bob 析构。→ 知识点：[optional](../error-utils/61-optional.md)「emplace、reset 与值语义的生命周期」一节
2. 空 optional：`has_value()` 为 false、`value()` 抛 `std::bad_optional_access`（`what()` 就是 `bad optional access`）、`value_or(-1)` 给 -1。`*empty` 普通构建下打印 **0**「看起来没事」；用 `-D_GLIBCXX_ASSERTIONS` 编译后运行，libstdc++ 的 `operator*` 断言 `this->_M_is_engaged()` 失败，abort（退出码 134）。→ 知识点：同上「真正的坑：解引用空 optional 是未定义行为」一节
3. 变式查找返回下标：全奇数组返回 `none`，含偶数组返回第一个偶数下标 3。→ 知识点：同上「构造与访问」一节

**验证输出**：

```text
$ g++ -std=c++23 -O2 hw36a.cpp -o hw36a && ./hw36a
empty.has_value()  = false
a.has_value()      = true
a.value()          = 42
*a                 = 42
empty.value_or(-1) = -1
caught: bad optional access
  User(alice, 30) 构造
  hi, 我是 alice, 30 岁
  User(alice) 析构
  User(bob, 25) 构造
  hi, 我是 bob, 25 岁
  User(bob) 析构
find odd: none
find mix: 3
```

`*empty` 的 UB 实测：

```text
$ g++ -std=c++23 -O2 hw36a_ub.cpp -o hw36a_ub && ./hw36a_ub
0                                ← 普通构建: 打印 0, 像没事
$ g++ -std=c++23 -O2 -D_GLIBCXX_ASSERTIONS hw36a_ub.cpp -o hw36a_ub_as && ./hw36a_ub_as
/usr/include/c++/16/optional:1251: constexpr _Tp& std::optional<_Tp>::operator*() & [with _Tp = int]: Assertion 'this->_M_is_engaged()' failed.
Aborted                          ← 退出码 134
```

## 3.6-B {#hw-3-6-b}

**难度 L3** · 题面见 [homework](01-homework.md#hw-3-6-b)

**思路**：`visit` + `overloaded` 是 C++ 的模式匹配，漏分支编译失败；`expected` 的 `and_then`/`transform` 把「可能失败的步骤」串成自动短路的链。

1. `overloaded` 三个 lambda 逐一匹配 `int/double/string`，四个值各走各的分支。→ 知识点：[variant](../error-utils/62-variant.md)「std::visit：把 if-else 链变成模式匹配」一节
2. 温度链：`parse_celsius` 用 `from_chars` 解析并检查 `ptr` 是否吃完整串，低于 -273.15 返回 `kBelowAbsoluteZero`；`and_then(to_fahrenheit)` 做换算（可能失败的下一步），`transform(fmt_fahrenheit)` 只拼字符串（不会失败）。`"36.5"` 一路走通得 `97.7 F`；`"abc"` 在解析处短路（code=0）；`"-300"` 解析成功但低于绝对零度被拒（code=1）——链上失败后整条链短路，错误一路传到底。→ 知识点：[expected](../error-utils/64-expected.md)「C++23 的 monadic」一节、[charconv](../strings/51-charconv.md)「from_chars」一节

**验证输出**：

```text
$ g++ -std=c++23 -O2 hw36b.cpp -o hw36b && ./hw36b
int:42
double:3.14
string:hello
int:7
"36.5" -> 97.7 F
"abc" -> ERR code=0
"-300" -> ERR code=1
```

## 3.C-1 {#hw-3-c-1}

**难度 L3** · 题面见 [homework](01-homework.md#hw-3-c-1)

**思路**：整条流水线串起五篇的知识：`string_view` 切字段、`from_chars` 解析（ec + ptr 双检查）、`nth_element` 中位数、`partial_sort` 前三、`accumulate` 防截断、`format` 出表格。

1. `parse_line` 用 `find(' ')` 切两段，`from_chars` 解析分数并检查 `ec`。8 行全部解析成功。→ 知识点：[string_view](../strings/50-string-view.md)「视口操作」一节、[charconv](../strings/51-charconv.md)「from_chars」一节
2. 平均分：`accumulate(..., 0.0, ...)` 初始值带小数点，得 87.94；传 0 会截成整数。→ 知识点：[numeric](../iterators-algorithms/44-numeric-algorithms.md)「accumulate」一节
3. 中位数：8 个元素取 `begin() + size()/2`（第 4 位），`nth_element` 后该位是 90.5。→ 知识点：[算法总览（下）](../iterators-algorithms/43-algorithm-overview-part2.md)「nth_element」一节
4. 前三名用 `partial_sort` 降序；全表 `ranges::sort` 按名字投影排序；表格用 `format` 对齐。→ 知识点：同上「partial_sort」「C++20 投影」两节、[format](../strings/52-format.md)「格式说明」一节

**验证输出**：

```text
$ g++ -std=c++23 -O2 -Wall -Wextra hw3c1.cpp -o hw3c1 && ./hw3c1
解析成功 8 行
平均分 = 87.94
中位数 = 90.5
前 3 名:
  1. dave      95.0
  2. grace     91.0
  3. alice     90.5
按名字排序:
  alice     90.5
  bob       88.0
  carol     90.5
  dave      95.0
  erin      76.5
  frank     88.0
  grace     91.0
  heidi     84.0
```

## 3.C-2 {#hw-3-c-2}

**难度 L4** · 题面见 [homework](01-homework.md#hw-3-c-2)

**思路**：四个 bug 对应四张失效规则表——view 悬垂、vector 扩容全失效、被删节点迭代器、遍历中 erase。每个都「先预测 → 工具实测 → 修复」走一遍。

**① string_view 绑临时串**。预测：临时 `string` 在语句末析构，`sv` 悬垂。普通构建下 `sv` 打印出垃圾（本机这次是乱码字节，`size` 仍是 4——长度是构造时抄进去的，析构不影响它）；ASan 一把抓住：

```text
$ g++ -std=c++20 -O1 -g -fsanitize=address hw3c2a_dangle.cpp -o hw3c2a_asan && ./hw3c2a_asan
==389==ERROR: AddressSanitizer: stack-use-after-scope on address 0x7a9117ff0130 ...
READ of size 4 at 0x7a9117ff0130 thread T0
    #3 0x5a0ce875b465 in main /tmp/cpp-v3-hw/hw3c2a_dangle.cpp:13
Address 0x7a9117ff0130 is located in stack of thread T0 at offset 304 in frame
    #0 0x5a0ce875a288 in main /tmp/cpp-v3-hw/hw3c2a_dangle.cpp:6
SUMMARY: AddressSanitizer: stack-use-after-scope ...
```

修复：先物化 `std::string owned = s + "x";` 再取 view，ASan 零报告、输出 `sv: [abcx] size=4`。→ 知识点：[string_view](../strings/50-string-view.md)「最大的坑：它不持有，会悬垂」一节

**② vector 扩容后旧迭代器**。预测：扩容换缓冲，旧迭代器失效。普通构建下 `*it` 打出 **6**（读到被释放内存里的垃圾值，不是原来的 2）；ASan 报 `heap-use-after-free`，并点出释放点是 `push_back` 内部的 `_M_realloc_append`：

```text
$ g++ -std=c++20 -O1 -g -fsanitize=address hw3c2b_realloc.cpp -o hw3c2b_asan && ./hw3c2b_asan
==419==ERROR: AddressSanitizer: heap-use-after-free on address 0x79dcdf7e0014 ...
READ of size 4 at 0x79dcdf7e0014 thread T0
    #0 0x58a9076a2a14 in main /tmp/cpp-v3-hw/hw3c2b_realloc.cpp:11
freed by thread T0 here:
    #6 ... in void std::vector<int, ...>::_M_realloc_append<int const&>(int const&) ...
    #8 0x58a9076a28f2 in main /tmp/cpp-v3-hw/hw3c2b_realloc.cpp:9
SUMMARY: AddressSanitizer: heap-use-after-free ...
```

修复：`v.reserve(200)` 预撑容量后同样操作 `*it = 2` 正常。→ 知识点：[vector 深入](../containers/03-vector-deep-dive.md)「迭代器失效：一张表讲完所有规矩」一节

**③ unordered_map 删除后使用被删元素的迭代器**。预测：`erase` 只失效被删元素本身的迭代器，节点被释放，用即 UB。普通构建下恰好还打印出 `beta`（节点内存没被覆盖，最阴险的「看着没事」）；ASan 报 `heap-use-after-free`；`-D_GLIBCXX_DEBUG` 构建当场断言：

```text
$ g++ -std=c++20 -O1 -D_GLIBCXX_DEBUG hw3c2c_erase.cpp -o hw3c2c_dbg && ./hw3c2c_dbg
/usr/include/c++/16/debug/safe_iterator.h:370:
    constexpr gnu_debug::_Safe_iterator<...>::operator->() const ...
Error: attempt to dereference a singular iterator.
...
Aborted                          ← 退出码 134
```

教材外补充：`-D_GLIBCXX_DEBUG` 是 libstdc++ 的 debug 模式断言，卷 3 各章未讲授。它与 ASan 的分工不同：ASan 靠运行期插桩抓**内存层**错误（越界读写、heap/stack-use-after-free、use-after-return），`_GLIBCXX_DEBUG` 抓**容器语义层**错误（解引用/自增奇异迭代器、越界 `operator[]`、跨容器比较）。本小题 ASan 在「节点内存被回收后再读」时抓到 heap-use-after-free，debug 模式则在解引用奇异迭代器时当场断言——两个工具从不同层抓住同一个 bug，互补而非互相替代。

修复：删除前先把内容取出来（`std::string saved = m.at(2);` 再 `erase`）；或者持有引用——C++14 起 rehash 不失效引用，插 200 个元素后 `ref` 仍是 `alpha`。debug 构建零报告。→ 知识点：[unordered_map 与 unordered_set 深入](../containers/07-unordered-map-set-deep-dive.md)「复杂度与迭代器失效」一节

**④ map 边遍历边 erase**。预测：`erase(it)` 后 `++it` 是对已失效迭代器自增，UB。普通构建下本机直接段错误（退出码 139）；debug 构建给出精确诊断：

```text
$ g++ -std=c++20 -O1 -D_GLIBCXX_DEBUG hw3c2d_erase.cpp -o hw3c2d_dbg && ./hw3c2d_dbg
/usr/include/c++/16/debug/safe_iterator.h:392:
In function:
    constexpr gnu_debug::_Safe_iterator<...>::operator++() ...
Error: attempt to increment a singular iterator.
...
Aborted                          ← 退出码 134
```

修复：`it = m.erase(it);`（erase 返回下一个有效迭代器），不删才 `++it`。修复后输出 `删偶数后: 1 3 5 7  size=4`，debug 构建零报告。→ 知识点：[map 与 set 深入](../containers/06-map-set-deep-dive.md)「复杂度和迭代器失效」一节、[容器选择指南](../containers/01-container-selection-guide.md)「迭代器失效速查」一节

## 3.C-3 {#hw-3-c-3}

**难度 L5** · 题面见 [homework](01-homework.md#hw-3-c-3)

**思路**：LRU 的两个 O(1) 来自「list 管顺序、unordered_map 管索引、splice 管搬家」的组合拳。vector 做不了：它没有 O(1) 的「把任意元素挪到头部」——头部插入要搬动整块元素；`list` 的 `splice` 只改几个指针，把节点从原位置摘下来挂到队首，零拷贝零分配。`unordered_map` 存 `list::iterator` 是安全的：list 插入不失效迭代器，删除只失效被删节点，所以索引永远有效。

1. 数据结构：`order_`（list，头=最新、尾=最久未用）+ `index_`（map，key → list 迭代器）。→ 知识点：[deque、list 与 forward_list](../containers/05-deque-list-forward-list.md)「list：双向链表」一节
2. `get`：查索引，命中就 `splice` 到队首（O(1)）并返回值；不中返回 `nullopt`。→ 知识点：同上「splice」、[optional](../error-utils/61-optional.md)「四种拿值的方式」一节
3. `put`：已存在就改值 + splice 到队首；满员时淘汰 `order_.back()`（最久未用），同时从索引里删掉；再 `emplace_front` 并登记迭代器。→ 知识点：[unordered_map 与 unordered_set 深入](../containers/07-unordered-map-set-deep-dive.md)「复杂度与迭代器失效」一节
4. 输出 `1 -1 3 -1 3 4` 与 LeetCode 146 用例同构（多一步 `get(3)` 验证命中不淘汰——LC 官方 Example 1 的操作序列输出是 `1 -1 -1 3 4`，本序列在 `put(4,4)` 之前多插了一次 `get(3)`）。→ 知识点：[容器选择指南](../containers/01-container-selection-guide.md)「选择决策树」一节

**参考实现**：

```cpp
#include <iostream>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

class LruCache {
public:
    explicit LruCache(int capacity) : capacity_(capacity) {}

    std::optional<int> get(int key)
    {
        auto it = index_.find(key);
        if (it == index_.end()) {
            return std::nullopt;
        }
        // 命中的条目挪到队首: splice 只改指针, 零拷贝
        order_.splice(order_.begin(), order_, it->second);
        return it->second->second;
    }

    void put(int key, int value)
    {
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = value;   // 直接改节点里的值
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        if (static_cast<int>(order_.size()) == capacity_) {
            int evict = order_.back().first;   // 队尾 = 最久未用
            order_.pop_back();
            index_.erase(evict);
        }
        order_.emplace_front(key, value);
        index_[key] = order_.begin();
    }

private:
    int capacity_;
    std::list<std::pair<int, int>> order_;   // 头=最新, 尾=最久未用
    std::unordered_map<int, std::list<std::pair<int, int>>::iterator> index_;
};

int main()
{
    LruCache cache(2);
    cache.put(1, 1);
    cache.put(2, 2);
    std::cout << cache.get(1).value() << ' ';             // 1
    cache.put(3, 3);                                      // 挤掉 key=2
    std::cout << (cache.get(2) ? 0 : -1) << ' ';          // -1
    std::cout << cache.get(3).value() << ' ';             // 3
    cache.put(4, 4);                                      // 挤掉 key=1
    std::cout << (cache.get(1) ? 0 : -1) << ' ';          // -1
    std::cout << cache.get(3).value() << ' ';             // 3
    std::cout << cache.get(4).value() << '\n';            // 4
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -O2 -Wall -Wextra hw3c3.cpp -o hw3c3 && ./hw3c3
1 -1 3 -1 3 4
```
