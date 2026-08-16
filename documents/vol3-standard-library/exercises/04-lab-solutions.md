---
title: "卷 3 Lab 实验参考"
description: "标准库解剖台 Lab 的实验参考：六个步骤加 L5 挑战的完整代码与逐步解答，每步标注知识点链接，所有输出在 WSL Arch（g++ 16.1.1）真实运行得到，悬垂与失效类题目附 ASan 实测报告。"
chapter: 3
order: 4
tags:
  - host
  - intermediate
  - cpp-modern
  - 实战
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20, 23]
reading_time_minutes: 17
prerequisites:
  - "卷 3 Lab 题面"
related:
  - "卷 3 课后练习（Homework）"
  - "卷 3 Project：CSV 报表流水线"
---

# 卷 3 Lab 实验参考

> 所有输出在 WSL Arch（g++ 16.1.1）真实运行得到。建议卡住时先看「思路」逐步对照；参考实现只是**一种**过关方式，你的实现不一样、验收标准对得上，就都是对的。

## 步骤 1：容器侦察 {#lab-1}

**思路**：`sizeof` 摸骨架，capacity 序列摸扩容节奏。

1. `array<int,4>` 是聚合类型零开销（4×4=16）；`vector<int>` 是三指针 24；`list<int>` 是双向链表 24；`string` 是 32（SSO）；`span<int>` 是「指针+长度」16，`span<int,4>` 长度编译期已知只存指针 8。→ 知识点：[array](../containers/02-array.md)「array 到底是什么」一节、[vector 深入](../containers/03-vector-deep-dive.md)「三个指针」一节、[span](../containers/08-span.md)「动态 extent 与静态 extent」一节
2. vector 容量 `1 → 2 → 4 → 8 → 16`（约 2×，标准未规定）；空 string 的 `capacity()` 已经是 **15**（SSO 内联缓冲），push 到第 16 个字符才出堆，之后 `15 → 30 → 60` 翻倍——输出里 `string push #1: size=1 capacity=15` 不是扩容：参考实现对首次 push 做了特判，把初始容量也打了出来。→ 知识点：[vector 深入](../containers/03-vector-deep-dive.md)「扩容这件事」一节、[string 深入](../containers/04-string-memory-deep-dive.md)「SSO 的阈值」一节

**验证输出**：

```text
$ g++ -std=c++20 -O2 lab1.cpp -o lab1 && ./lab1
sizeof(array<int,4>)  = 16
sizeof(vector<int>)   = 24
sizeof(list<int>)     = 24
sizeof(string)        = 32
sizeof(span<int>)     = 16
sizeof(span<int,4>)   = 8
vector push #1: size=1 capacity=1
vector push #2: size=2 capacity=2
vector push #3: size=3 capacity=4
vector push #5: size=5 capacity=8
vector push #9: size=9 capacity=16
string push #1: size=1 capacity=15     ← 特判打印初始容量（容量其实没变）
string push #16: size=16 capacity=30
string push #31: size=31 capacity=60
```

## 步骤 2：迭代器失效对照表 {#lab-2}

**思路**：四条对照线各对应失效表的一行。

1. a 与 b 对照：不 reserve 时扩容换缓冲，旧指针失效（打印 0）；`reserve(100)` 后 push 不触发扩容，旧指针仍然有效（打印 1）。→ 知识点：[vector 深入](../containers/03-vector-deep-dive.md)「迭代器失效：一张表讲完所有规矩」一节
2. c：map 插入**永不失效**已有引用，插 1000 个后 `ref` 还是 `alpha`。→ 知识点：[map 与 set 深入](../containers/06-map-set-deep-dive.md)「复杂度和迭代器失效」一节
3. d：unordered_map 的 rehash 会失效迭代器，但 **C++14 起引用和指针不失效**——`reserve(1000)` 触发 rehash 后 `uref` 仍是 `alpha`。→ 知识点：[unordered_map 与 unordered_set 深入](../containers/07-unordered-map-set-deep-dive.md)「复杂度与迭代器失效」一节

**验证输出**：

```text
$ g++ -std=c++20 -O2 lab2.cpp -o lab2 && ./lab2
a. vector 扩容后旧指针仍指向 &v[1]? 0
b. vector reserve(100) 后旧指针仍指向 &v[1]? 1
c. map 插 1000 个后 ref = alpha
d. unordered_map rehash 后引用 = alpha
```

## 步骤 3：文本解析流水线 {#lab-3}

**思路**：`string_view` 切、`from_chars` 解析（ec + ptr 双检查）、`nth_element`/`partial_sort` 按需排、`accumulate` 防截断、`format` 出表。

1. `find(' ')` 切两段，`from_chars` 解析并检查 `ec` 与 `ptr`；`bad_line` 没有空格，计数失败。→ 知识点：[string_view](../strings/50-string-view.md)「视口操作」一节、[charconv](../strings/51-charconv.md)「from_chars」一节
2. 平均分 88.10 靠 `accumulate(..., 0.0, ...)` 的 double 初始值；传 0 会截断。→ 知识点：[numeric](../iterators-algorithms/44-numeric-algorithms.md)「accumulate」一节
3. `nth_element` 第 `size()/2` 位得中位数 90.5；`partial_sort` 前 2 名。→ 知识点：[算法总览（下）](../iterators-algorithms/43-algorithm-overview-part2.md)「nth_element」「partial_sort」两节
4. `ranges::sort(entries, {}, &Entry::name)` 投影按名字排，`format` 对齐输出。→ 知识点：同上「C++20 投影」一节、[format](../strings/52-format.md)「格式说明」一节

**完整代码**：

```cpp
#include <algorithm>
#include <charconv>
#include <format>
#include <iostream>
#include <numeric>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

struct Entry {
    std::string name;
    double score;
};

int main()
{
    const char* raw[] = {
        "alice 90.5",
        "bob 88",
        "carol 90.5",
        "dave 95",
        "bad_line",
        "erin 76.5",
    };
    std::vector<Entry> entries;
    int failed = 0;
    for (const char* line : raw) {
        std::string_view sv(line);
        const auto sp = sv.find(' ');
        if (sp == std::string_view::npos) {
            ++failed;
            continue;
        }
        Entry e;
        e.name = std::string(sv.substr(0, sp));
        auto num = sv.substr(sp + 1);
        auto r = std::from_chars(num.data(), num.data() + num.size(), e.score);
        if (r.ec != std::errc{} || r.ptr != num.data() + num.size()) {
            ++failed;
            continue;
        }
        entries.push_back(std::move(e));
    }
    std::cout << "解析成功 " << entries.size() << " 行, 失败 " << failed << " 行\n";

    // 平均分: 初始值必须 0.0, 否则截断
    double total = std::accumulate(entries.begin(), entries.end(), 0.0,
                                   [](double acc, const Entry& e) { return acc + e.score; });
    std::cout << std::format("平均分 = {:.2f}\n", total / static_cast<double>(entries.size()));

    // 中位数: nth_element 只保证第 n 位到位
    std::vector<double> scores;
    for (const auto& e : entries) scores.push_back(e.score);
    auto mid = scores.begin() + scores.size() / 2;
    std::nth_element(scores.begin(), mid, scores.end());
    std::cout << "中位数 = " << *mid << '\n';

    // 前 2 名: partial_sort
    std::partial_sort(entries.begin(), entries.begin() + 2, entries.end(),
                      [](const Entry& a, const Entry& b) { return a.score > b.score; });
    std::cout << "前 2 名:\n";
    for (int i = 0; i < 2; ++i) {
        std::cout << std::format("  {}. {:<8} {:>5.1f}\n", i + 1, entries[i].name, entries[i].score);
    }

    // 全表: ranges::sort 按名字
    std::ranges::sort(entries, {}, &Entry::name);
    std::cout << "全表(按名字):\n";
    for (const auto& e : entries) {
        std::cout << std::format("  {:<8} {:>5.1f}\n", e.name, e.score);
    }
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++23 -O2 lab3.cpp -o lab3 && ./lab3
解析成功 5 行, 失败 1 行
平均分 = 88.10
中位数 = 90.5
前 2 名:
  1. dave      95.0
  2. carol     90.5
全表(按名字):
  alice     90.5
  bob       88.0
  carol     90.5
  dave      95.0
  erin      76.5
```

注意前 2 名的第 2 位是 carol——`partial_sort` 不保证并列者（carol/alice 都是 90.5）的顺序，本机这次轮到 carol。

## 步骤 4：string 内存解剖 {#lab-4}

**思路**：SSO 阈值用「`data()` 是否落在对象内」探测；`resize_and_overwrite` 是「拿 string 当缓冲接 C API」的正确姿势；悬垂 view 交给 ASan 戳破。

1. 从长度 0 扫到 40，长度 15 还在对象内、16 出堆——**SSO 阈值 = 15**（libstdc++ 16 的实现细节，别当硬性假设写进代码）。→ 知识点：[string 深入](../containers/04-string-memory-deep-dive.md)「SSO 的阈值」一节
2. 老写法 `resize(64)` 先清零再被覆盖；C++23 `resize_and_overwrite` 不清零、回调报告实际长度。两种都接住 `fake_read` 的 5 字节 `"hello"`，但后者省掉 64 个字符的值初始化。→ 知识点：同上「resize_and_overwrite」一节
3. `bad_return()` 返回局部 `string` 的视图：普通构建下 `sv` 打出一段垃圾（`size` 仍是 11——长度在构造时抄进对象，析构不影响它）；ASan 报 `stack-use-after-return`，直接点名 `bad_return()` 帧里的 `local`。→ 知识点：[string_view](../strings/50-string-view.md)「最大的坑：它不持有，会悬垂」一节

**SSO 与 resize_and_overwrite 的验证输出**：

```text
$ g++ -std=c++23 -O2 lab4.cpp -o lab4 && ./lab4
sizeof(std::string) = 32
长度 15 还在对象内, 长度 16 出堆 -> SSO 阈值 = 15
resize 老写法: 'hello' (len=5)
resize_and_overwrite: 'hello' (len=5)
```

**悬垂 view 的验证输出**：

```text
$ g++ -std=c++20 -O2 lab4_dangle.cpp -o lab4_dangle && ./lab4_dangle
sv: [<乱码字节>] size=11                     ← 普通构建: 打印垃圾
$ g++ -std=c++20 -O1 -g -fsanitize=address lab4_dangle.cpp -o lab4_asan && ./lab4_asan
==317==ERROR: AddressSanitizer: stack-use-after-return on address 0x79d2b02f0090 ...
READ of size 11 at 0x79d2b02f0090 thread T0
    #3 0x5ba1e5d78660 in main /tmp/cpp-v3-lab/lab4_dangle.cpp:14
Address 0x79d2b02f0090 is located in stack of thread T0 at offset 144 in frame
    #0 0x5ba1e5d78238 in bad_return() /tmp/cpp-v3-lab/lab4_dangle.cpp:6
  This frame has 4 object(s):
    [128, 160) 'local' (line 7) <== Memory access at offset 144 is inside this variable
SUMMARY: AddressSanitizer: stack-use-after-return ...
```

## 步骤 5：文件系统报表与截断检测 {#lab-5}

**思路**：遍历用 entry 成员（命中缓存，省一次 stat）；`format_to_n` 的 `res.size` 是「完整长度」而不是「实际写入长度」，截断判定靠它；错误双路径各走一遍。

1. 三个文件、24 字节；`e.is_regular_file()` / `e.file_size()` 用遍历时已缓存的 stat，别写 `fs::file_size(e.path())`（多一次系统调用）。→ 知识点：[filesystem](../io/57-filesystem.md)「directory_entry 的缓存」一节
2. 报表行 `"{:<16} {:>6}"` 的完整长度是 23（16 宽左对齐文件名 + 1 空格 + 6 宽右对齐字节数），8 字节缓冲装不下，`res.size=23 > 7` 判截断。→ 知识点：[format](../strings/52-format.md)「format_to」一节与「format_to_n 的 res.size 是完整长度」提示块
3. 异常版 `what()` 带操作名与路径；error_code 版不抛、`value=2`（ENOENT）。→ 知识点：[filesystem](../io/57-filesystem.md)「错误处理：异常与 error_code 双路径」一节、[error_code](../error-utils/66-error-code.md)「error_code 的构成」一节

**验证输出**：

```text
$ g++ -std=c++23 -O2 lab5.cpp -o lab5 && ./lab5
文件数 = 3, 总字节 = 24
buf = [a.txt  ] 完整长度 = 23 截断? true
buf = [b.txt  ] 完整长度 = 23 截断? true
buf = [c.txt  ] 完整长度 = 23 截断? true
异常版 what: filesystem error: cannot get file size: No such file or directory [/tmp/cpp-v3-lab/definitely_missing_xyz]
error_code 版: value=2 message=No such file or directory
```

## 步骤 6：expected + error_code 错误链 {#lab-6}

**思路**：自定义 category 四步走（枚举 → trait → category → make_error_code），`default_error_condition` 是跨体系比较的桥梁；`expected` 的 `transform`/`and_then` 串起解析链。

1. 四步搭 `MyErrc` 体系。第 3 步里 `default_error_condition` 把 `kTimeout` 映射到 `errc::timed_out`，于是 `error_code{MyErrc::kTimeout, my_category()} == std::errc::timed_out` 为 **1**。→ 知识点：[error_code](../error-utils/66-error-code.md)「自定义 category：从零搭一套错误码体系」一节
2. `parse_value` 三段失败：无 `=` 给 `MyErrc::kBadFormat`；`from_chars` 失败（`"1a"`）给标准 `errc::invalid_argument`（generic category）；>1000 给 `MyErrc::kTooBig`。→ 知识点：[expected](../error-utils/64-expected.md)「错误类型 E 怎么选」一节、[charconv](../strings/51-charconv.md)「from_chars」一节
3. 链 `parse_value(...).transform(翻倍).and_then(检查)`：`transform` 只改值、`and_then` 可再失败；失败逐行打印 `message` 与 category。3 条成功（90、88、120 → 翻倍后总和 180+176+240=596），3 条失败。→ 知识点：[expected](../error-utils/64-expected.md)「C++23 的 monadic」一节

**验证输出**：

```text
$ g++ -std=c++23 -O2 lab6.cpp -o lab6 && ./lab6
tc.message() = 操作超时  tc == errc::timed_out? 1
"score=1a" -> Invalid argument (cat=generic)
"bad" -> 格式错误 (cat=my-app)
"score=5000" -> 数值太大 (cat=my-app)
成功 3 条, 失败 3 条, 总和(翻倍后) = 596
```

## 附加挑战（L5）：第 k 小距离对 {#lab-l5}

**思路**：n² 对距离不能枚举；排序后「数 ≤ d 的对数」可以用 `upper_bound` 对每个 i 二分完成，再对答案 d 二分。`count_pairs` 用 `upper_bound(a[i] + d)` 而不是 `lower_bound`：我们要数 `a[j] - a[i] <= d` 即 `a[j] <= a[i] + d`，`upper_bound` 给的是「第一个 > a[i]+d 的位置」，减掉起点就是「≤ d」的个数——`lower_bound` 会把**恰好等于** `a[i]+d` 的那些漏掉，边界差一个。

1. `kth_smallest_distance` 先排序，再在 `[0, max-min]` 上二分答案，`count_pairs ≥ k` 就收右端。→ 知识点：[算法总览（下）](../iterators-algorithms/43-algorithm-overview-part2.md)「排序家族」一节、[算法总览（上）](../iterators-algorithms/42-algorithm-overview-part1.md)「二分那一族」一节
2. n=200 小样本与暴力对照：第 12345 小距离 39241，两法一致（true）。→ 知识点：同上「upper_bound」一节
3. n=10000、k=3000000：答案 3047，耗时 3 ms——比 n² 枚举快了几个数量级。→ 知识点：[chrono](../time-numeric/58-chrono.md)「测耗时为什么只能用 steady_clock」一节

**完整代码**：

```cpp
#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <vector>

// 数出距离 <= d 的数对数: O(n log n)
long long count_pairs(const std::vector<long long>& a, long long d)
{
    long long c = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        c += static_cast<long long>(
            std::upper_bound(a.begin() + static_cast<ptrdiff_t>(i + 1), a.end(), a[i] + d)
            - (a.begin() + static_cast<ptrdiff_t>(i + 1)));
    }
    return c;
}

// 排序 + 二分答案
long long kth_smallest_distance(std::vector<long long> a, long long k)
{
    std::sort(a.begin(), a.end());
    long long lo = 0;
    long long hi = a.back() - a.front();
    while (lo < hi) {
        long long mid = lo + (hi - lo) / 2;
        if (count_pairs(a, mid) >= k) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return lo;
}

int main()
{
    // ① 小样本: 与暴力结果对照
    std::mt19937 rng(42);
    std::vector<long long> small(200);
    std::uniform_int_distribution<long long> dist(0, 100000);
    for (auto& x : small) x = dist(rng);
    std::vector<long long> brute;
    brute.reserve(small.size() * (small.size() - 1) / 2);
    for (std::size_t i = 0; i < small.size(); ++i) {
        for (std::size_t j = i + 1; j < small.size(); ++j) {
            brute.push_back(std::llabs(small[i] - small[j]));
        }
    }
    std::sort(brute.begin(), brute.end());
    long long k_small = 12345;
    long long got = kth_smallest_distance(small, k_small);
    std::cout << "n=200 对照: 暴力第 " << k_small << " 小 = " << brute[static_cast<std::size_t>(k_small - 1)]
              << ", 二分答案 = " << got << ", 一致? " << std::boolalpha
              << (got == brute[static_cast<std::size_t>(k_small - 1)]) << '\n';

    // ② 大样本: n=10000, 计时
    std::vector<long long> big(10000);
    for (auto& x : big) x = dist(rng);
    auto t0 = std::chrono::steady_clock::now();
    long long k_big = 3000000;
    long long ans = kth_smallest_distance(std::move(big), k_big);
    auto t1 = std::chrono::steady_clock::now();
    std::cout << "n=10000, k=3000000: 第 k 小距离 = " << ans << "  耗时 "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
              << " ms\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++20 -O2 labl5.cpp -o labl5 && ./labl5
n=200 对照: 暴力第 12345 小 = 39241, 二分答案 = 39241, 一致? true
n=10000, k=3000000: 第 k 小距离 = 3047  耗时 3 ms
```
