---
title: "卷 1 · 基础 Project 参考实现"
description: "卷 1 综合项目（BookShelf 图书管理系统）的完整参考实现：book.hpp 与 main.cpp 分文件逐段讲解，每步标注知识点链接，含 Makefile、多态书架、map 统计、异常通道、sanitizer 质量门与 LeetCode 224 表达式求值（Hard 改编）的真实运行输出。"
chapter: 1
order: 6
tags: [host, beginner, cpp-modern, 实战]
difficulty: beginner
platform: host
cpp_standard: [11, 14, 17]
reading_time_minutes: 23
prerequisites: []
related: []
---

# 卷 1 · 基础 Project 参考实现

> 全部输出在 WSL Arch（g++ 16.1.1）下真实运行得到，编译命令带 `-Werror`，会话与 sanitizer 各跑一遍。参考实现只是**一种**过关方式；你的实现不一样、验收标准对得上，就都是对的。

## 核心任务（L2）：能跑起来的书架 {#pj-core}

**思路**：类放头文件（模板也在头文件，所以这里全部内联），命令循环放 `main.cpp`；`add` 用 `istringstream` 解析，`list` 走多态 `describe()`。先立头文件契约，再填实现。

**`book.hpp` 前半——抽象基类与两个派生类**。`Book` 用纯虚 `describe()` 立契约，虚析构保多态释放；构造时把负库存钳成 0，守住「库存非负」这条不变量。→ 知识点：[抽象类与接口](../ch08/03-abstract-classes.md)「纯虚函数与抽象类的诞生」一节、[构造函数](../ch06/02-constructors.md)「参数化构造」一节

```cpp
#pragma once

#include <memory>
#include <sstream>
#include <string>
#include <vector>

// 抽象基类:定义「一本书」的统一接口
class Book {
protected:
    std::string title_;
    std::string author_;
    std::string category_;
    int stock_;

public:
    Book(const std::string& title, const std::string& author,
         const std::string& category, int stock)
        : title_(title), author_(author), category_(category), stock_(stock)
    {
        if (stock_ < 0) {
            stock_ = 0;
        }
    }

    virtual ~Book() = default;
    virtual std::string describe() const = 0;

    bool borrow_one()
    {
        if (stock_ <= 0) {
            return false;
        }
        --stock_;
        return true;
    }

    void return_one() { ++stock_; }

    int stock() const { return stock_; }
    const std::string& title() const { return title_; }
    const std::string& author() const { return author_; }
    const std::string& category() const { return category_; }
};
```

`PaperBook` 和 `Ebook` 各自补上特有字段并重写 `describe()`。`Ebook` 里那个不起眼的 `format_mb` 值得注意：`std::to_string(4.5)` 会给出 `"4.500000"`，用 `ostringstream` 默认精度打印才是 `"4.5"`——格式化细节就藏在这种小函数里。→ 知识点：[std::string](../ch05/03-std-string.md)「数值转换」一节（`to_string` 对浮点的 `%f` 行为）

```cpp
class PaperBook : public Book {
private:
    int pages_;

public:
    PaperBook(const std::string& title, const std::string& author,
              const std::string& category, int stock, int pages)
        : Book(title, author, category, stock), pages_(pages)
    {
    }

    std::string describe() const override
    {
        return "[纸质] 《" + title_ + "》 " + author_ + " (" + std::to_string(pages_)
               + " 页) 库存 " + std::to_string(stock_);
    }
};

class Ebook : public Book {
private:
    double size_mb_;

    static std::string format_mb(double v)
    {
        std::ostringstream oss;
        oss << v;
        return oss.str();
    }

public:
    Ebook(const std::string& title, const std::string& author,
          const std::string& category, int stock, double size_mb)
        : Book(title, author, category, stock), size_mb_(size_mb)
    {
    }

    std::string describe() const override
    {
        return "[电子] 《" + title_ + "》 " + author_ + " (" + format_mb(size_mb_)
               + " MB) 库存 " + std::to_string(stock_);
    }
};
```

**`book.hpp` 后半——函数模板**。模板必须放头文件，这是 C++ 编译模型决定的：编译器实例化时要看到完整定义。谓词类型 `Predicate` 由调用点推导，lambda、函数对象、函数指针都能塞进来。→ 知识点：[类模板](../ch09/02-class-templates.md)「踩坑预警——模板声明和实现必须放在头文件中」一节、[函数模板](../ch09/01-function-templates.md)「模板实例化」一节

```cpp
// 函数模板:对书架按谓词计数,谓词可以是 lambda 或函数对象
template <typename Predicate>
int count_books_if(const std::vector<std::unique_ptr<Book>>& shelf, Predicate pred)
{
    int count = 0;
    for (const auto& b : shelf) {
        if (pred(*b)) {
            ++count;
        }
    }
    return count;
}
```

**`main.cpp`——命令循环与 `add`/`list`**。命令循环是「读一行 → 拆命令词 → 分派」的标准三件套；`args` 用 `line.substr(cmd.size())` 拿命令之后的整段尾巴再 trim，这样书名里可以有空格（本项目解析器按空白分词，中文书名正好没空格）。`add` 里 `if (!(iss >> kind >> title >> ...))` 是流状态检查：任何一项读失败整个条件为真，直接抛用法错误。→ 知识点：[异常基础](../ch10/01-try-catch.md)（`throw` 与流状态）、[std::string](../ch05/03-std-string.md)「行输入」一节

```cpp
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "book.hpp"

using Shelf = std::vector<std::unique_ptr<Book>>;

// ---- 工具:去掉首尾空格 ----
std::string trim(const std::string& s)
{
    std::size_t b = s.find_first_not_of(' ');
    if (b == std::string::npos) {
        return std::string();
    }
    std::size_t e = s.find_last_not_of(' ');
    return s.substr(b, e - b + 1);
}

void do_add(Shelf& shelf, const std::string& args)
{
    std::istringstream iss(args);
    std::string kind, title, author, category;
    int stock = 0;
    if (!(iss >> kind >> title >> author >> category >> stock)) {
        throw std::invalid_argument("用法: add <paper|ebook> <书名> <作者> <类目> <库存> [页数|大小MB]");
    }
    if (title.empty() || author.empty() || category.empty()) {
        throw std::invalid_argument("书名/作者/类目不能为空");
    }
    if (stock < 0) {
        throw std::invalid_argument("库存不能为负");
    }
    if (kind == "paper") {
        int pages = 0;
        if (!(iss >> pages) || pages <= 0) {
            throw std::invalid_argument("纸质书需要合法的页数");
        }
        shelf.push_back(std::make_unique<PaperBook>(title, author, category, stock, pages));
    } else if (kind == "ebook") {
        double mb = 0.0;
        if (!(iss >> mb) || mb < 0.0) {
            throw std::invalid_argument("电子书需要合法的大小");
        }
        shelf.push_back(std::make_unique<Ebook>(title, author, category, stock, mb));
    } else {
        throw std::invalid_argument("kind 必须是 paper 或 ebook");
    }
    std::cout << "已添加: " << shelf.back()->title() << std::endl;
}

void do_list(const Shelf& shelf)
{
    if (shelf.empty()) {
        std::cout << "(书架是空的)" << std::endl;
        return;
    }
    for (std::size_t i = 0; i < shelf.size(); ++i) {
        std::cout << "  [" << i << "] " << shelf[i]->describe() << std::endl;
    }
}
```

**`main` 的命令循环**。catch 顺序是「具体在前、一般在后」；`std::getline` 返回流引用，读到 EOF（管道输入结束）自动退出循环。→ 知识点：[异常基础](../ch10/01-try-catch.md)「标准异常层次——exception 家族」一节

```cpp
int main()
{
    Shelf shelf;
    std::string line;

    std::cout << "BookShelf v1.0 (输入 quit 退出)" << std::endl;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        if (!(iss >> cmd)) {
            continue;   // 空行跳过
        }
        std::string args = trim(line.substr(cmd.size()));

        if (cmd == "quit") {
            break;
        }
        try {
            if (cmd == "add") {
                do_add(shelf, args);
            } else if (cmd == "list") {
                do_list(shelf);
            } else if (cmd == "search") {
                do_search(shelf, args);
            } else if (cmd == "stats") {
                do_stats(shelf);
            } else if (cmd == "borrow") {
                do_borrow(shelf, args);
            } else if (cmd == "return") {
                do_return(shelf, args);
            } else if (cmd == "calc") {
                do_calc(args);
            } else {
                std::cout << "未知命令: " << cmd
                          << " (可用: add list search stats borrow return calc quit)" << std::endl;
            }
        } catch (const std::invalid_argument& e) {
            std::cout << "[参数错误] " << e.what() << std::endl;
        } catch (const std::runtime_error& e) {
            std::cout << "[运行错误] " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cout << "[错误] " << e.what() << std::endl;
        }
    }
    std::cout << "再见" << std::endl;
    return 0;
}
```

**`Makefile`**——变量 + 规则 + `.PHONY`，`sanitize` 目标单独产出带 ASan/UBSan 的构建。→ 知识点：[Linux 环境搭建](../ch00/01-setup-linux.md)「第四步——跑通第一个 CMake 项目」一节（CMake 生成的 Makefile 与这里手写的 Makefile 同属构建脚本）

```makefile
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -I.
TARGET = bookshelf

$(TARGET): main.cpp book.hpp
    $(CXX) $(CXXFLAGS) -o $(TARGET) main.cpp

sanitize:
    $(CXX) $(CXXFLAGS) -O0 -g -fsanitize=address,undefined -o $(TARGET)_asan main.cpp

clean:
    rm -f $(TARGET) $(TARGET)_asan

.PHONY: sanitize clean
```

**验证输出**（核心层会话，命令从 `session.txt` 喂入）：

```text
$ make
g++ -std=c++17 -Wall -Wextra -Werror -I. -o bookshelf main.cpp
$ make
make: 'bookshelf' is up to date.
$ ./bookshelf < session.txt
BookShelf v1.0 (输入 quit 退出)
已添加: 红楼梦
已添加: 三体
已添加: C++并发编程
已添加: 活着
已添加: 围城
  [0] [纸质] 《红楼梦》 曹雪芹 (573 页) 库存 3
  [1] [纸质] 《三体》 刘慈欣 (302 页) 库存 5
  [2] [电子] 《C++并发编程》 Anthony (4.5 MB) 库存 2
  [3] [纸质] 《活着》 余华 (191 页) 库存 0
  [4] [纸质] 《围城》 钱钟书 (359 页) 库存 2
再见
```

## 进阶任务（L3）：搜索与统计 {#pj-l3}

**思路**：搜索是「`find` 判 `npos`」的线性扫；统计用 map 吃「有序输出」的免费午餐，用函数模板吃「统计条件可扩展」的免费午餐。

`do_search`：书名或作者任一命中即打印。→ 知识点：[std::string](../ch05/03-std-string.md)「查找与子串」一节

```cpp
void do_search(const Shelf& shelf, const std::string& keyword)
{
    std::size_t found = 0;
    for (const auto& b : shelf) {
        if (b->title().find(keyword) != std::string::npos
            || b->author().find(keyword) != std::string::npos) {
            std::cout << "  " << b->describe() << std::endl;
            ++found;
        }
    }
    if (found == 0) {
        std::cout << "没有与 \"" << keyword << "\" 匹配的书" << std::endl;
    }
}
```

`do_stats`：map 的 `++by_category[b->category()]` 用 `operator[]` 自动插入——这里「访问时自动创建」恰是想要的语义；统计类目为什么不用 `unordered_map`？因为报表要按字典序输出，map 的红黑树天然有序。→ 知识点：[关联容器快速上手](../ch11/02-map-set.md)「上号——std::map 基本操作」一节

```cpp
void do_stats(const Shelf& shelf)
{
    std::map<std::string, int> by_category;   // 类目 -> 书目数
    int total_stock = 0;
    for (const auto& b : shelf) {
        ++by_category[b->category()];
        total_stock += b->stock();
    }
    std::cout << "--- 类目统计(map 按字典序) ---" << std::endl;
    for (const auto& entry : by_category) {
        std::cout << "  " << entry.first << ": " << entry.second << " 种" << std::endl;
    }
    std::cout << "总库存: " << total_stock << " 册" << std::endl;

    // 函数模板 + lambda 谓词
    int literature = count_books_if(shelf,
                                    [](const Book& b) { return b.category() == "文学"; });
    std::cout << "文学类书目数(count_books_if 模板): " << literature << std::endl;
}
```

**验证输出**：

```text
$ ./bookshelf < session.txt
...
search 三体
  [纸质] 《三体》 刘慈欣 (302 页) 库存 5
--- 类目统计(map 按字典序) ---
  技术: 1 种
  文学: 3 种
  科幻: 1 种
总库存: 12 册
文学类书目数(count_books_if 模板): 3
```

（此处只截取 `search`/`stats` 相关行，完整会话见文末。）

## 再进阶任务（L4）：异常通道与质量门 {#pj-l4}

**思路**：业务错误分类——参数形状错是 `invalid_argument`，业务状态错（没书/没库存）是 `runtime_error`；命令循环统一兜住，程序永不崩。

`do_borrow` / `do_return`：先找书，找不到抛 `runtime_error`；找到了再走 `borrow_one()`，失败就是「库存不足」。→ 知识点：[异常基础](../ch10/01-try-catch.md)「标准异常层次」一节、[STL 常用模式](../ch11/04-stl-patterns.md)（算法与容器协作）

```cpp
void do_borrow(Shelf& shelf, const std::string& title)
{
    if (title.empty()) {
        throw std::invalid_argument("用法: borrow <书名>");
    }
    for (auto& b : shelf) {
        if (b->title() == title) {
            if (!b->borrow_one()) {
                throw std::runtime_error("《" + title + "》库存不足");
            }
            std::cout << "已借出《" << title << "》,剩余库存 " << b->stock() << std::endl;
            return;
        }
    }
    throw std::runtime_error("书架上没有《" + title + "》");
}

void do_return(Shelf& shelf, const std::string& title)
{
    if (title.empty()) {
        throw std::invalid_argument("用法: return <书名>");
    }
    for (auto& b : shelf) {
        if (b->title() == title) {
            b->return_one();
            std::cout << "已归还《" << title << "》,当前库存 " << b->stock() << std::endl;
            return;
        }
    }
    throw std::runtime_error("书架上没有《" + title + "》");
}
```

**验证输出**（三个健壮性测试 + sanitizer）：

```text
$ g++ -std=c++17 -Wall -Wextra -Werror main.cpp -o bookshelf        # 零警告,编译通过
$ ./bookshelf < session.txt
...
已借出《三体》,剩余库存 4
[运行错误] 《活着》库存不足
已借出《红楼梦》,剩余库存 2
已借出《红楼梦》,剩余库存 1
已归还《红楼梦》,当前库存 2
...
[参数错误] 用法: add <paper|ebook> <书名> <作者> <类目> <库存> [页数|大小MB]
未知命令: foo (可用: add list search stats borrow return calc quit)
再见

$ g++ -std=c++17 -Wall -Wextra -Werror -O0 -g -fsanitize=address,undefined main.cpp -o bookshelf_asan
$ ASAN_OPTIONS=detect_leaks=1 ./bookshelf_asan < session.txt
BookShelf v1.0 (输入 quit 退出)
已添加: 红楼梦
已添加: 三体
已添加: C++并发编程
...
未知命令: foo (可用: add list search stats borrow return calc quit)
再见
(全程无任何 sanitizer 报告,退出码 0)
```

`borrow 活着` 撞上库存 0 → `[运行错误]`；`add paper 缺参数示例` 缺字段 → `[参数错误]`；`foo` → 未知命令提示。三种错误走三条通道，程序一次都没崩。

## 终极挑战（L5）：表达式求值器 {#pj-l5}

**思路**：只处理 `+ - ( )` 的表达式有一个漂亮的性质——每个数对结果的贡献只取决于「它前面的符号」和「它所在括号层的符号」。用一个 `vector<int>` 当符号栈：`(` 压入 `栈顶 × 当前符号`（外层符号被继承进来），`)` 弹出，数字出现时累加 `栈顶 × 当前符号 × 数字`。改编自 LeetCode #224（Hard）。

`do_calc` 与求值器。→ 知识点：[std::vector 快速上手](../ch11/01-vector.md)（vector 的 `push_back`/`back`/`pop_back` 就是栈三件套）、[循环语句](../ch02/02-loops.md)（单遍扫描）、[异常基础](../ch10/01-try-catch.md)（非法字符抛异常）

```cpp
void do_calc(const std::string& args)
{
    if (args.empty()) {
        throw std::invalid_argument("用法: calc <表达式>,如 calc (1+(4+5+2)-3)+(6+8)");
    }
    std::cout << args << " = " << evaluate_expression(args) << std::endl;
}

// ---- L5:表达式求值(LeetCode 224 Basic Calculator 改编) ----
// 支持 +、-、括号与空格;vector 当栈用(教材外补充:本卷未讲 std::stack)
int evaluate_expression(const std::string& expr)
{
    std::vector<int> signs;   // 每层括号的符号栈
    signs.push_back(1);
    int result = 0;
    int sign = 1;
    std::size_t i = 0;

    while (i < expr.size()) {
        char c = expr[i];
        if (c == ' ') {
            ++i;
        } else if (c == '+' || c == '-') {
            sign = (c == '-') ? -1 : 1;
            ++i;
        } else if (c == '(') {
            signs.push_back(signs.back() * sign);
            sign = 1;
            ++i;
        } else if (c == ')') {
            signs.pop_back();
            ++i;
        } else if (c >= '0' && c <= '9') {
            long value = 0;
            while (i < expr.size() && expr[i] >= '0' && expr[i] <= '9') {
                value = value * 10 + (expr[i] - '0');
                ++i;
            }
            result += signs.back() * sign * static_cast<int>(value);
        } else {
            throw std::invalid_argument(std::string("非法字符: ") + c);
        }
    }
    return result;
}
```

关键不变量一句话：`(` 压栈时用 `signs.back() * sign`——把「当前符号」与「外层符号」合并成一个系数存进栈里，于是任何数字出现时只需要乘上栈顶这一个系数，括号深度任意嵌套都成立。数字用 `long` 累积再截断成 `int`，中间值不溢出。

**验证输出**：

```text
$ ./bookshelf < session.txt
...
(1+(4+5+2)-3)+(6+8) = 23
2-1 + 2 = 3
(0-5)+8 = 3
21-10+3 = 14
1+2*3 = [参数错误] 非法字符: *
再见

$ ASAN_OPTIONS=detect_leaks=1 ./bookshelf_asan < session.txt
(输出一致,无 sanitizer 报告;退出码 0)
```

四个用例：括号嵌套的官方样例 23、带空格的 3、负括号的 3、连续加减的 14，全部正确；`*` 触发 `[参数错误]` 被兜住——224 的口径只支持 `+ - ( )`，乘除留给卷外的下一道题。

## 完整会话记录

`session.txt` 内容与普通构建的完整输出（sanitizer 构建输出与之一致）：

```text
add paper 红楼梦 曹雪芹 文学 3 573
add paper 三体 刘慈欣 科幻 5 302
add ebook C++并发编程 Anthony 技术 2 4.5
add paper 活着 余华 文学 0 191
add paper 围城 钱钟书 文学 2 359
list
stats
search 三体
borrow 三体
borrow 活着
borrow 红楼梦
borrow 红楼梦
return 红楼梦
list
calc (1+(4+5+2)-3)+(6+8)
calc  2-1 + 2
calc (0-5)+8
calc 21-10+3
calc 1+2*3
add paper 缺参数示例
foo
quit
```

```text
$ ./bookshelf < session.txt
BookShelf v1.0 (输入 quit 退出)
已添加: 红楼梦
已添加: 三体
已添加: C++并发编程
已添加: 活着
已添加: 围城
  [0] [纸质] 《红楼梦》 曹雪芹 (573 页) 库存 3
  [1] [纸质] 《三体》 刘慈欣 (302 页) 库存 5
  [2] [电子] 《C++并发编程》 Anthony (4.5 MB) 库存 2
  [3] [纸质] 《活着》 余华 (191 页) 库存 0
  [4] [纸质] 《围城》 钱钟书 (359 页) 库存 2
--- 类目统计(map 按字典序) ---
  技术: 1 种
  文学: 3 种
  科幻: 1 种
总库存: 12 册
文学类书目数(count_books_if 模板): 3
  [纸质] 《三体》 刘慈欣 (302 页) 库存 5
已借出《三体》,剩余库存 4
[运行错误] 《活着》库存不足
已借出《红楼梦》,剩余库存 2
已借出《红楼梦》,剩余库存 1
已归还《红楼梦》,当前库存 2
  [0] [纸质] 《红楼梦》 曹雪芹 (573 页) 库存 2
  [1] [纸质] 《三体》 刘慈欣 (302 页) 库存 4
  [2] [电子] 《C++并发编程》 Anthony (4.5 MB) 库存 2
  [3] [纸质] 《活着》 余华 (191 页) 库存 0
  [4] [纸质] 《围城》 钱钟书 (359 页) 库存 2
(1+(4+5+2)-3)+(6+8) = 23
2-1 + 2 = 3
(0-5)+8 = 3
21-10+3 = 14
1+2*3 = [参数错误] 非法字符: *
[参数错误] 用法: add <paper|ebook> <书名> <作者> <类目> <库存> [页数|大小MB]
未知命令: foo (可用: add list search stats borrow return calc quit)
再见
```

## 收尾

四层盖完，BookShelf 把卷 1 的类、继承、模板、容器、算法、异常串成了一栋能住的房子——最妙的时刻在最后：`calc` 那道 Hard 题证明，双指针、循环和 vector 这些「基础」拼起来，就能正面刚 LeetCode。这就是卷 1 给你的家底。卷 2 见。
