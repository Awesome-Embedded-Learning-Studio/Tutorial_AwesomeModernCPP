---
title: "文件与目录操作"
description: "exists、copy、move、remove、权限与空间查询"
chapter: 9
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
platform: host
cpp_standard: [17]
reading_time_minutes: 15
prerequisites:
  - "Chapter 9: path 操作"
related:
  - "目录遍历与搜索"
---

# 文件与目录操作

上一篇我们学会了用 `std::filesystem::path` 处理路径的语法问题——构造、分解、修改、比较，全是纯计算，不碰磁盘。这一篇我们开始动真格的：用 `<filesystem>` 库直接操作文件系统——检查文件是否存在、创建目录、复制文件、删除文件、查询权限和磁盘空间。

和上一篇一样，我们的环境是 C++17，GCC 13+ / Clang 15+ / MSVC 2022。头文件 `<filesystem>`，命名空间 `namespace fs = std::filesystem;`。

> **学习目标**
>
> - 完成本章后，你将能够：
> - [ ] 使用 `exists`、`is_regular_file`、`is_directory` 检查文件状态
> - [ ] 掌握 `create_directory`、`create_directories` 的使用
> - [ ] 安全地进行文件复制和删除操作
> - [ ] 理解 `permissions`、`space`、`last_write_time` 等元数据查询
> - [ ] 编写实用的日志轮转工具

## 文件状态查询：它存在吗？它是什么类型？

文件系统操作的第一步通常是"先看看这个路径上到底有什么"。`<filesystem>` 提供了一组查询函数来回答这个问题。

### exists：路径存在吗

`fs::exists(p)` 检查给定路径是否在文件系统上存在。它可以接受 `path` 对象，也可以接受 `directory_entry`（我们下一篇会讲）。返回 `bool`：

```cpp
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    fs::path p = "/usr/local/bin/gcc";
    if (fs::exists(p)) {
        std::cout << p << " 存在\n";
    } else {
        std::cout << p << " 不存在\n";
    }
    return 0;
}
```

⚠️ `exists()` 在某些情况下会抛出异常（比如权限不足导致无法访问父目录）。如果你不希望异常传播，使用不接受 `std::error_code` 的重载版本，或者用 try-catch 包裹。更好的做法是使用接受 `std::error_code` 的重载：

```cpp
std::error_code ec;
bool exists = fs::exists(p, ec);
if (ec) {
    std::cerr << "查询失败: " << ec.message() << "\n";
}
```

### is_regular_file / is_directory / is_symlink：类型判断

知道路径存在之后，下一步是判断它的类型。`fs::is_regular_file(p)` 判断是否是普通文件，`fs::is_directory(p)` 判断是否是目录，`fs::is_symlink(p)` 判断是否是符号链接。还有 `is_block_file`、`is_character_file`、`is_fifo`、`is_socket`、`is_other` 等更细分的类型判断，在 Linux 系统编程中偶尔会用到。

```cpp
fs::path p = "/usr/local/bin";

if (fs::is_directory(p)) {
    std::cout << p << " 是一个目录\n";
} else if (fs::is_regular_file(p)) {
    std::cout << p << " 是一个普通文件\n";
} else if (fs::is_symlink(p)) {
    std::cout << p << " 是一个符号链接\n";
}
```

⚠️ 如果路径不存在，这些函数返回 `false`——不会抛异常。所以你不需要先 `exists()` 再判断类型，直接判断就行。但要注意：如果 `status()` 调用本身失败（比如权限问题），会抛 `filesystem_error` 异常。

### file_size / last_write_time / status：元数据查询

除了类型，我们经常还需要查询文件的大小、最后修改时间和权限状态：

```cpp
#include <filesystem>
#include <iostream>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

void print_file_info(const fs::path& p) {
    std::error_code ec;

    // 文件大小（字节）
    auto size = fs::file_size(p, ec);
    if (!ec) {
        std::cout << "大小: " << size << " 字节\n";
        if (size > 1024 * 1024) {
            std::cout << "      "
                      << size / (1024.0 * 1024.0) << " MB\n";
        } else if (size > 1024) {
            std::cout << "      "
                      << size / 1024.0 << " KB\n";
        }
    }

    // 最后修改时间
    auto ftime = fs::last_write_time(p, ec);
    if (!ec) {
        // C++20 之前：需要转换成 time_t 来显示
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
        );
        auto time_t_val = std::chrono::system_clock::to_time_t(sctp);
        std::cout << "修改时间: "
                  << std::ctime(&time_t_val);
    }

    // 文件状态（权限等）
    auto status = fs::status(p, ec);
    if (!ec) {
        std::cout << "类型: " << static_cast<int>(status.type()) << "\n";
        std::cout << "权限: " << static_cast<unsigned>(status.permissions()) << "\n";
    }
}

int main() {
    print_file_info("/usr/local/bin/gcc");
    return 0;
}
```

⚠️ `last_write_time` 在 C++20 之前转换成可读格式有点繁琐（如上所示），因为 `file_time_type` 的时钟不一定是 `system_clock`。C++20 提供了更简洁的方式，通过 `std::chrono::clock_cast`，但 C++17 只能用上面的近似方法。在实际项目中，用 `std::ctime` 做简单显示够用了，只是精度可能不完全准确。

## 创建目录

`fs::create_directory(p)` 创建一个目录——前提是父目录必须已经存在。如果父目录不存在，调用会失败：

```cpp
fs::path dir = "/tmp/myapp_config";
if (!fs::exists(dir)) {
    if (fs::create_directory(dir)) {
        std::cout << "目录创建成功\n";
    } else {
        std::cerr << "目录创建失败\n";
    }
}
```

如果你需要创建一个多级目录（比如 `/tmp/a/b/c`，而 `/tmp/a` 和 `/tmp/a/b` 都不存在），用 `fs::create_directories(p)`。它会自动创建路径中所有缺失的中间目录，类似于 `mkdir -p`：

```cpp
fs::path deep_dir = "/tmp/myapp/data/cache/tmp";
fs::create_directories(deep_dir);  // 自动创建所有中间目录
std::cout << "创建完成\n";
```

`create_directories` 是笔者用得最多的文件系统操作之一。在程序启动时，确保配置目录、日志目录、缓存目录都存在，这是一个非常常见的需求。用 `create_directories` 一行代码就搞定了，不用手动检查每一级是否存在。

⚠️ `create_directory` 在目录已经存在时返回 `false`，但不会报错。`create_directories` 同理——如果所有目录都存在，它也返回 `false`。所以你不应该用返回值来判断"是否出错"，而应该用 `std::error_code` 版本。

## 复制文件和目录

`fs::copy(from, to)` 是一个多功能复制函数。它的行为取决于 `from` 的类型和是否指定了 `copy_options`：

```cpp
// 默认行为：
// - 如果 from 是普通文件，复制文件到 to
// - 如果 from 是目录，复制目录结构到 to（不递归复制内容）
// - 如果 from 是符号链接，复制链接本身

fs::path src = "/tmp/source.txt";
fs::path dst = "/tmp/dest.txt";

std::error_code ec;
fs::copy(src, dst, ec);
if (ec) {
    std::cerr << "复制失败: " << ec.message() << "\n";
}
```

### copy_options：控制复制行为

`copy_options` 是一个 bitmask 类型，用来精细控制复制行为。常用的选项包括：

`fs::copy_options::overwrite_existing`——如果目标文件已存在，覆盖它。默认情况下，如果目标已存在，`copy` 会失败（或跳过，取决于具体操作）。

`fs::copy_options::recursive`——递归复制目录内容。如果 from 是目录，会递归复制目录下所有文件和子目录。

`fs::copy_options::copy_symlinks`——复制符号链接本身（而不是跟随链接复制指向的文件）。

```cpp
// 递归复制整个目录
fs::copy("/tmp/source_dir", "/tmp/dest_dir",
         fs::copy_options::recursive |
         fs::copy_options::overwrite_existing);
```

`fs::copy_file(from, to, options)` 是专门用于文件复制的函数。它和 `copy` 的区别在于：`copy_file` 只处理普通文件，而且提供了更精细的控制。比如 `copy_file` 可以保证原子性——在复制完成之前，目标文件不会被部分覆盖。

```cpp
// 安全的文件复制（原子性保证）
fs::path src = "/data/important_config.yaml";
fs::path dst = "/backup/important_config.yaml";

std::error_code ec;
fs::copy_file(src, dst,
              fs::copy_options::overwrite_existing, ec);
if (ec) {
    std::cerr << "复制失败: " << ec.message() << "\n";
} else {
    std::cout << "复制成功\n";
}
```

## 删除和重命名

`fs::remove(p)` 删除一个文件或空目录。如果路径不存在，返回 `false`（不报错）。如果路径是符号链接，删除链接本身而不是目标。如果路径是非空目录，删除失败：

```cpp
fs::path temp = "/tmp/temp_file.txt";
bool removed = fs::remove(temp);
if (removed) {
    std::cout << "已删除\n";
} else {
    std::cout << "文件不存在或删除失败\n";
}
```

`fs::remove_all(p)` 递归删除一个目录及其所有内容（文件、子目录、符号链接）。返回删除的文件数量。这是"核弹级"操作——一定要确认路径正确再调用：

```cpp
fs::path temp_dir = "/tmp/my_temp_dir";
auto count = fs::remove_all(temp_dir);
std::cout << "删除了 " << count << " 个文件/目录\n";
```

⚠️ `remove_all` 是不可逆的操作。笔者有一次在调试时不小心把路径写错了（少了一层目录），差点把整个项目目录清空。幸好当时跑在测试环境里，没有造成实际损失。从那以后，笔者在调用 `remove_all` 之前一定会打印路径并确认。建议你也养成这个习惯。

`fs::rename(old_path, new_path)` 重命名或移动文件/目录。它在同一个文件系统上是原子操作（只修改目录项，不移动数据），跨文件系统时等同于复制+删除：

```cpp
std::error_code ec;
fs::rename("/tmp/old_name.txt", "/tmp/new_name.txt", ec);
if (ec) {
    std::cerr << "重命名失败: " << ec.message() << "\n";
}
```

## 权限与磁盘空间

### permissions：修改文件权限

`fs::permissions(p, prms)` 修改文件的权限位，类似于 `chmod`。权限用 `fs::perms` 枚举表示：

```cpp
fs::path script = "/tmp/my_script.sh";

// 设置为 rwxr-xr-x (755)
fs::permissions(script,
    fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec |
    fs::perms::group_read | fs::perms::group_exec |
    fs::perms::others_read | fs::perms::others_exec);

// 或者用 perm_options 控制修改方式
fs::permissions(script,
    fs::perms::owner_exec,     // 只修改 owner_exec 位
    fs::perm_options::add);    // 添加（不影响其他位）
```

第三个参数 `perm_options` 可以是 `replace`（替换所有权限，默认行为）、`add`（添加指定权限位）或 `remove`（移除指定权限位）。这在只需要修改一两个权限位时比替换全部权限更方便。

### space：查询磁盘空间

`fs::space(p)` 返回一个 `space_info` 结构体，包含磁盘的容量、已用空间和可用空间：

```cpp
auto info = fs::space("/tmp");
if (info.capacity > 0) {
    std::cout << "总容量:   "
              << info.capacity / (1024.0 * 1024 * 1024) << " GB\n";
    std::cout << "可用空间: "
              << info.available / (1024.0 * 1024 * 1024) << " GB\n";
    std::cout << "剩余空间: "
              << info.free / (1024.0 * 1024 * 1024) << " GB\n";
}
```

注意 `available` 和 `free` 的区别：`free` 是磁盘上的剩余空间（包括只有 root 能用的部分），`available` 是当前用户实际可用的空间。在 Linux 上，这两个值的差异来自 reserved blocks（ext4 默认保留 5% 给 root）。

## 临时文件处理

C++ 没有直接提供"创建临时文件"的标准 API（C++23 的 `std::filesystem::temp_directory_path()` 只是告诉你临时目录在哪里）。不过在 C++17 中，我们可以组合使用现有的工具来安全地处理临时文件：

```cpp
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;

/// @brief 创建一个唯一的临时文件路径
/// @return 临时文件的路径（文件尚未创建）
fs::path make_temp_file() {
    auto temp_dir = fs::temp_directory_path();

    // 生成随机后缀
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 999999);
    auto suffix = std::to_string(dist(gen));

    auto temp_path = temp_dir / ("myapp_temp_" + suffix + ".tmp");
    return temp_path;
}

/// @brief 安全地将数据写入临时文件，然后原子性地重命名为目标文件
/// @param target 目标文件路径
/// @param data 要写入的数据
/// @return 是否成功
bool safe_write_file(const fs::path& target, const std::string& data) {
    auto temp = make_temp_file();

    // 先写入临时文件
    {
        std::ofstream out(temp);
        if (!out) return false;
        out << data;
        out.close();
        if (out.fail()) {
            fs::remove(temp);
            return false;
        }
    }

    // 原子性重命名
    std::error_code ec;
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp);  // 清理临时文件
        return false;
    }
    return true;
}
```

这个"写临时文件 + 原子重命名"的模式在需要保证数据完整性的场景中非常重要——如果写入过程中程序崩溃或断电，目标文件要么是旧的完整版本，要么是新的完整版本，不会出现"写了一半"的损坏状态。很多数据库、配置文件管理器、包管理器都用这种模式。

## 实战：日志轮转工具

我们把本篇学到的所有操作组合起来，写一个实用的日志轮转（log rotation）工具。日志轮转的核心逻辑是：当日志文件超过一定大小时，把它重命名为备份文件（加序号），然后创建新的空日志文件。同时限制备份数量，超过限制的旧备份要删除。

```cpp
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <vector>
#include <string>

namespace fs = std::filesystem;

/// @brief 执行日志轮转
/// @param log_path 日志文件路径
/// @param max_size 最大文件大小（字节）
/// @param max_backups 最大备份数量
void rotate_log(const fs::path& log_path,
                std::uintmax_t max_size,
                int max_backups) {
    std::error_code ec;

    // 检查日志文件是否存在且超过大小限制
    if (!fs::exists(log_path, ec) || ec) return;
    auto size = fs::file_size(log_path, ec);
    if (ec || size < max_size) return;

    auto stem = log_path.stem().string();
    auto ext = log_path.extension().string();
    auto parent = log_path.parent_path();

    // 收集已有的备份文件
    std::vector<fs::path> backups;
    for (int i = 1; i <= max_backups + 1; ++i) {
        auto backup_name = stem + "." + std::to_string(i) + ext;
        auto backup_path = parent / backup_name;
        if (fs::exists(backup_path)) {
            backups.push_back(backup_path);
        }
    }

    // 删除超出数量限制的旧备份
    std::sort(backups.begin(), backups.end());
    while (static_cast<int>(backups.size()) >= max_backups) {
        fs::remove(backups.back(), ec);
        backups.pop_back();
    }

    // 将现有备份序号 +1
    for (int i = static_cast<int>(backups.size()); i >= 1; --i) {
        auto old_name = stem + "." + std::to_string(i) + ext;
        auto new_name = stem + "." + std::to_string(i + 1) + ext;
        fs::rename(parent / old_name, parent / new_name, ec);
    }

    // 将当前日志重命名为 .1 备份
    auto first_backup = parent / (stem + ".1" + ext);
    fs::rename(log_path, first_backup, ec);

    // 创建新的空日志文件
    std::ofstream(log_path).close();

    std::cout << "日志轮转完成: " << log_path << "\n";
}

int main() {
    // 示例：当 app.log 超过 1MB 时轮转，最多保留 5 个备份
    rotate_log("/tmp/app.log", 1024 * 1024, 5);
    return 0;
}
```

运行后，`/tmp/` 下的文件状态会变成这样：

```text
app.log         ← 新的空日志文件
app.1.log       ← 上一次的日志
app.2.log       ← 上上次的日志
...
app.5.log       ← 最老的备份
```

这个轮转工具使用了 `exists`、`file_size`、`rename`、`remove` 等本篇学到的所有核心操作。其中"原子重命名"保证了轮转过程中不会丢失日志数据——即使程序在重命名过程中崩溃，最多也就是某个备份文件没有完成重命名，下次轮转会自动处理。

## 错误处理的两种模式

贯穿这篇文，笔者一直在用两种方式处理错误：抛异常和 `std::error_code`。我们来总结一下 `<filesystem>` 中错误处理的最佳实践。

大多数 `fs::xxx()` 函数都有两个重载版本：一个在出错时抛 `fs::filesystem_error` 异常，另一个接受一个 `std::error_code&` 参数并在出错时通过它返回错误码。选择哪种取决于你的场景：

```cpp
// 模式一：抛异常（适合"不应该失败"的操作）
fs::create_directories("/tmp/myapp/data");

// 模式二：error_code（适合"可能失败"的操作）
std::error_code ec;
fs::copy(src, dst, ec);
if (ec) {
    // 处理错误
}
```

笔者个人的偏好是：对于程序启动时的初始化操作（创建配置目录等），用抛异常的版本——因为这些操作失败意味着程序无法正常运行，异常可以直接终止启动流程。对于运行时可能正常失败的操作（复制文件、删除临时文件等），用 `error_code` 版本——因为这些失败是可预期的，需要优雅地处理。

## 小结

这一篇我们覆盖了 `<filesystem>` 库的核心文件操作。文件状态查询（`exists`、`is_regular_file`、`is_directory`）和元数据查询（`file_size`、`last_write_time`、`status`）让我们能了解"文件系统上到底有什么"。`create_directory` 和 `create_directories` 负责创建目录，后者会自动创建中间目录，非常方便。`copy` / `copy_file` 提供灵活的文件复制，`remove` / `remove_all` 提供文件删除，`rename` 提供原子重命名。`permissions` 和 `space` 分别处理权限和磁盘空间查询。`temp_directory_path` 和"写临时文件 + 原子重命名"模式是保证数据完整性的关键技巧。

下一篇我们来聊聊目录遍历——`directory_iterator` 和 `recursive_directory_iterator`，以及如何高效地在文件系统中搜索文件。

## 参考资源

- [cppreference: std::filesystem](https://en.cppreference.com/w/cpp/filesystem)
- [cppreference: copy](https://en.cppreference.com/w/cpp/filesystem/copy)
- [cppreference: create_directory](https://en.cppreference.com/cpp/filesystem/create_directory)
- [cppreference: remove](https://en.cppreference.com/w/cpp/filesystem/remove)
- [cppreference: permissions](https://en.cppreference.com/w/cpp/filesystem/permissions)
- [C++ Stories: 22 Common Filesystem Tasks](https://www.cppstories.com/2024/common-filesystem-cpp20/)
