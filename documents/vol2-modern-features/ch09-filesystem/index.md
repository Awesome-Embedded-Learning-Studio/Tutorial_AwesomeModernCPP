---
title: "文件系统库"
description: "用 std::filesystem 实现跨平台的路径和文件操作"
---

# 文件系统库

在 std::filesystem 出现之前，C++ 标准库几乎没有文件系统操作的能力——你只能依赖 POSIX API 或平台特定的函数。C++17 的 filesystem 库终于填补了这个空白，提供了跨平台的路径处理、文件操作和目录遍历能力。这一章我们从路径操作开始，掌握文件和目录的增删改查，最后实现一个实用的目录遍历与搜索工具。

## 本章内容

- [path 操作：跨平台路径处理](01-filesystem-path/)
- [文件与目录操作](02-filesystem-ops/)
- [目录遍历与搜索](03-directory-iteration/)
