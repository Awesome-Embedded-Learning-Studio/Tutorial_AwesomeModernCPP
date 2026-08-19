---
title: "卷 2 Project 参考实现"
description: "logscan 日志巡检器的完整参考实现：分层任务逐步讲解（L1 热身骨架、scan、info/rotate、analyze 质量门、编译期哈希分派），每个文件逐段标注知识点链接，附真实运行输出（g++ 16.1.1 / WSL Arch，含 sanitizer 会话）。"
chapter: 2
order: 6
tags: [host, intermediate, cpp-modern, 移动语义, 类型安全, 工程实践]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 21
prerequisites:
  - "卷 2 Project：logscan 日志巡检器"
related:
  - "卷 2 全部章节（第 0~11 章）"
---

# 卷 2 Project 参考实现

> 全部输出在 WSL Arch（g++ 16.1.1，`-std=c++17`）真实运行得到。参考实现只是**一种**过关方式；你的实现不一样、验收标准对得上，就都是对的。工程分两个文件：`include/log_levels.hpp`（级别 + 编译期哈希）、`include/logscan.hpp`（行解析 + 自制 expected）、`src/main.cpp`（命令循环）。

## 核心任务（L2）：能跑起来的扫描器 {#pj-core}

**思路**：L1 热身只交接口——`Summary` + 四个命令声明 + 空实现，先让「骨架能编」立起来；核心任务把 `scan` 填实：递归遍历 + 扩展名过滤 + 排序输出。

**L1 热身骨架**（`pj_skel/logscan.hpp` + `pj_skel/main.cpp`）——空实现只求 `-c` 零警告。→ 知识点：[目录遍历与搜索](../ch09-filesystem/03-directory-iteration.md)、[文件与目录操作](../ch09-filesystem/02-filesystem-ops.md)（接口先行、逐步填实）

```cpp
// pj_skel/logscan.hpp
#pragma once
// L1 warmup skeleton: interface declarations only, no implementations.
#include <cstddef>
#include <cstdint>
#include <string>

struct Summary
{
    int info = 0;
    int warn = 0;
    int error = 0;
    std::size_t bad_lines = 0;
    std::string first_error;
};

int cmd_scan(const std::string& root);
int cmd_info(const std::string& path);
int cmd_rotate(const std::string& path, std::uintmax_t max_bytes, int max_backups);
int cmd_analyze(const std::string& path);
```

```cpp
// pj_skel/main.cpp
#include "logscan.hpp"

int cmd_scan(const std::string& /*root*/) { return 0; }
int cmd_info(const std::string& /*path*/) { return 0; }
int cmd_rotate(const std::string& /*path*/, std::uintmax_t /*max_bytes*/, int /*max_backups*/) { return 0; }
int cmd_analyze(const std::string& /*path*/) { return 0; }

int main()
{
    return 0;
}
```

```text
$ g++ -std=c++17 -Wall -Wextra -c -o pj_skel/main.o pj_skel/main.cpp
（零警告，exit 0）
```

**`cmd_scan`（src/main.cpp）**——`recursive_directory_iterator` + `skip_permission_denied`，`is_regular_file()` 与 `extension() == ".log"` 双重过滤，收集进 `vector` 排序输出；`file_size` 走 `error_code` 重载。→ 知识点：[目录遍历与搜索](../ch09-filesystem/03-directory-iteration.md)「recursive_directory_iterator」「遍历时过滤」两节、[path 操作：跨平台路径处理](../ch09-filesystem/01-filesystem-path.md)（`extension()`）

```cpp
int cmd_scan(const std::string& root_s)
{
    fs::path root(root_s);
    std::error_code ec;
    auto options = fs::directory_options::skip_permission_denied;
    std::vector<fs::path> logs;
    for (auto it = fs::recursive_directory_iterator(root, options, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec) {
            ec.clear();
            continue;
        }
        const auto& entry = *it;
        if (entry.is_regular_file() && entry.path().extension() == ".log") {
            logs.push_back(entry.path());
        }
    }
    std::sort(logs.begin(), logs.end());
    std::cout << "found " << logs.size() << " .log file(s) under " << root_s << "\n";
    for (const auto& p : logs) {
        std::error_code ec2;
        auto size = fs::file_size(p, ec2);
        std::cout << "  " << p.string() << "  (" << (ec2 ? 0 : size) << " bytes)\n";
    }
    return 0;
}
```

## 进阶任务（L3）：info 与 rotate {#pj-avg}

**思路**：`info` 三段元数据各有一个小坎——`file_size` 用 `error_code` 版、`last_write_time` 在 C++17 下要手动换算到 `system_clock` 再 `ctime`（C++20 才有 `clock_cast`）、行数用 `getline` 复用同一缓冲。`rotate` 的关键是**从高序号往低序号挪**，否则 `.1` 会覆盖 `.2` 需要的旧文件。

1. `cmd_info`：存在性检查 → 大小 → 时间换算 → 行数。→ 知识点：[文件与目录操作](../ch09-filesystem/02-filesystem-ops.md)「file_size / last_write_time / status：元数据查询」一节
2. `cmd_rotate`：阈值判断 + 从 `max_backups-1` 到 1 逐个 `rename` 后移 + 当前文件改名 `.1` + 重建空文件。→ 知识点：[文件与目录操作](../ch09-filesystem/02-filesystem-ops.md)「实战：日志轮转工具」一节（`rename` 同文件系统内是原子操作）

```cpp
int cmd_info(const std::string& path_s)
{
    fs::path p(path_s);
    std::error_code ec;
    if (!fs::is_regular_file(p, ec) || ec) {
        std::cout << "no such file: " << path_s << "\n";
        return 1;
    }
    auto size = fs::file_size(p, ec);
    std::cout << "file:  " << p.string() << "\n";
    std::cout << "bytes: " << (ec ? 0 : size) << "\n";
    auto ftime = fs::last_write_time(p, ec);
    if (!ec) {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        auto t = std::chrono::system_clock::to_time_t(sctp);
        std::cout << "mtime: " << std::ctime(&t);
    }
    std::ifstream in(path_s);
    std::string line;
    std::size_t lines = 0;
    while (std::getline(in, line)) {
        ++lines;
    }
    std::cout << "lines: " << lines << "\n";
    return 0;
}

int cmd_rotate(const std::string& path_s, std::uintmax_t max_bytes, int max_backups)
{
    fs::path log_path(path_s);
    std::error_code ec;
    if (!fs::exists(log_path, ec) || ec) {
        std::cout << "no such file: " << path_s << "\n";
        return 1;
    }
    auto size = fs::file_size(log_path, ec);
    if (ec || size < max_bytes) {
        std::cout << "below threshold, no rotation (" << (ec ? 0 : size)
                  << " < " << max_bytes << ")\n";
        return 0;
    }
    auto stem = log_path.stem().string();
    auto ext = log_path.extension().string();
    auto parent = log_path.parent_path();
    for (int i = max_backups - 1; i >= 1; --i) {
        fs::path from = parent / (stem + "." + std::to_string(i) + ext);
        fs::path to = parent / (stem + "." + std::to_string(i + 1) + ext);
        if (fs::exists(from)) {
            fs::rename(from, to, ec);
        }
    }
    fs::path first_backup = parent / (stem + ".1" + ext);
    fs::rename(log_path, first_backup, ec);
    std::ofstream(log_path).close();
    std::cout << "rotated: " << log_path.string() << " -> " << first_backup.filename().string() << "\n";
    return 0;
}
```

**验证输出**（演示树：`app.log`、`big.log`（2870 字节）、`sub/worker.log`、`tiny.log`）：

```text
$ ./logscan
logscan: scan <dir> | info <file> | rotate <file> <max_kb> <backups> | analyze <file> | quit
> info /tmp/logscan_demo/app.log
file:  /tmp/logscan_demo/app.log
bytes: 102
mtime: Sat Aug 15 14:08:15 2026
lines: 4
> rotate /tmp/logscan_demo/big.log 2 3
rotated: /tmp/logscan_demo/big.log -> big.1.log
> rotate /tmp/logscan_demo/app.log 999 3
below threshold, no rotation (102 < 1022976)
> scan /tmp/logscan_demo
found 5 .log file(s) under /tmp/logscan_demo
  /tmp/logscan_demo/app.log  (102 bytes)
  /tmp/logscan_demo/big.1.log  (2870 bytes)
  /tmp/logscan_demo/big.log  (0 bytes)
  /tmp/logscan_demo/sub/worker.log  (100 bytes)
  /tmp/logscan_demo/tiny.log  (13 bytes)
```

要点：轮转后 `big.log` 归零、2870 字节进了 `big.1.log`；`app.log` 低于阈值原样不动。`rotate <file> 2 3` 的阈值是 $2 \times 1024 = 2048$ 字节，`big.log` 超了才轮。

## 再进阶任务（L4）：把门装上 {#pj-gates}

**思路**：`analyze` 的行解析是纯函数：`analyze_line(line, line_no)` 返回 `expected<LineOutcome, LineError>`——坏行带行号穿透到命令层，好行给「级别 + 零拷贝消息视图」。`LineOutcome::message` 是 `string_view`，直接指向 `getline` 的缓冲，行处理完就失效，所以 `first_error` 只在记录时拷贝成 `std::string`。级别令牌与消息之间的空白用 `remove_prefix` 跳过。

1. `expected`（union 修复版）+ `LineError`/`LineOutcome`。→ 知识点：[std::expected\<T, E>：类型安全的错误传播](../ch10-error-handling/03-expected-error.md)「C++17 环境下的简化实现」一节
2. `analyze_line`：去 `\r` → 校验 `[` → 找 `]` → 切令牌 → 级别识别（L5 会换成哈希分派）→ 消息视图。→ 知识点：[string_view 陷阱与最佳实践](../ch08-string-view/03-string-view-pitfalls.md)（视图只做短期只读观察）
3. `cmd_analyze`：`getline` 循环 + `switch` 计数 + 坏行打印 + 第一条 ERROR 拷贝。→ 知识点：[enum class 与强类型枚举](../ch04-type-safety/01-enum-class.md)、[结构化绑定：一行解包多个值](../ch05-structured-bindings/01-structured-bindings.md)（`for (const auto& [ext, stat] : by_ext)` 已在 scan 用过）

```cpp
// include/logscan.hpp（核心片段）
struct LineError
{
    std::size_t line;
    std::string message;
};

struct LineOutcome
{
    LogLevel level;
    std::string_view message;   // zero-copy view into the caller's line
};

struct Summary
{
    int info = 0;
    int warn = 0;
    int error = 0;
    std::size_t bad_lines = 0;
    std::string first_error;
};

// ---- mini expected (C++17) ----
template <typename E>
struct unexpected
{
    E value;
    constexpr explicit unexpected(E v) : value(std::move(v)) {}
};

template <typename T, typename E>
class expected
{
    bool has_value_;
    union Storage
    {
        T val_;
        E err_;
        Storage() {}
        ~Storage() {}
    } storage_;

public:
    expected(const T& v) : has_value_(true) { new (&storage_.val_) T(v); }
    expected(T&& v) : has_value_(true) { new (&storage_.val_) T(std::move(v)); }
    expected(unexpected<E> u) : has_value_(false) { new (&storage_.err_) E(std::move(u.value)); }
    expected(const expected& other) : has_value_(other.has_value_)
    {
        if (has_value_) {
            new (&storage_.val_) T(other.storage_.val_);
        } else {
            new (&storage_.err_) E(other.storage_.err_);
        }
    }
    ~expected()
    {
        if (has_value_) {
            storage_.val_.~T();
        } else {
            storage_.err_.~E();
        }
    }

    constexpr bool has_value() const noexcept { return has_value_; }
    constexpr explicit operator bool() const noexcept { return has_value_; }
    T& operator*() { return storage_.val_; }
    T* operator->() { return &storage_.val_; }
    const E& error() const { return storage_.err_; }
};

/// Parse "[LEVEL] message"; the message is a view into `line` (zero copy).
inline expected<LineOutcome, LineError> analyze_line(std::string_view line, std::size_t line_no)
{
    while (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    if (line.empty()) {
        return unexpected<LineError>{LineError{line_no, "empty line"}};
    }
    if (line.front() != '[') {
        return unexpected<LineError>{LineError{line_no, "does not start with '['"}};
    }
    auto close = line.find(']');
    if (close == std::string_view::npos) {
        return unexpected<LineError>{LineError{line_no, "missing ']'"}};
    }
    std::string_view token = line.substr(1, close - 1);
    LogLevel level = level_from_token(token);
    if (level == LogLevel::kUnknown) {
        return unexpected<LineError>{LineError{line_no, "unknown level token"}};
    }
    std::string_view message = line.substr(close + 1);
    while (!message.empty() && message.front() == ' ') {
        message.remove_prefix(1);
    }
    return LineOutcome{level, message};
}
```

```cpp
// src/main.cpp（cmd_analyze）
int cmd_analyze(const std::string& path_s)
{
    std::ifstream file(path_s);
    if (!file) {
        std::cout << "cannot open: " << path_s << "\n";
        return 1;
    }

    // summary lives behind a unique_ptr; updated in place, never returned (ch00/ch01)
    std::unique_ptr<Summary> summary = std::make_unique<Summary>();
    std::string line;   // getline reuses this buffer; views only live per-line
    std::size_t line_no = 0;
    while (std::getline(file, line)) {
        ++line_no;
        auto outcome = analyze_line(line, line_no);
        if (!outcome) {
            ++summary->bad_lines;
            std::cout << "  bad line " << outcome.error().line << ": "
                      << outcome.error().message << " [" << line << "]\n";
            continue;
        }
        switch (outcome->level) {
        case LogLevel::kInfo:  ++summary->info; break;
        case LogLevel::kWarn:  ++summary->warn; break;
        case LogLevel::kError: ++summary->error; break;
        case LogLevel::kUnknown: break;
        }
        if (outcome->level == LogLevel::kError && summary->first_error.empty()) {
            summary->first_error = std::string(outcome->message);
        }
    }

    std::cout << "analyzed " << path_s << " (" << line_no << " lines)\n";
    std::cout << "  " << kLevelNames[static_cast<int>(LogLevel::kInfo)] << "  = " << summary->info << "\n";
    std::cout << "  " << kLevelNames[static_cast<int>(LogLevel::kWarn)] << "  = " << summary->warn << "\n";
    std::cout << "  " << kLevelNames[static_cast<int>(LogLevel::kError)] << "  = " << summary->error << "\n";
    std::cout << "  bad lines = " << summary->bad_lines << "\n";
    if (!summary->first_error.empty()) {
        std::cout << "  first ERROR message: " << summary->first_error << "\n";
    }
    return 0;
}
```

**验证输出**（`worker.log` 含一行 `[TRACE]` 坏行）：

```text
$ ./logscan
> analyze /tmp/logscan_demo/sub/worker.log
  bad line 3: unknown level token [[TRACE] this level does not exist]
analyzed /tmp/logscan_demo/sub/worker.log (4 lines)
  INFO  = 1
  WARN  = 1
  ERROR  = 1
  bad lines = 1
  first ERROR message: queue overflow
> analyze /tmp/logscan_demo/app.log
analyzed /tmp/logscan_demo/app.log (4 lines)
  INFO  = 2
  WARN  = 1
  ERROR  = 1
  bad lines = 0
  first ERROR message: connection refused on retry 1
```

**质量门**（`-Wall -Wextra` 零警告 + sanitizer 会话零报告）：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -Iinclude -o logscan src/main.cpp
（零警告，exit 0）

$ g++ -std=c++17 -Wall -Wextra -g -Iinclude -fsanitize=address,undefined -o logscan_asan src/main.cpp
$ ./logscan_asan
> scan /tmp/logscan_demo
found 5 .log file(s) under /tmp/logscan_demo
  ...（同前）
> analyze /tmp/logscan_demo/sub/worker.log
  bad line 3: unknown level token [[TRACE] this level does not exist]
analyzed /tmp/logscan_demo/sub/worker.log (4 lines)
  INFO  = 1
  WARN  = 1
  ERROR  = 1
  bad lines = 1
  first ERROR message: queue overflow
> quit
（sanitizer 零报告，exit 0）
```

要点：`[TRACE]` 不是认识的级别——错误按行号报、原文照贴；`first_error` 只在命中第一条 ERROR 时拷贝一次，其余消息全程零拷贝。

## 终极挑战（L5）：编译期级别分派 {#pj-l5}

**思路**：三件挑战分别对应本卷三章的组合拳——ch11 的 UDL + ch02 的编译期哈希做「字符串分派」、ch02 的编译期常量表做「枚举 ↔ 名字」互查、ch00/ch01 的移动语义搬结果。

1. `hash_string`/`operator""_hash` 都是 `constexpr`；`level_from_token` 用 `switch (hash_string(token))` + `case "INFO"_hash:` 这类编译期常量标签完成分派。→ 知识点：[用户自定义字面量基础](../ch11-user-defined-literals/01-udl-basics.md)「字符串字面量」一节、[if/switch 初始化器：缩小变量作用域](../ch05-structured-bindings/02-init-statements.md)「switch 初始化器」一节（哈希分派）、[编译期计算实战：从查表到编译期字符串](../ch02-constexpr/04-compile-time-practice.md)（FNV-1a）
2. `kLevelNames` 按枚举值索引 + `static_assert` 锁长度——新增枚举值忘了加名字，编译直接炸。→ 知识点：[编译期计算实战：从查表到编译期字符串](../ch02-constexpr/04-compile-time-practice.md)（编译期查表）
3. `Summary` 走 `make_unique` + 命令函数内就地更新——大结构体不拷贝、所有权单一。→ 知识点：[移动构造与移动赋值](../ch00-move-semantics/02-move-semantics.md)、[unique_ptr 详解：独占所有权的零开销智能指针](../ch01-smart-pointers/02-unique-ptr.md)

```cpp
// include/log_levels.hpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

enum class LogLevel : std::uint8_t
{
    kInfo = 0,
    kWarn = 1,
    kError = 2,
    kUnknown = 3,
};

// compile-time table indexed by LogLevel (kept in sync by static_assert)
constexpr const char* kLevelNames[] = {"INFO", "WARN", "ERROR", "UNKNOWN"};
static_assert(sizeof(kLevelNames) / sizeof(kLevelNames[0]) == 4,
              "kLevelNames must cover every LogLevel");

constexpr std::uint32_t hash_string(std::string_view s)
{
    std::uint32_t h = 2166136261u;
    for (char c : s) {
        h = (h ^ static_cast<std::uint8_t>(c)) * 16777619u;
    }
    return h;
}

constexpr std::uint32_t operator""_hash(const char* s, std::size_t n)
{
    std::uint32_t h = 2166136261u;
    for (std::size_t i = 0; i < n; ++i) {
        h = (h ^ static_cast<std::uint8_t>(s[i])) * 16777619u;
    }
    return h;
}

// runtime token -> level, dispatched through a hash switch whose case
// labels are compile-time constants (user-defined literal).
inline LogLevel level_from_token(std::string_view token)
{
    switch (hash_string(token)) {
    case "INFO"_hash:  return LogLevel::kInfo;
    case "WARN"_hash:  return LogLevel::kWarn;
    case "ERROR"_hash: return LogLevel::kError;
    default:           return LogLevel::kUnknown;
    }
}
```

**验证输出**（完整会话节选）：

```text
$ g++ -std=c++17 -Wall -Wextra -O2 -Iinclude -o logscan src/main.cpp && ./logscan
logscan: scan <dir> | info <file> | rotate <file> <max_kb> <backups> | analyze <file> | quit
> analyze /tmp/logscan_demo/sub/worker.log
  bad line 3: unknown level token [[TRACE] this level does not exist]
analyzed /tmp/logscan_demo/sub/worker.log (4 lines)
  INFO  = 1
  WARN  = 1
  ERROR  = 1
  bad lines = 1
  first ERROR message: queue overflow
> analyze /tmp/logscan_demo/app.log
analyzed /tmp/logscan_demo/app.log (4 lines)
  INFO  = 2
  WARN  = 1
  ERROR  = 1
  bad lines = 0
  first ERROR message: connection refused on retry 1
> quit

$ # sanitizer 构建同一会话:零报告(见 L4 质量门)
```

要点：`"INFO"_hash` 在编译期就算成整数常量当 case 标签，运行时只对 token 算一次哈希再 `switch`——比 `if/else` 字符串比较链零分配、好读；碰撞风险按教材的老规矩兜底：`switch` 命中后如果需要精确语义（比如两个 token 哈希相同但拼写不同），再拿原 `string_view` 比对一次。`kLevelNames` 那张表的 `static_assert` 让「加枚举值不更新表」变成编译错误而不是运行时踩空。`Summary` 经 `unique_ptr` 构造、命令函数内就地更新——三个统计值和一个字符串一路零拷贝到输出点。

到这里，「卷 2 的知识点是一体的」就有了实物：`filesystem` 是眼睛、`path` 是手、`string_view` 是零拷贝的传送带、`variant`/`enum class` 是分类器、`expected` 是错误管道、`unique_ptr` 和移动语义是搬运工——一台日志巡检器，没有一处需要你手动 `new`/`delete`，也没有一处是孤立存在的语法点。
