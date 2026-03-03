# std::string_view 全攻略

笔者最近常常跟字符串打交道，这篇博客也是跟先前的C++工程实践一起联动的——也就是解决IniParser的问题

> 传送门
>
> - CSDN：[现代C++工程实践：简单的IniParser2：分解需求与编写split-CSDN博客](https://blog.csdn.net/charlie114514191/article/details/155731063)
> - 知乎：[现代C++工程实践：简单的IniParser2：分解需求与编写split - 老老老陈醋的文章 - 知乎](https://zhuanlan.zhihu.com/p/1981659758034437080)
> - Github: [Awesome-Embedded-Learning-Studio/Tutorial_cpp_SimpleIniParser: 这是我们C++工程化开始的旅程！手搓一个最简单的Ini分析器！This is the beginning of our journey in C++ engineering! Handcrafting the simplest INI parser!](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_cpp_SimpleIniParser)

## 先别急，先问是什么

#### `std::string_view` 是什么？为什么要它？

`std::string_view`（C++17）是一个轻量、不可变的"字符串视图"类型：**它不拥有字符缓冲区，只保存指向字符序列起始处的指针和长度（size）**（PS：所以你看，是不是很"视图"），用于以 O(1) 的代价表示子串、字面量或其他字符序列的只读窗口。它的设计是为了解决频繁读操作时不必要的内存拷贝问题，从而提高性能与通用性。

> Sir This way: [C++参考文献](https://en.cppreference.com/w/cpp/string/basic_string_view.html)

------

## std::string_view 的实现原理

> PS，不感兴趣就直接跳，但是知道比不知道好 `:)`

虽然标准没有规定具体的内部结构，但 **所有主流实现（libstdc++ / libc++ / MSVC STL）都使用两字段或三字段的简单结构**：

```cpp
template<class CharT, class Traits = std::char_traits<CharT>>
class basic_string_view {
    const CharT* _ptr;   // 指向底层字符序列（不拥有）
    size_t       _len;   // 长度（不含 '\0'）
};

```

#### 特点

1. **不拥有内存（non-owning view）**
2. 只保存两份轻量信息：指针 + 长度
3. 复制便宜：仅复制两个字（8 字节指针 + 8 字节 size）
4. 任何"子串操作"（substr、remove_prefix）都只改 `_ptr`/`_len`，**O(1) 无分配**

相比之下，`std::string` 除了指针外，还管理容量、分配器、部分还包含小字符串优化（SSO），同时有析构逻辑，成本完全不同。所以这样看`std::string`显然很重，对吧。

------

## string_view 内部函数如何处理数据？

#### 例：substr

```cpp
string_view substr(size_t pos, size_t count) const {
    return string_view(_ptr + pos, min(count, _len - pos));
}

```

完全没有开辟新内存，仅调整指针和长度。

#### remove_prefix

```cpp
void remove_prefix(size_t n) {
    _ptr += n;
    _len -= n;
}

```

#### compare / find 等操作

全部是对 `_ptr` 指向的内存直接遍历（通常依赖 `Traits::compare`），不涉及新内存创建。

------

#### 一句话总结其实现哲学

**`string_view` 是一个 lightweight façade（轻量外壳），把任意字符序列变成 "可操作的只读字符串对象"，但永远不负责内存。**这种我相信大伙就会拉高警惕了。肯定处理不好就要跟生命周期炸了。所以：这既是优势，也是最大的风险来源（生命周期问题）。

------

## 与 const char* 的本质对比（逐项分析）

笔者记得之前有在知乎上看到大佬们的讨论：设计上，跟const char*的区别在哪里？实际上回顾设计，笔者认为，如果说std::string封装了char[]，那么`string_view`封装了const char\*。

------

#### 表达能力：string_view 是"带长度的字符串"，char* 是"指针"

感谢GPT，我写了一会，让他拉了一个表格，可以看看：

| 特性                                   | `std::string_view`       | `const char*`                            |
| -------------------------------------- | ------------------------ | ---------------------------------------- |
| 是否包含长度                           | ✔ 有 `size()`            | ❌ 没有，需要 `strlen`                    |
| 表示子串是否安全                       | ✔ 完整支持（有长度）     | ❌ 只能通过临时修改 `'\0'` 或传递额外长度 |
| 是否可以为任意字节序列服务（含零字符） | ✔ 可以（长度独立）       | ❌ 需要 NUL 终止                          |
| 是否支持遍历、查找、比较等高级接口     | ✔ 丰富的成员函数         | ❌ 几乎没有，用 C 函数                    |
| 字面量转换是否简洁                     | ✔ `"abc"sv`（C++17 UDL） | ✔ 可直接使用 `"abc"`                     |

核心区别是：

- 【string_view = (指针, 长度)】
- 【const char* = 指针 + 隐含以 '\0' 终止】

所以`string_view` 的 **显式长度** 是一个巨大的优势。因为有的时候这种`\0`不是我们的意图。

------

#### 2）安全性：string_view 对比 const char*

##### string_view 在访问越界方面更安全

```cpp
sv[i]  // 有边界检查（debug），release 通常不检查但基于 _len 计算

```

而：

```cpp
p[i]   // 完全没有任何边界概念

```

#### string_view 在生命周期方面更危险（容易悬空）

`string_view` 不拥有内存，所以很容易这样写出 bug：

```cpp
std::string_view sv = std::string("abc"); // 指向临时 -> 悬空

```

但 `const char*` 同样会悬空，例如：

```cpp
const char* p = std::string("abc").c_str(); // 同样悬空

```

**两者都会悬空，区别只是 `string_view` 更喜欢被隐式构造，所以更容易犯错**。

------

#### 3）性能差异

| 操作                  | string_view             | const char*                 |
| --------------------- | ----------------------- | --------------------------- |
| 复制                  | O(1) 两个字             | O(1) 一个字                 |
| 比较                  | 长度可用，性能更好      | 必须扫描并比对直到 `'\0'`   |
| 子串操作              | O(1)                    | 必须手工构造新的指针/终止符 |
| 与 std::string 互操作 | 🚀 直接构造 view，无拷贝 | ❌ 常需 strlen，可能 O(n)    |

典型性能差：

##### 扫描长度

```cpp
strlen(const char*)  // O(n)
sv.size()            // O(1)

```

所以如果你的函数这样写：

```cpp
void foo(const char* p);

```

然后内部多次 `strlen(p)`，会变成 O(n²) 模式。

改成：

```cpp
void foo(std::string_view sv);

```

就没有这种性能坑。

------

## 用法层面的巨大差异

#### string_view 是 "只读字符串"的语义类型

它明确告诉读者：

- **我不修改**
- **我不复制**
- **我不拥有数据**

而 const char* 无法表达所有这些语义：

```cpp
const char* p;

```

你根本不知道：

- p 是不是只读（也许来自 char 数组）
- p 是否指向 NUL 终止的空间
- p 是否有固定长度
- p 是否能包含 '\0'
- p 是否有效

string_view 解决了这些语义上的歧义。

------

## 常用成员 / 操作速查（选取关键 API）

- `size(), length(), empty()`：长度/空判断（如 `basic_string`）。
- `data()`：返回指向当前视图起始字符的指针 —— **注意：不保证以 `'\0'` 结尾**（如果你从完整 C 字符串构造且未做子视图操作，可能是以 `'\0'` 终止，但不能依赖）。因此用 `data()` 传给要求 NUL 结尾的 C API 常常是错误的。
- `substr(pos, count)`：返回一个新的 `string_view`（O(1)）表示子区间，不分配内存。
- `remove_prefix(n)`, `remove_suffix(n)`：修改视图（移动起点或缩短长度），也都是 O(1)。
- 比较函数 `compare()` 与重载了 `==, <, >` 等操作符（按字典序 / 长度等比较规则），`operator<<` 支持流输出。

------

## 字面量与便捷写法

C++17 提供了 UDL（user-defined-literal）`""sv`，可以直接写 `"hello"sv` 得到一个 `std::string_view`（该字面量在 `std::literals::string_view_literals` 命名空间中）。这是构造对字面量的常用快捷方式。

------

## 性能语义与接口设计建议

- **传参：按值还是按 `const&`？** 通常建议把 `std::string_view` 当成小的值类型来传递（即按值）。理由包括：传值消除一次间接，代码更可读（也见 ISO C++ 社区与 Abseil 的建议），不过在某些 ABI/平台（历史上 MSVC x86-64 等）下，按值并不总是更快，但整体实践建议"习惯性按值传递"。
- **用作容器键（`unordered_map`/`unordered_set`）？** 标准库为 `string_view` 提供了 `std::hash` 的特化，可以直接作为键，不过关键在于：**使用 `string_view` 作为键时必须保证被视图指向的数据的生命周期至少与哈希表中该键的生命周期一样长**，否则会发生悬空引用。`std::hash<string_view>` 与 `std::hash<string>` 的行为在 cppreference 有说明（hash 相等性的描述）。

------

## 还要再强调一下生命周期与悬空（真正的"坑"）

**核心警示：`std::string_view` 不拥有底层数据。它不会延长底层对象的生命周期。**
 典型错误场景：

```cpp
std::string_view f() {
    std::string s = "hello";
    return std::string_view{s}; // 返回后 string s 被销毁，视图悬空 —— 未定义行为
}

```

或：

```cpp
auto sv = std::string_view{ some_function_returning_temp_string() }; // temp 被析构，sv 悬空

```

这类"use-after-free / dangling view"是 `string_view` 最常见与最严重的 bug 根源。静态分析器和代码审查要重点关注这类模式。学术/工程社区也有研究工具检测这类问题。

**如何防御：**

1. 在 API 层级明确语义：若函数需要持有字符串副本，参数就用 `std::string`（或在内部做 `std::string` 的拷贝）；若只在调用期内使用，则 `string_view` 很合适。
2. 不把 `string_view` 存入需要长期持有的容器，除非你能保证底层缓冲区的所有权（例如静态常量字面量或一直存活的池）。
3. 在从 `std::string` 获取 `string_view` 并传递给异步/延迟执行的代码时尤其小心（例如线程、异步任务、lambda 捕获后延迟执行）。
4. 使用静态分析工具或编译器警告（并保持 Code Review 关注）来捕捉典型用法错误。

------

## `data()` 与 NUL 终止问题（实践警告）

`string_view::data()` 返回的缓冲并**不保证**以 NUL (`'\0'`) 结尾（例如对一个通过 `remove_suffix` 或 `substr` 生成的视图），因此把 `sv.data()` 直接传给只接受 C 风格 NUL 终止字符串的 API（如 `printf("%s")`、一些老 C 库函数）是容易出错的。若确实需要 NUL 终止，必须显式拷贝到 `std::string` 并在末尾加 `'\0'`（或在 C++20 可用 `std::string svstr(sv);`）。

------

## 常见误用样例与正确写法（代码示例）

笔者之前不太会用`std::string_view`，就干出来过这种事情：

#### 错误：返回指向临时的 string_view（悬空）

```cpp
std::string_view make_view_bad() {
    return std::string("temp"); // UB：返回的 view 指向临时 string 的缓冲区
}

```

#### 正确：如果需要长期保存，拷贝到 std::string

```cpp
std::string make_copy() {
    return std::string("temp");
}
auto v = make_copy();            // v 是 std::string，拥有数据
std::string_view sv = v;        // sv 可安全使用，前提是 v 不销毁

```

#### 好的 API 习惯（接受任意只读字符串）

```cpp
#include <string_view>

void process(std::string_view sv) {
    // 只在调用期间使用 sv
    if (sv.size() > 10) { /*...*/ }
}

int main() {
    std::string s = "hello";
    process(s);            // implicit conversion
    process("literal");    // ok, string literal 的 storage 是静态的，会被放置到data段所以无所谓
}

```

通常推荐把 `std::string_view` 作为"只读输入参数"的首选类型（按值）。

------

## 与 std::string / char* 的转换与互操作

- `string_view` 可隐式从 `std::string` 或 `const char*` 构造（注意悬空风险）。
- `std::string` 可以从 `string_view` 构造（会进行拷贝）。若需 C 风格 NUL 结尾的缓冲区，构造后可用 `c_str()` 或 `data()`（C++11 后 `data()` 返回 NUL 终止的 char* 在部分版本的标准里有细微差别，但从 C++17 `std::string::data()` 保证可用于 read-only NUL-terminated C string）。对于 `string_view::data()`，**仍不保证末尾 NUL**。

# Reference

下面是本文中用到的重要参考资料（点击即可阅读权威描述）：

- cppreference — `std::basic_string_view`（总览、成员、注意事项）。([C++参考文献](https://en.cppreference.com/w/cpp/string/basic_string_view.html?utm_source=chatgpt.com))
- cppreference — `basic_string_view` 构造函数细节页面。([C++参考文献](https://en.cppreference.com/w/cpp/string/basic_string_view/basic_string_view.html?utm_source=chatgpt.com))
- cppreference — `data()` 的说明（**不保证 NUL**）。([C++参考文献](https://en.cppreference.com/w/cpp/string/basic_string_view/data.html?utm_source=chatgpt.com))
- cppreference — 用户字面量 `operator""sv`（`"..."sv`）。([C++参考文献](https://en.cppreference.com/w/cpp/string/basic_string_view/operator%22%22sv.html?utm_source=chatgpt.com))
- cppreference — `std::hash` 对 `string_view` 的特化说明。([C++参考文献](https://en.cppreference.com/w/cpp/string/basic_string_view/hash.html?utm_source=chatgpt.com))
- cppreference — 比较/运算符文档（compare / operator== 等）。([C++参考文献](https://en.cppreference.com/w/cpp/string/basic_string_view/operator_cmp.html?utm_source=chatgpt.com))
- cppreference — `operator<<`（流输出支持）。([C++参考文献](https://en.cppreference.com/w/cpp/string/basic_string_view/operator_ltlt.html?utm_source=chatgpt.com))
- 学术/工程：关于 `string_view` 生命周期错误检测的研究（示例论文）。([arXiv](https://arxiv.org/abs/2408.09325?utm_source=chatgpt.com))
- 讨论/示例：StackOverflow 上关于 `string_view` 悬空与实际示例的讨论（入门级错误示例）。([Stack Overflow](https://stackoverflow.com/questions/55790420/is-string-view-really-promoting-use-after-free-errors?utm_source=chatgpt.com))
- ISO WG21 提案 / 未来工作：`zstring_view` 提案（示例）。([开放标准](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3655r0.html?utm_source=chatgpt.com))

------

### 结束语

`std::string_view` 是 C++17 带来的非常实用且高效的工具：在适合的地方它能显著减少复制，提高解析/处理字符串的性能。但同时，它也把"谁负责数据所有权"这个问题显式地交还给了程序员。把 `string_view` 当作"轻量的只读窗口"来用，并在接口设计与生命周期边界处格外小心，你就能既享受性能又保证安全。
