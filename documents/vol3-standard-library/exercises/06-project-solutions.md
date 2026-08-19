---
title: "卷 3 Project 参考实现"
description: "CSV 报表流水线项目的完整参考实现：四层任务逐步讲解，每步标注知识点链接，含 charconv 解析、统计层、expected 错误链与 -Wconversion 质量门、外部排序 k 路归并的真实运行输出（g++ 16.1.1 / WSL Arch 实跑）。"
chapter: 3
order: 6
tags:
  - host
  - intermediate
  - cpp-modern
  - 工程实践
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20, 23]
reading_time_minutes: 17
prerequisites:
  - "卷 3 Project 题面"
related:
  - "卷 3 课后练习（Homework）"
  - "卷 3 Lab：标准库解剖台"
---

# 卷 3 Project 参考实现

> 全部输出在 WSL Arch（g++ 16.1.1）真实运行得到。参考实现只是**一种**过关方式；你的实现不一样、验收标准对得上，就都是对的。代码风格跟教程统一：函数大括号 Allman 换行、缩进 4 空格、常量 `kPascalCase`。

## 核心任务（L2）：能跑起来的报表 {#pj-core}

**思路**：`Row` 先定义；解析只信 `from_chars` 的 `ec` **加** `ptr`（两个都查才算数）；`reserve(64)` 预撑容量，后面只 `push_back`——这就是本层定下的「迭代器失效审计」基调：**全程不持有任何迭代器**，扩容与否都不影响正确性。

**L1 热身输出**——骨架只放类型定义与空函数声明，先验证工具链与体积：

```text
$ g++ -std=c++23 -O2 -Wall -Wextra warmup.cpp -o warmup && ./warmup
工具链侦察:
  sizeof(Row)          = 48
  sizeof(string)       = 32
  sizeof(vector<Row>)  = 24
  sizeof(span<Row>)    = 16
  版本: __cplusplus    = 202302
```

`sizeof(Row)=48`：`string` 32 + `double` 8 + `int` 4 + 对齐填充 4——如果对 48 的来历好奇，回[对象大小、对齐与平凡类型](../containers/12-object-size-and-trivial-types.md)补课。→ 知识点：[对象大小、对齐与平凡类型](../containers/12-object-size-and-trivial-types.md)「大小与对齐」一节

**`pj_core.cpp`**——解析 + 表格：

```cpp
#include <charconv>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

struct Row {
    std::string name;
    double score;
    int year;
};

// 解析 "名字,分数,年份" 三段; 失败返回 false
bool parse_row(std::string_view line, Row& out)
{
    const auto c1 = line.find(',');
    const auto c2 = line.find(',', c1 + 1);
    if (c1 == std::string_view::npos || c2 == std::string_view::npos) {
        return false;
    }
    out.name = std::string(line.substr(0, c1));
    auto score_sv = line.substr(c1 + 1, c2 - c1 - 1);
    auto year_sv = line.substr(c2 + 1);

    auto r1 = std::from_chars(score_sv.data(), score_sv.data() + score_sv.size(), out.score);
    if (r1.ec != std::errc{} || r1.ptr != score_sv.data() + score_sv.size()) {
        return false;
    }
    auto r2 = std::from_chars(year_sv.data(), year_sv.data() + year_sv.size(), out.year);
    if (r2.ec != std::errc{} || r2.ptr != year_sv.data() + year_sv.size()) {
        return false;
    }
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "用法: csvreport <数据文件>\n";
        return 1;
    }
    std::ifstream in(argv[1]);
    if (!in.is_open()) {
        std::cerr << "打不开文件: " << argv[1] << '\n';
        return 1;
    }

    std::vector<Row> rows;
    rows.reserve(64);   // 预撑容量: 后面只 push_back, 不持有任何迭代器
    int bad = 0;
    std::string line;
    while (std::getline(in, line)) {
        Row r;
        if (!parse_row(line, r)) {
            ++bad;
            continue;
        }
        rows.push_back(std::move(r));
    }
    std::cout << std::format("解析 {} 行, 坏行 {}\n", rows.size(), bad);
    std::cout << std::format("{:<8} {:>6} {:>6}\n", "名字", "分数", "年份");
    for (const auto& r : rows) {
        std::cout << std::format("{:<8} {:>6.1f} {:>6}\n", r.name, r.score, r.year);
    }
    return 0;
}
```

→ 知识点：[string_view](../strings/50-string-view.md)「视口操作」一节、[charconv](../strings/51-charconv.md)「from_chars」一节与「忽略返回值里的 ec」提示块、[fstream](../io/56-fstream.md)「错误检查」一节、[format](../strings/52-format.md)「格式说明」一节、[vector 深入](../containers/03-vector-deep-dive.md)「临了收几句」（reserve 的工程习惯）

**验证输出**：

```text
$ g++ -std=c++23 -O2 -Wall -Wextra pj_core.cpp -o pj_core && ./pj_core data.csv
解析 8 行, 坏行 1
名字       分数   年份
alice      90.5   2022
bob        88.0   2021
carol      90.5   2022
dave       95.0   2020
erin       76.5   2021
frank      88.0   2022
grace      91.0   2020
heidi      84.0   2021
```

## 进阶任务（L3）：统计层 {#pj-avg}

**思路**：平均分的总分必须用 `double` 初始值——`accumulate(..., 0)` 会把每个分数先截断成 int，703.5 变 702；中位数 `nth_element` 只钉第 `size()/2` 位；前 3 名 `partial_sort`；按年份分组用 `map`（key 自动有序）；按名字查用「投影排序 + 投影二分」。

在核心版上追加（完整程序就是 `pj_full.cpp`，这里只贴新增部分）：

```cpp
double calc_avg(const std::vector<Row>& rows)
{
    // 初始值必须 0.0: 传 0 会把每个分数截断成 int
    double total = std::accumulate(rows.begin(), rows.end(), 0.0,
                                   [](double acc, const Row& r) { return acc + r.score; });
    return total / static_cast<double>(rows.size());
}

double calc_median(std::vector<double> scores)
{
    auto mid = scores.begin() + scores.size() / 2;
    std::nth_element(scores.begin(), mid, scores.end());
    return *mid;
}
```

统计输出部分：

```cpp
    // top3: partial_sort 只排前 3
    auto top = rows;
    std::partial_sort(top.begin(), top.begin() + 3, top.end(),
                      [](const Row& a, const Row& b) { return a.score > b.score; });
    // by-year: map 按 key 有序
    std::map<int, std::vector<double>> by_year;
    for (const auto& r : rows) {
        by_year[r.year].push_back(r.score);
    }
    // find by name: 排序 + 二分 (ranges::lower_bound 带投影)
    auto by_name = rows;
    std::ranges::sort(by_name, {}, &Row::name);
    auto it = std::ranges::lower_bound(by_name, "frank", {}, &Row::name);
```

→ 知识点：[numeric](../iterators-algorithms/44-numeric-algorithms.md)「accumulate 的返回类型」一节、[算法总览（下）](../iterators-algorithms/43-algorithm-overview-part2.md)「partial_sort」「nth_element」「C++20 投影」三节、[map 与 set 深入](../containers/06-map-set-deep-dive.md)「复杂度和迭代器失效」一节、[算法总览（上）](../iterators-algorithms/42-algorithm-overview-part1.md)「二分那一族」一节

**验证输出**：

```text
$ g++ -std=c++23 -O2 -Wall -Wextra pj_full.cpp -o pj_full && ./pj_full data.csv
解析 8 行, 坏行 1
平均分 = 87.94  中位数 = 90.5
前 3 名:
  1. dave      95.0 (2020)
  2. grace     91.0 (2020)
  3. alice     90.5 (2022)
按年份统计:
  2020: 2 人, 平均 93.00
  2021: 3 人, 平均 82.83
  2022: 3 人, 平均 89.67
查找 frank: 分数 88.0 (2022)
```

## 再进阶任务（L4）：把门装上 {#pj-gates}

**思路**：解析升级成 `expected<Row, error_code>`——坏行的**原因**也成了数据；自定义 `CsvErrc` 四步走让错误码带 `csvreport` category；`-Wconversion -Werror` 逼你显式化每个窄化转换；sanitizer 全绿收尾。

1. 四步搭 `CsvErrc` 体系（枚举 → `is_error_code_enum` 特化 → category 单例 → `make_error_code`），`parse_row` 返回 `std::expected<Row, std::error_code>`，失败路径各给各的错误码。→ 知识点：[error_code](../error-utils/66-error-code.md)「自定义 category：从零搭一套错误码体系」一节
2. `check_range` 作为链上第二步，`parse_row(line).and_then(check_range)` 串起来；`transform`/`and_then` 的语义分工回[第 64 篇](../error-utils/64-expected.md)核对。→ 知识点：[expected](../error-utils/64-expected.md)「C++23 的 monadic」一节
3. `-Wconversion -Werror` 下零警告：所有窄化处显式 `static_cast`（如 `static_cast<int>(e)`、`static_cast<double>(rows.size())`）。→ 知识点：教程约定「`-Wconversion` 逼你把隐式转换显式化」的工程实践（见[对象大小、对齐与平凡类型](../containers/12-object-size-and-trivial-types.md)实战原则一节）
4. sanitizer：`-fsanitize=address,undefined -fno-sanitize-recover=all` 构建，坏行数据集与正常数据集都零报告、退出码 0。→ 知识点：[容器选择指南](../containers/01-container-selection-guide.md)「迭代器失效速查」一节（迭代器失效审计：只 reserve + push_back、不持有迭代器）

**验证输出**：

```text
$ g++ -std=c++23 -O2 -Wall -Wextra -Wconversion -Werror pj_gates.cpp -o pj_gates
$ ./pj_gates data_bad.csv
第 3 行失败: 分数超出 [0,100] (cat=csvreport)
第 4 行失败: 年份超出 [2000,2100] (cat=csvreport)
第 6 行失败: 字段格式错误 (cat=csvreport)
有效 4 行, 平均分 = 86.50
$ g++ -std=c++23 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all pj_gates.cpp -o pj_gates_san
$ ./pj_gates_san data_bad.csv && ./pj_gates_san data.csv; echo "两次 sanitizer 运行退出码 = $?"
第 3 行失败: 分数超出 [0,100] (cat=csvreport)
第 4 行失败: 年份超出 [2000,2100] (cat=csvreport)
第 6 行失败: 字段格式错误 (cat=csvreport)
有效 4 行, 平均分 = 86.50
第 9 行失败: 字段格式错误 (cat=csvreport)
有效 8 行, 平均分 = 87.94
两次 sanitizer 运行退出码 = 0          ← 零报告
```

> 数据集构成（便于复现）：`data_bad.csv` 共 7 行——第 3 行分数越界、第 4 行年份越界、第 6 行字段格式错误，其余 4 行有效（平均 86.50，即 4 个分数之和 346.0）；`data.csv` 共 9 行、仅第 9 行字段格式错误（8 行有效，平均 87.94）。

## 终极挑战（L5）：内存受限的外部排序 {#pj-l5}

**思路**：内存一次只能装 5000 个数，整份 30000 个的 `std::sort` 就不成立了——数据装不进内存。外排序的正解是「分趟排序 + k 路归并」：每趟读 5000 个排好写一个有序 run，最后用一个**最小堆**在 6 个 run 的队首之间每次挑最小的，归并成完整有序文件。堆里始终只有 k 个元素，内存占用 O(k)，每次 pop/push 是 O(log k)，总体 O(N log k)。

1. 生成与分趟：`to_chars` 写、`from_chars` 读（ec + ptr 双检查），每趟 `std::sort` 排好落盘。→ 知识点：[random](../time-numeric/60-random.md)「正确写法」一节、[charconv](../strings/51-charconv.md)「to_chars / from_chars」两节、[算法总览（下）](../iterators-algorithms/43-algorithm-overview-part2.md)「sort：Introsort」一节
2. k 路归并：`priority_queue<Item, vector<Item>, greater<Item>>` 最小堆，`Item{value, run号}` 重载 `operator>`；每 pop 一次就从对应 run 补读一个。这正是[容器适配器](../containers/09-container-adapters.md)那篇「Top-K / 合并 k 个有序序列」的主力结构。→ 知识点：[容器适配器](../containers/09-container-adapters.md)「priority_queue」一节
3. 验证：读回输出 `std::is_sorted` 为 true（`is_sorted` 为教材外补充：非修改式算法、语义自明——判断区间是否已有序），`steady_clock` 计时。→ 知识点：[算法总览（上）](../iterators-algorithms/42-algorithm-overview-part1.md)「非修改式：只读，连一个元素都不改」一节、[chrono](../time-numeric/58-chrono.md)「测耗时为什么只能用 steady_clock」一节

**完整代码**：

```cpp
#include <algorithm>
#include <charconv>
#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <queue>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

constexpr std::size_t kChunk = 5000;   // 内存上限: 一次最多装 5000 个数

void generate(const char* path, std::size_t n)
{
    std::mt19937 rng(42);
    std::uniform_int_distribution<long long> dist(1, 1000000);
    std::ofstream out(path);
    char buf[32];
    for (std::size_t i = 0; i < n; ++i) {
        auto r = std::to_chars(buf, buf + sizeof(buf), dist(rng));
        out.write(buf, static_cast<std::streamsize>(r.ptr - buf));
        out.put('\n');
    }
}

bool read_number(std::ifstream& in, long long& out)
{
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        auto r = std::from_chars(line.data(), line.data() + line.size(), out);
        if (r.ec == std::errc{} && r.ptr == line.data() + line.size()) {
            return true;
        }
    }
    return false;
}

std::size_t make_runs(const char* input, std::string_view prefix)
{
    std::ifstream in(input);
    std::size_t run = 0;
    long long x = 0;
    for (;;) {
        std::vector<long long> buf;
        buf.reserve(kChunk);
        while (buf.size() < kChunk && read_number(in, x)) {
            buf.push_back(x);
        }
        if (buf.empty()) {
            break;
        }
        std::sort(buf.begin(), buf.end());
        std::ofstream out(std::format("{}_{}.run", prefix, run));
        char num[32];
        for (long long v : buf) {
            auto r = std::to_chars(num, num + sizeof(num), v);
            out.write(num, static_cast<std::streamsize>(r.ptr - num));
            out.put('\n');
        }
        ++run;
    }
    return run;
}

struct Item {
    long long value;
    std::size_t run;
    bool operator>(const Item& o) const
    {
        return value > o.value;   // std::greater -> 最小堆
    }
};

void merge_runs(std::size_t runs, std::string_view prefix, const char* output)
{
    std::vector<std::ifstream> in(runs);
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    for (std::size_t i = 0; i < runs; ++i) {
        in[i].open(std::format("{}_{}.run", prefix, i));
        long long x = 0;
        if (read_number(in[i], x)) {
            pq.push({x, i});
        }
    }
    std::ofstream out(output);
    char num[32];
    while (!pq.empty()) {
        Item top = pq.top();
        pq.pop();
        auto r = std::to_chars(num, num + sizeof(num), top.value);
        out.write(num, static_cast<std::streamsize>(r.ptr - num));
        out.put('\n');
        long long next = 0;
        if (read_number(in[top.run], next)) {
            pq.push({next, top.run});
        }
    }
}

int main()
{
    const char* input = "/tmp/cpp-v3-pj/input.txt";
    const char* output = "/tmp/cpp-v3-pj/sorted.txt";
    std::string prefix = "/tmp/cpp-v3-pj/run";
    constexpr std::size_t kN = 30000;

    auto t0 = std::chrono::steady_clock::now();
    generate(input, kN);
    auto runs = make_runs(input, prefix);
    merge_runs(runs, prefix, output);
    auto t1 = std::chrono::steady_clock::now();

    std::vector<long long> check;
    std::ifstream out_in(output);
    long long x = 0;
    while (read_number(out_in, x)) {
        check.push_back(x);
    }
    std::cout << std::format("N={}, 内存上限 M={}, run 数={}, 输出行数={}\n",
                             kN, kChunk, runs, check.size());
    std::cout << "std::is_sorted 验证: " << std::boolalpha
              << std::is_sorted(check.begin(), check.end()) << '\n';
    std::cout << "总耗时: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
              << " ms\n";
    return 0;
}
```

**验证输出**：

```text
$ g++ -std=c++23 -O2 -Wall -Wextra -Wconversion -Werror pj_l5.cpp -o pj_l5 && ./pj_l5
N=30000, 内存上限 M=5000, run 数=6, 输出行数=30000
std::is_sorted 验证: true
总耗时: 3 ms
```

30000 个数被切成 6 个 run（每个 5000），6 路归并出 30000 行，`is_sorted` 验证为 true。整条流水线里 `to_chars`/`from_chars` 负责文本 I/O、`sort` 负责趟内排序、`priority_queue` 负责归并、`steady_clock` 负责计时——本卷的家当一个没闲着。
