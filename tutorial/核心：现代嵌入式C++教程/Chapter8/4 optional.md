# 嵌入式C++教程——`std::optional`

想象一下：你问函数“有没有值？”它摇头说“也许”。`std::optional` 就是那句优雅的“也许”，比抛出异常、返回裸指针或约定用 `-1` 做“无值”标记，更有礼貌，也更不容易把别人坑死。

下面把 `std::optional` 从概念、用法、陷阱到实战小技巧，一股脑儿讲清楚——语言幽默但不耍流氓，代码直接可复制到你的博客或笔记里。

------

## 什么是 std::optional

`std::optional<T>` 表示“要么有一个 `T` 的值，要么什么都没有”。它把“可能为空”的概念搬进了类型系统，让你在类型层面把空值显式化——比随手返回 `nullptr` 或魔法常数要体面多了。

## 为什么用它

- 比裸指针语义更明确：`std::optional<T>` 表示“有或没有一个值”，而不是“可能指向堆上、栈上或野指针”。
- 比 `T` 的哨兵值（比如 `-1`、`""`）更可靠：不需要记住哪些值表示“无效”。
- 比异常适合用于“失败是常态”的场景（例如解析配置、查找缓存），调用者可以优雅地检查并处理。

## 基本用法

```cpp
#include <optional>
#include <string>
#include <iostream>

std::optional<std::string> read_env(const std::string& key) {
    // 假装从环境里找，没找到就返回空
    if (key == "HOME") return std::string("/home/alice");
    return std::nullopt;
}

int main() {
    auto maybe_home = read_env("HOME");
    if (maybe_home) {                     // 等价于 maybe_home.has_value()
        std::cout << "home: " << *maybe_home << '\n'; // 解引用
    } else {
        std::cout << "no home\n";
    }

    // 安全获取：有值则返回，否则返回默认
    std::string home = maybe_home.value_or("/tmp");
    std::cout << "home-or-fallback: " << home << '\n';

    // value()：没有值会抛 std::bad_optional_access
    try {
        std::cout << maybe_home.value() << '\n';
    } catch (const std::bad_optional_access&) {
        std::cout << "oops, no value\n";
    }
}
```

要点：

- `std::nullopt`：表示“空的 optional”；
- `operator*` / `operator->` 用于访问内部值（像指针一样）；
- `has_value()` / `operator bool()` 用于检查是否有值；
- `value()` 在空时会抛 `std::bad_optional_access`（所以慎用）。

## 更“高级”的构造和操作

```cpp
std::optional<int> a;                 // empty
std::optional<int> b = 42;            // contains 42
std::optional<int> c = std::nullopt;  // empty, 显式

// 就地构造（避免额外拷贝）
std::optional<std::string> s(std::in_place, 10, 'x'); // "xxxxxxxxxx"

// emplace：重用 optional 的存储重新构造值
s.emplace("hello");

// reset：清空
s.reset();

// 可选地持有引用 —— 注意语义差异
int x = 7;
std::optional<int&> ref = x; // optional 可以保存引用（表示“可能引用某物”）
```

小提醒：`std::optional<T&>` 是允许的，但它的语义是“可能引用某个已存在的对象”，并不会管理生命周期——别让引用悬空。

## 返回值风格：函数应该返回 `optional` 还是抛异常？

`optional` 非常适合那些**查找类**、**解析类**函数：失败并非真正的“错误”，只是“未找到”或“不可用”。例如 `find_user(id)`、`parse_int(str)` 都适合返回 `optional`。而真正的程序错误、逻辑错误仍然建议用异常或其他错误处理机制。

示例：

```cpp
std::optional<int> parse_int(const std::string& s) {
    try {
        size_t idx;
        int v = std::stoi(s, &idx);
        if (idx != s.size()) return std::nullopt; // 有多余字符
        return v;
    } catch (...) {
        return std::nullopt;
    }
}
```

调用者可以优雅地分支处理，不被异常打断控制流。

## 与指针的对比（别互相嫉妒）

- `T*`：表达“指向某个 T 的地址；可能为空”。但它并不表达“这个地址是否有效（所有权/生命周期）”。
- `std::optional<T>`：存储实际的 `T` 值（或不存），管理构造/析构；语义更强烈：“这里可能有一个完整的值”。
- `std::optional<T&>`：如果你只是想“可能引用某个外部对象”，可以用引用版本，但要小心生命周期。

## 与std::variant的边界

`optional<T>` 可以被视作 `variant<T, std::monostate>` 的简化版（只带一个备选状态）。如果你需要多个不同类型的备选分支，`std::variant` 是正确工具；想要简单的“要么有要么没有”，`optional` 更轻快。

## 常见误解与陷阱（请认真）

- **`value()` 会抛异常**：别以为 `value()` 总是安全的；如果你确定有值，用 `*` 或 `->`；否则先检查 `has_value()`。
- **大小不是固定的“+1 字节”**：`sizeof(std::optional<T>)` 与 `T` 的布局有关并且实现定义，不要硬编码内存布局。
- **不要滥用在 public API 里做“万能错误代码”**：`optional` 表示“缺少值”，不是“错误原因”。如果你需要传递错误原因（如错误码或详细信息），选 `std::expected`（C++23/提案）或自己定义结构。
- **`optional` 并非智能指针**：若需要共享/独占所有权，请使用 `shared_ptr`/`unique_ptr`。

## 实战小技巧（只三条，别贪多）

1. 返回 `optional` 代替 `nullptr`：语义更清晰，调用方必须显式处理“无值”——这能减少 bug。
2. 使用 `value_or` 提供默认值：简洁优雅，但注意可能会执行代价较高的默认构造。
3. 当“无值”同时需要错误信息时，别用 `optional` —— 用结构体或 `expected`。

## 小示例：用 optional 实现链式转换

> 许多函数式语言能优雅链式处理 `Maybe` / `Option`，C++17 没有内建 `map`，但可以写出类似风格：

```cpp
template<typename T, typename F>
auto optional_map(std::optional<T> const& opt, F f)
    -> std::optional<decltype(f(*opt))>
{
    if (opt) return f(*opt);
    return std::nullopt;
}

// 使用：
auto s = std::optional<std::string>{"42"};
auto maybe_int = optional_map(s, [](auto& str){ return std::stoi(str); });
// maybe_int 是 std::optional<int>
```

这是“手工链式”，写成小工具函数后代码会更优雅。

`std::optional` 是 C++ 标准库里很实用的小玩意儿：语义明确、表达力强、能让 API 更自描述。别把它当成万能胶：当你确实需要传递错误信息、或涉及复杂所有权时，选择适合的工具。但要是你只是要表达“这里可能有值，也可能没有值”——别再用 `-1`、别再用 `nullptr`，用 `std::optional`，让你的代码看起来更像成年人写的。