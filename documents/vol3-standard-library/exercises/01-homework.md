---
title: "卷 3 课后练习（Homework）"
description: "标准库深入卷的课后练习：6 章每章 2 题（基础+进阶），另加 2 道跨章综合与 1 道 L5 挑战（LRU 缓存，改编自 LeetCode 146）。难度覆盖 L1~L5，题目全部做变式处理，参考答案独立成文件、逐步解答附知识点链接。"
chapter: 3
order: 1
tags:
  - host
  - intermediate
  - cpp-modern
  - 容器
  - 类型安全
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20, 23]
reading_time_minutes: 14
prerequisites:
  - "卷 3 全部章节（容器、迭代器与算法、字符串、I/O、时间与数值、错误处理）"
related:
  - "卷 3 Lab：标准库解剖台"
  - "卷 3 Project：CSV 报表流水线"
---

# 卷 3 课后练习（Homework）

这里的题按章组织，每章两道（基础 + 进阶），最后是两道跨章综合和一道 L5 挑战。每题标注难度档位（L1~L5，见[练习总览](index.md)）和涉及章节。题目都是「变式」——换场景、换数据、换推理方向，照抄教材例题抄不出答案；每道题都要真编译真跑，把输出贴下来才算完。

答案在独立的[参考答案](02-homework-solutions.md)文件里，按题号对应，每步解答带知识点链接。建议一章做完再看答案。所有代码用 `-std=c++20` 起步（个别题目会要求 C++23 或 sanitizer 旗标，题面会写明）。

## 3.1 容器与数据结构

### 3.1-A {#hw-3-1-a}

难度 **L1** · 涉及[容器选择指南：按操作、内存与失效规则挑对容器](../containers/01-container-selection-guide.md)、[array：编译期固定大小的聚合容器](../containers/02-array.md)、[vector 深入：三指针、扩容与迭代器失效](../containers/03-vector-deep-dive.md)、[span：非拥有的连续视图](../containers/08-span.md)

写一个程序，用 `sizeof` 打印 `std::array<int,4>`、`std::vector<int>`、`std::list<int>`、`std::string`、`std::span<int>`、`std::span<int,4>` 六种类型各占多少字节；然后新建一个空 `vector<int>`，连续 `push_back` 16 个元素，每当 `capacity()` 发生变化就打印一行 size 与 capacity。回答三问：① `sizeof(std::array<int,4>)` 为什么恰好是 16？② capacity 的跳变序列说明本机 libstdc++ 的扩容倍率大概是多少？这个倍率是标准规定的吗？③ 同样是「指针 + 长度」的视图，`span<int>` 为什么比 `span<int,4>` 大 8 字节？编译用 `-std=c++20 -O2`，贴完整输出。（本题与 [Lab 步骤 1](03-lab.md#lab-1) 同源，Lab 是扩展版：多一条 string 扩容侦察。）

[参考答案 →](02-homework-solutions.md#hw-3-1-a)

### 3.1-B {#hw-3-1-b}

难度 **L2** · 涉及[map 与 set 深入：红黑树、异构查找与节点句柄](../containers/06-map-set-deep-dive.md)、[vector 深入：三指针、扩容与迭代器失效](../containers/03-vector-deep-dive.md)、[deque、list 与 forward_list：vector 之外的三个选择](../containers/05-deque-list-forward-list.md)、[容器选择指南：按操作、内存与失效规则挑对容器](../containers/01-container-selection-guide.md)

① 写一个事件注册表 `std::map<int,std::string>`，注册 2 个事件后，拿到 key=1 的引用和一个迭代器，然后一口气插入 1000 个新事件——引用和迭代器还能用吗？再用一个 `vector` 做对照：`reserve(2)` 后 push 两个元素、拿一个指向 `v[1]` 的指针，再 `push_back` 触发扩容——**先预测**旧指针还指向 `&v[1]` 吗？真跑验证，解释两套容器失效规则差在哪。② 用 `list::splice` 把第二个 list 的第 2、3 个节点「零拷贝」搬到第一个 list 的末尾，打印两个 list 验证。③ 一句话回答：为什么 `deque` 的迭代器失效比 `vector` 更「凶」——哪怕 erase 一个元素也让全部迭代器失效？（① 与 [Lab 步骤 2](03-lab.md#lab-2) 同源，Lab 是扩展版：多了 vector reserve 对照与 unordered_map rehash 两行。）

[参考答案 →](02-homework-solutions.md#hw-3-1-b)

## 3.2 迭代器与算法

### 3.2-A {#hw-3-2-a}

难度 **L2** · 涉及[迭代器适配器：反向、插入与流，把现成迭代器改出新行为](../iterators-algorithms/41-iterator-adapters.md)

① 用 `std::copy` + 三个插入迭代器，把 `v1{7,2,5}` 分别：追加两次到空 vector（`back_inserter`）、塞进空 `deque`（`front_inserter`，观察顺序反转）、插到 `{10,20,30}` 的 20 之前（`inserter`）。② 把 `front_inserter` 套到 `vector` 上——**先预测会发生什么**，再真编译，贴出报错的关键行。③ 反向迭代器：对 `{10,20,30,40,50}` 取 `rit = rbegin() + 1`（指向 40），打印 `*rit`、`*rit.base()`、`*(rit.base()-1)`，说清 `base()` 差一位的规则；再用 `std::sort(v.rbegin(), v.rend())` 给 `{3,1,4,1,5,9,2,6}` 拿降序，贴输出。

[参考答案 →](02-homework-solutions.md#hw-3-2-a)

### 3.2-B {#hw-3-2-b}

难度 **L3** · 涉及[算法总览（下）：排序、分区与堆](../iterators-algorithms/43-algorithm-overview-part2.md)、[numeric：累加、填充、内积与相邻差](../iterators-algorithms/44-numeric-algorithms.md)

造 8 个学生 `{name, score}`，分数用 double 且故意有并列（alice 90.5 / bob 88 / carol 90.5 / dave 95 / erin 76.5 / frank 88 / grace 91 / heidi 84），完成四件事：① 用 `nth_element` 求中位分数（口径：8 个里取第 4 位＝`begin() + size()/2`，0-based 下标 4、排序后第 5 个元素；严格中位数应是中间两值平均 89.25，`nth_element` 给的是「上中位数」）；② 用 `partial_sort` + `std::greater` 取前 3 名；③ 用 `std::ranges::sort` 的投影按 `score` 各排一次升序和降序（降序把比较器换成 `std::greater{}`），再用 `std::ranges::stable_sort` 升序排一次——对比并列的 88 分和 90.5 分两对，谁保持了输入顺序？④ 把这 8 个分数塞进 `vector<double>`，分别用 `std::accumulate(v, 0)` 和 `std::accumulate(v, 0.0)` 求和——**先算数学和**（应是 703.5），再预测两个结果、真跑验证，解释初始值怎么决定了返回类型。（统计部分与 [Lab 步骤 3](03-lab.md#lab-3) 同源，Lab 的变式是解析日志并处理坏行。）

[参考答案 →](02-homework-solutions.md#hw-3-2-b)

## 3.3 字符串与文本

### 3.3-A {#hw-3-3-a}

难度 **L1** · 涉及[string_view：非拥有的只读字符串视图](../strings/50-string-view.md)、[string 深入：SSO、COW 与 resize_and_overwrite](../containers/04-string-memory-deep-dive.md)

打印 `sizeof(std::string)` 与 `sizeof(std::string_view)`；把字符串 `"ftp://example.com:21/pub/file.txt"` 用 `string_view` 的 `substr`/`remove_suffix` 等视口操作切成 scheme、host、port、path 四段（全程不拷贝），打印四段；再用 C++20/23 的小接口测 `starts_with("ftp")`、`ends_with(".txt")`、`contains("ample")`。贴输出（注意 `contains` 需要 `-std=c++23`），并回答：这四段 view 的数据存在哪块内存里？

[参考答案 →](02-homework-solutions.md#hw-3-3-a)

### 3.3-B {#hw-3-3-b}

难度 **L3** · 涉及[charconv：零开销的数字与字符串互转](../strings/51-charconv.md)、[format：C++20 的类型安全格式化](../strings/52-format.md)、[string_view：非拥有的只读字符串视图](../strings/50-string-view.md)

写 `bool parse_row(string_view line, Row& out)` 解析「名字,年份,分数」三字段（如 `"alice,1998,3.140"`）：用 `string_view::find` 切字段，`std::from_chars` 解析 int 年份与 double 分数。要求：① 检查 `from_chars` 的 `ec` **并且**检查 `ptr` 是否走到字段末尾——否则 `"19x9"` 这种前缀解析会蒙混过关，题目给的第三个输入就是来抓这个漏的；② 解析 `"bob, 2001,2.718"`——注意年份前有个空格，`from_chars` 不跳空白会失败，写出你的 trim 方案并验证；③ 用 `to_chars` 把分数写进 32 字节缓冲，用 `(buf, r.ptr)` 构造 string（**不要**直接把 buf 当 C 字符串用），打印长度；④ 用 `std::format` 输出对齐表格（名字左对齐 8、年份右对齐 6、分数 `>8.2f`）。对 `"alice,1998,3.140"`、`"bob, 2001,2.718"`、`"carol,19x9,1.5"` 三个输入贴完整输出（`-std=c++23`）。

[参考答案 →](02-homework-solutions.md#hw-3-3-b)

## 3.4 I/O 与文件系统

### 3.4-A {#hw-3-4-a}

难度 **L2** · 涉及[fstream：文件流读写、RAII 与它的可移植性坑](../io/56-fstream.md)

① 往 `/tmp` 下某文件先写 `"OLD"`，再默认打开写 `"new"`——**先预测**第一次写的内容去哪了，真跑读回验证；接着用 `std::ios::app` 追加 `"PLUS"` 再读回。② 打开一个不存在的文件、**不检查** `is_open()` 就 `>>` 读一个 int——那个 int 会变成什么？贴输出；给出「打开后立刻检查、失败早退」的正确写法。③ 对一个三行文本文件，分别用 `while (std::getline(in, line))` 和 `while (!in.eof())` 数行数——**先预测**后者的结果，真跑验证并解释为什么多 1。

[参考答案 →](02-homework-solutions.md#hw-3-4-a)

### 3.4-B {#hw-3-4-b}

难度 **L3** · 涉及[filesystem：C++17 跨平台文件系统操作](../io/57-filesystem.md)、[error_code：错误码体系与自定义 category](../error-utils/66-error-code.md)

① 用 `create_directories` 造一棵小目录树（两层子目录 + 3 个文件，内容自己定），用 `recursive_directory_iterator` 遍历：**用 `directory_entry` 的成员** `is_regular_file()`/`file_size()` 统计文件数与总字节——想想为什么不用 `fs::file_size(e.path())`。② 对不存在的路径调 `fs::file_size`，分别走异常版（贴 `what()`）和 `error_code` 版（贴 `value` 与 `message`）。③ `path` 拼接：`base / "/etc/x"` 与 `base / "etc/x"` 各得到什么？贴输出并解释「右边带根会吃掉左边」。（本题与 [Lab 步骤 5](03-lab.md#lab-5) 同源，Lab 是扩展版：多 format_to_n 截断检测。）

[参考答案 →](02-homework-solutions.md#hw-3-4-b)

## 3.5 时间与数值

### 3.5-A {#hw-3-5-a}

难度 **L2** · 涉及[chrono：duration、时钟与 C++20 日历](../time-numeric/58-chrono.md)

① 打印三个时钟的 `is_steady`，说清测耗时为什么只能用 `steady_clock`。② 写一行 `seconds s = milliseconds{500};` **先预测**能不能编译，真编译贴报错，解释「有损方向禁止隐式转换」。③ 对 `milliseconds{1999}` 分别用 `duration_cast<seconds>`、`floor<seconds>`、`ceil<seconds>`、`round<seconds>` 转秒，贴四个结果并解释 `round` 的银行家舍入。④ 用 `steady_clock` 量一个忙等 100ms 循环的真实耗时，贴输出。

[参考答案 →](02-homework-solutions.md#hw-3-5-a)

### 3.5-B {#hw-3-5-b}

难度 **L3** · 涉及[random：为什么别再用 rand()](../time-numeric/60-random.md)、[cmath：数学函数、浮点分类与精度陷阱](../time-numeric/59-cmath.md)

① 用 `std::mt19937(42)` + `uniform_int_distribution<int>(1,6)` 掷 60000 次骰子，统计 6 个面各自次数；对照 `srand(42)` + `rand()%6+1` 再做一遍。贴两组分布；回答：`uniform_int_distribution` 的区间是开还是闭？`eng() % 6` 的取模偏差是怎么被拒绝采样消掉的？② `std::sqrt(1e200*1e200 + 1e200*1e200)` 和 `std::hypot(1e200, 1e200)` 各是多少？解释朴素写法的 inf 从哪来。③ `NaN == NaN` 是真是假？该用什么函数判 NaN？④ 0.1 累加 10 次 `== 1.0` 是真是假？贴 17 位精度的输出。

[参考答案 →](02-homework-solutions.md#hw-3-5-b)

## 3.6 错误处理与工具

### 3.6-A {#hw-3-6-a}

难度 **L2** · 涉及[optional：把「可能没有」做成类型](../error-utils/61-optional.md)

① 定义一个带构造/析构日志的 `struct User`，用 `optional<User>` 依次 `emplace("alice",30)` → 再 `emplace("bob",25)` → `reset()`，贴出完整构造/析构日志，说清「再 emplace 先析构旧的」。② 对空 optional 依次用四种访问方式：`has_value()`、`value()`（接住异常贴 `what()`）、`value_or(-1)`、以及 `*empty`——最后这个先别跑，**先预测**；然后把它单独写成一个小程序，用 `-D_GLIBCXX_ASSERTIONS` 编译运行，贴断言信息与退出码（普通构建下它「看起来没事」，正是最阴险处）。③ 变式查找：写 `optional<int> find_first_even(const int*, int n)` 返回**下标**（教材例题返回的是值），对全奇数组与含偶数组各测一次。

[参考答案 →](02-homework-solutions.md#hw-3-6-a)

### 3.6-B {#hw-3-6-b}

难度 **L3** · 涉及[variant：类型安全的联合体与 visit](../error-utils/62-variant.md)、[expected：值或错误，C++23 的错误处理新范式](../error-utils/64-expected.md)、[charconv：零开销的数字与字符串互转](../strings/51-charconv.md)

① 用 `variant<int,double,string>` 存 `{42, 3.14, "hello", 7}` 四个值，`std::visit` + `overloaded` 逐一打印（int/double/string 三种分支）。② 写一条「摄氏→华氏」的温度转换链：`parse_celsius(string_view)` 用 `from_chars` 解析 double 并拒绝低于 -273.15 的输入（返回 `std::expected<double, temp_error>`），再 `and_then(to_fahrenheit)`（×9/5+32）、`transform(fmt_fahrenheit)`（用 `to_chars` 拼 `"97.7 F"` 这种字符串）。对 `"36.5"`、`"abc"`、`"-300"` 三个输入跑这条链（`-std=c++23`），贴输出，说清哪一步短路、两个错误码各是多少。（② 与 [Lab 步骤 6](03-lab.md#lab-6) 同源，Lab 是扩展版：错误类型换成 error_code 自定义 category。）

[参考答案 →](02-homework-solutions.md#hw-3-6-b)

## 3.C 跨章综合与挑战

### 3.C-1 {#hw-3-c-1}

难度 **L3** · 涉及[string_view：非拥有的只读字符串视图](../strings/50-string-view.md)、[charconv：零开销的数字与字符串互转](../strings/51-charconv.md)、[算法总览（下）：排序、分区与堆](../iterators-algorithms/43-algorithm-overview-part2.md)、[numeric：累加、填充、内积与相邻差](../iterators-algorithms/44-numeric-algorithms.md)、[format：C++20 的类型安全格式化](../strings/52-format.md)

综合题：写一个「成绩日志流水线」。给定 8 行「名字 分数」日志（alice 90.5 / bob 88 / carol 90.5 / dave 95 / erin 76.5 / frank 88 / grace 91 / heidi 84），程序要：`string_view::find` 切两段 → `from_chars` 解析分数（检查 ec 与 ptr）→ 存 `vector<Entry>`。输出四项：① 平均分（`accumulate` 初始值**必须** 0.0，想想为什么）；② 中位数（`nth_element`）；③ 前 3 名（`partial_sort` 降序）；④ 按名字排序的完整表格（`std::format` 对齐）。要求 `-Wall -Wextra` 编译零警告（`-std=c++23`），贴输出，并说明你的中位数口径（偶数个元素取哪个位置）。（本题与 [Lab 步骤 3](03-lab.md#lab-3) 同源，Lab 是加了坏行处理的变式。）

[参考答案 →](02-homework-solutions.md#hw-3-c-1)

### 3.C-2 {#hw-3-c-2}

难度 **L4** · 涉及[string_view：非拥有的只读字符串视图](../strings/50-string-view.md)、[vector 深入：三指针、扩容与迭代器失效](../containers/03-vector-deep-dive.md)、[unordered_map 与 unordered_set 深入：哈希表、桶与自定义 hash](../containers/07-unordered-map-set-deep-dive.md)、[map 与 set 深入：红黑树、异构查找与节点句柄](../containers/06-map-set-deep-dive.md)、[容器选择指南：按操作、内存与失效规则挑对容器](../containers/01-container-selection-guide.md)

失效与悬垂侦探（Core Guidelines / CSAPP 风格：每个 bug 都要「先预测 → 工具实测 → 贴报告 → 修复」）。下面四段代码「看着能跑」，请你逐一处理：

1. `std::string s = "abc"; std::string_view sv = s + "x";`，随后做几次其他字符串分配，再打印 `sv`（用 `-fsanitize=address` 跑）。
2. 拿到 `v.begin()+1` 后向 vector 狂 `push_back` 到触发扩容，再解引用这个旧迭代器（用 `-fsanitize=address` 跑）。
3. `unordered_map` 里 `find(2)` 拿到迭代器后立刻 `erase(2)`，再 `it->second`（用 `-fsanitize=address` 或 `-D_GLIBCXX_DEBUG` 跑）。
4. `map<int,int>` 边遍历边 `m.erase(it)` 删偶数 key（普通构建跑一次、`-D_GLIBCXX_DEBUG` 再跑一次）。

修复要求：① 先物化成 `std::string` 再取 view；② `reserve`；③ 删除前先把内容取出来，或用引用观察（C++14 起 rehash 不失效引用）；④ `it = m.erase(it)` 惯用法。全部修复后 sanitizer/debug 构建零报告。每题贴出工具的关键报告行（如 `heap-use-after-free`、`stack-use-after-scope`、debug 断言），并对照教材的失效规则表解释机理。（教材外补充：`-D_GLIBCXX_DEBUG` 是 libstdc++ 的 debug 模式断言，与 ASan 的运行期插桩分工不同，卷 3 各章未讲授，两者分工见参考答案 [3.C-2](02-homework-solutions.md#hw-3-c-2)。① 的悬垂 view 与 [Lab 步骤 4](03-lab.md#lab-4) 同源。）

[参考答案 →](02-homework-solutions.md#hw-3-c-2)

### 3.C-3 {#hw-3-c-3}

难度 **L5** · 涉及[deque、list 与 forward_list：vector 之外的三个选择](../containers/05-deque-list-forward-list.md)、[unordered_map 与 unordered_set 深入：哈希表、桶与自定义 hash](../containers/07-unordered-map-set-deep-dive.md)、[容器选择指南：按操作、内存与失效规则挑对容器](../containers/01-container-selection-guide.md)、[optional：把「可能没有」做成类型](../error-utils/61-optional.md)

挑战题（改编自 **LeetCode 146 LRU Cache**；本卷 L5＝「用本卷知识可解的最难问题」，档位口径见[练习总览](index.md)）。设计并实现一个 LRU（最近最少使用）缓存：容量 N，`get(key)` 返回缓存值或空，`put(key,value)` 写入并在满时淘汰最久未用条目，两个操作都必须**平均 O(1)**。要求用本卷的组合：`std::list<pair<int,int>>` 按新旧排（头最新、尾最久）+ `std::unordered_map<int, list::iterator>` 做索引 + `list::splice` 把命中的节点零拷贝挪到队首。回答：为什么 `vector` 做不了这题？`splice` 在这里省掉了什么？验证用例（容量 2）：`put(1,1) put(2,2) get(1) put(3,3) get(2) get(3) put(4,4) get(1) get(3) get(4)`，输出应为 `1 -1 3 -1 3 4`。`-Wall -Wextra` 零警告，贴运行输出。

[参考答案 →](02-homework-solutions.md#hw-3-c-3)
