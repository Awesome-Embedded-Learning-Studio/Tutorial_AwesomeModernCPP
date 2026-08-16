---
title: "卷 1 · 基础 Project：BookShelf 图书管理系统"
description: "卷 1 综合项目：做一个命令行图书管理系统 BookShelf——抽象类与多态、unique_ptr 容器、函数模板、map 统计、异常通道与 sanitizer 质量门，最后用 vector 当栈挑战 LeetCode 224 表达式求值（Hard 改编）。任务分四层，难度 L1~L5，参考实现分文件逐段讲解并附真实运行输出。"
chapter: 1
order: 5
tags: [host, beginner, cpp-modern, 实战]
difficulty: beginner
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 4
prerequisites: []
related: []
---

# 卷 1 · 基础 Project：BookShelf 图书管理系统

## 项目定位

把卷 1 的家当全部用进一个真实的小程序：`bookshelf`——一个命令行图书管理系统。抽象类 + 多态管「书」、`unique_ptr` 容器管「书架」、函数模板 + lambda 做统计、`std::map` 出类目报表、异常通道做错误处理，最后用「vector 当栈」挑战一道 LeetCode 困难题。任务分四层，一层一层往上盖；卡住了看[参考实现](./06-project-solutions.md)，它按层组织，可以只读你卡住的那一层。

工程结构：`book.hpp`（类的声明与实现——含模板，所以全部放头文件）+ `main.cpp`（命令循环与各命令实现）+ `Makefile`。所有代码遵循教程规范：snake_case 文件名、PascalCase 类名、4 空格缩进、函数大括号换行。

## 任务分层

### 核心任务（L2）：能跑起来的书架 {#pj-core}

**L1 热身**：先只搭骨架——`book.hpp` 里写出 `Book` 抽象基类的声明（成员、纯虚 `describe()`、虚析构）和两个空派生类 `PaperBook`、`Ebook` 的声明，`main.cpp` 里只写一个空的 `main`。不实现任何逻辑，只求 `g++ -std=c++17 -Wall -Wextra -c main.cpp` 零警告通过——头文件契约先立起来。

然后实现 `add`、`list`、`quit` 三个命令。数据结构：`Book` 抽象基类持有 `title_/author_/category_/stock_`（构造时负库存钳成 0），纯虚 `describe()`；`PaperBook` 加页数、`Ebook` 加文件大小（MB）；书架用 `std::vector<std::unique_ptr<Book>>`。命令循环：`std::getline` 读一行、`std::istringstream` 拆命令词、if-else 分派。`add` 的语法：`add <paper|ebook> <书名> <作者> <类目> <库存> [页数|大小MB]`，书名等不含空格。配一个 Makefile（变量 + 规则 + `clean`/`.PHONY`）。

**验收标准**：`make` 全绿；`add` 五本书（含一本电子书）后 `list` 输出带 `[纸质]/[电子]` 前缀的完整列表；`quit` 正常退出。贴出 `make` 和一次会话的完整输出。

[参考实现 →](./06-project-solutions.md#pj-core)

### 进阶任务（L3）：搜索与统计 {#pj-l3}

加两个命令。①`search <关键词>`：遍历书架，书名**或**作者里包含关键词（用 `std::string::find` 判 `npos`）的书都打印出来，一个都没匹配就提示。②`stats`：用 `std::map<std::string, int>` 按类目统计书目数（map 自动按字典序输出），再累加总库存；最后用**函数模板** `template <typename Predicate> int count_books_if(const Shelf&, Predicate)` 配合 lambda 谓词，统计类目为「文学」的书目数——这个模板就是你第 9 章学的函数模板在真实项目里的落点。

**验收标准**：贴出 `search 三体` 和 `stats` 的输出；说清为什么统计类目用 `std::map` 而不是 `std::unordered_map`（提示：要不要有序输出），以及 `count_books_if` 为什么比写死一个 `count_literature` 函数更好扩展。

[参考实现 →](./06-project-solutions.md#pj-l3)

### 再进阶任务（L4）：异常通道与质量门 {#pj-l4}

四件事。①补 `borrow <书名>` 和 `return <书名>` 命令：书不存在抛 `std::runtime_error("书架上没有《…》")`，库存为 0 时抛 `std::runtime_error("《…》库存不足")`，参数为空抛 `std::invalid_argument`。②命令循环里用**层次化 catch**（`invalid_argument` → `runtime_error` → `exception`）把异常统一兜成 `[参数错误]`/`[运行错误]` 的友好提示，程序永远不崩。③健壮输入：空行跳过、未知命令给出可用命令清单。④质量门：整个工程用 `g++ -std=c++17 -Wall -Wextra -Werror` 做到**零警告**；再用 `-fsanitize=address,undefined` 构建，跑一遍包含各种非法输入的完整会话，**必须零报告、退出码 0**。

**验收标准**：贴出「借空库存的书」「借不存在的书」「add 缺参数」三个健壮性测试的输出、`-Werror` 零警告的编译命令、以及 sanitizer 会话零报告的运行结果。

[参考实现 →](./06-project-solutions.md#pj-l4)

### 终极挑战（L5）：表达式求值器 {#pj-l5}

给 BookShelf 加一个 `calc <表达式>` 命令：求值一个只含 `+`、`-`、括号和空格的整数算术表达式。本题改编自 **LeetCode #224 Basic Calculator（Hard）**，入门卷 L5 口径＝「用本卷知识可解的最难问题」，档位口径见[练习总览](./index.md)。解法思路（符号栈）：遍历字符串，`(` 时把「当前符号 × 外层符号」压栈、`)` 时弹栈，遇到数字就把它乘上「栈顶符号 × 当前符号」累加进结果——**用 `std::vector<int>` 当栈**（本卷没讲 `std::stack`，vector 的 `push_back`/`back`/`pop_back` 就是栈三件套，属教材外补充）。要求：

1. 非法字符（比如 `*`）抛 `std::invalid_argument`，被命令循环兜住；
2. 用 $(1+(4+5+2)-3)+(6+8)$（= 23）、$(0-5)+8$（= 3）、$21-10+3$（= 14）验证；
3. 全套在 sanitizer 构建下零报告。

**验收标准**：贴出四个 `calc` 输出（三个正确值 + 一个 `*` 的报错）；一句话说清符号栈的关键不变量——压栈时为什么要乘上「外层符号」。

[参考实现 →](./06-project-solutions.md#pj-l5)

## 提交物清单

项目目录（`book.hpp`、`main.cpp`、`Makefile`）+ 各层终端记录（`session.log`、`sanitize.log`）+ 200 字以内的小结：说说这个项目里哪一处让你对「卷 1 的知识点是一体的」体会最深。
