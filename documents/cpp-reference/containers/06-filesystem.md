---
chapter: 99
cpp_standard:
- 17
- 20
- 23
description: 跨平台的文件系统操作库：路径操作、目录遍历、文件状态查询
difficulty: beginner
order: 6
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::filesystem
---
<!--
参考卡模板 (Reference Card Template)
用于 documents/cpp-reference/ 下的特性速查页。
与 article-template.md 不同，参考卡走精炼的结构化格式，不需要叙事风格。

标签使用规则：
1. 必须包含 1 个 platform 标签（参考卡统一用 host）
2. 必须包含 1 个 difficulty 标签
3. 至少包含 1 个 topic 标签
4. 从 scripts/validate_frontmatter.py 的 VALID_TAGS 集合中选取
-->

# std::filesystem（C++17）

## 一句话

平台无关的文件系统操作库：路径拼接与规范化、目录创建与遍历、文件复制与删除、权限与状态查询——告别 `stat()` 和 `opendir()`。

## 头文件

`#include <filesystem>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 路径类 | `class path` | 路径的构造、拼接、分解（跨平台分隔符处理） |
| 路径拼接 | `path operator/(const path& lhs, const path& rhs)` | `p / "subdir" / "file.txt"` |
| 当前路径 | `path current_path()` | 获取/设置工作目录 |
| 目录迭代 | `class directory_iterator` | 遍历单层目录 |
| 递归迭代 | `class recursive_directory_iterator` | 递归遍历子目录 |
| 文件状态 | `bool exists(const path& p)` | 检查路径是否存在 |
| 文件大小 | `uintmax_t file_size(const path& p)` | 获取文件字节数 |
| 创建目录 | `bool create_directory(const path& p)` | 创建单个目录 |
| 创建多级目录 | `bool create_directories(const path& p)` | 递归创建整个路径 |
| 复制文件 | `bool copy_file(const path& from, const path& to)` | 复制单个文件 |
| 删除 | `bool remove(const path& p)` | 删除文件或空目录 |
| 递归删除 | `uintmax_t remove_all(const path& p)` | 递归删除目录及内容 |
| 重命名 | `void rename(const path& old, const path& newp)` | 重命名或移动 |

## 最小示例

```cpp
// Standard: C++17
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    fs::path p = fs::current_path() / "test.txt";
    std::cout << p << "\n";                      // 完整路径
    std::cout << p.filename() << "\n";           // test.txt
    std::cout << p.extension() << "\n";          // .txt

    fs::create_directories("a/b/c");             // 递归创建
    std::cout << fs::exists("a/b") << "\n";      // true
    fs::remove_all("a");                         // 递归删除
}
```

## 嵌入式适用性：低

- 依赖操作系统文件系统抽象层（POSIX 或 Win32），裸机环境无文件系统
- 适用于嵌入式 Linux（如 Buildroot/Yocto 平台）或上位机配置/日志工具
- 头文件引入的开销较大，资源极度受限的设备不建议使用
- 对需要文件系统的嵌入式场景（如 FAT32 on SD card），可选用轻量级替代（如 LittleFS）

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 8 | 7 | 19.12 |

## 另见

- [教程：std::filesystem](../../vol2-modern-features/ch09-filesystem/01-filesystem-path.md)
- [cppreference: std::filesystem](https://en.cppreference.com/w/cpp/filesystem)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
