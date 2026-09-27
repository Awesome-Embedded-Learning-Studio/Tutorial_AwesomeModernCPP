---
chapter: 4
cpp_standard:
- 17
- 23
description: 用 optional 替代特殊值和裸指针，安全表达可选语义
difficulty: intermediate
order: 4
platform: host
reading_time_minutes: 13
related:
- 错误处理的现代方式
tags:
- host
- cpp-modern
- intermediate
- optional
- 类型安全
title: std::optional：优雅表达"可能没有值"
---
# std::optional：优雅表达"可能没有值"

笔者在之前，可是写过太多这样的代码了：查找失败了就返回 `-1`，出错了就返回 `nullptr`，配置项不存在的话，就还您一个空字符串。这些约定写的时候觉得理所当然，三个月后您再回头看，手心就开始冒冷汗了。`-1` 到底表示的是"没找到"，还是人家真的算出了一个 -1？`nullptr` 是"可选的空值"，还是"出错了"？每一个返回特殊值的函数，都在逼着三个月后的自己重新翻一遍文档。

`std::optional`（C++17 引入）就是冲着"如何安全表达可能没有值"这个问题来的。它把"有值还是没值"编码进了类型系统里，编译器和调用方都能从函数的签名上直接看到"这个返回值可能为空"，咱们不需要再靠注释或者文档来传达这件事。

## 传统方案是怎么表达"可能没有值"的

咱们回头看看 `optional` 进标准库以前，大家手头都有哪些办法。

头一种您一定写过：拿 `-1`、`UINT_MAX`、空字符串这样的特定取值来表示"无效"，行话里这套做法的学名叫**哨兵值**（sentinel value）。麻烦的地方在于，每个函数挑的"特殊值"都不一样，调用方得把所有的约定装进脑子里。而且有些类型压根找不到合适的特殊值，比如 `double` 的 `-1.0`，人家完全可能就是一个合法的返回值。

第二种是**裸指针**的方案：返回 `nullptr` 表示"没值"，这样的做法在查找函数里特别常见。它的问题是指针的语义实在太宽了。`T*` 可以表示"可能为空的可选值"，也可以表示"不拥有所有权的观察指针"，还可以表示"指向动态分配的对象"，调用方从类型上压根区分不了这些语义。更危险的是解引用：空指针一解引用，就是 UB 了。UB 的全称是 Undefined Behavior，中文的意思就是未定义行为。真出了事，它不会给咱们任何友好的错误提示。

第三种是 `std::pair<T, bool>` 的组合，第二个 `bool` 元素负责告诉您"值是否有效"。其实它比前两种方案好一点，不过用起来就啰嗦了，每次都逃不开对 `.second` 的检查，而且 `first` 在 `second == false` 时的值是未定义的，默认构造还可能是不合法的。

```cpp
// 三种传统方案对比
int find_index_old(const std::vector<int>& v, int target)
{
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
        if (v[i] == target) return i;
    }
    return -1;  // 特殊值约定：调用方必须记住 -1 表示没找到
}

int* find_ptr_old(std::vector<int>& v, int target)
{
    for (auto& x : v) {
        if (x == target) return &x;
    }
    return nullptr;  // 裸指针：语义不明确
}

std::pair<int, bool> find_pair_old(const std::vector<int>& v, int target)
{
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
        if (v[i] == target) return {i, true};
    }
    return {0, false};  // first 的值在此处无意义
}
```

咱们把三种方案摆到一起，就能看到它们共同的缺陷：类型签名没有表达出"可能没有值"的语义。`int` 的返回类型不会告诉您 `-1` 是特殊值，`int*` 也不会告诉您 `nullptr` 代表的是"没找到"而不是"出错了"。`std::optional` 直接在类型的层面把这个问题解决了。

## optional 的语义与基本 API

咱们可以把 `std::optional<T>` 读成"要么持有一个 `T` 类型的值，要么是空的"。它是值语义的类型（不是指针），持有的对象直接嵌在 `optional` 内部的存储里，动态内存分配是一次也没有的。

### 构造

```cpp
#include <optional>
#include <string>
#include <iostream>

std::optional<int> a;                      // 空（不持有值）
std::optional<int> b = 42;                 // 持有 42
std::optional<int> c = std::nullopt;       // 显式空
std::optional<std::string> d = "hello";    // 持有 "hello"

// 就地构造（避免临时对象）
std::optional<std::string> e(std::in_place, 10, 'x');  // "xxxxxxxxxx"
```

### 检查与访问

```cpp
std::optional<int> opt = 42;

// 检查是否有值
if (opt.has_value()) { /* ... */ }
if (opt) { /* ... */ }             // 等价的隐式 bool 转换

// 访问值
int x = *opt;                       // 解引用（未检查——空时是 UB）
int y = opt.value();                // 空时抛 std::bad_optional_access
int z = opt.value_or(0);            // 空时返回默认值 0

// 访问成员（对于类类型）
std::optional<std::string> name = "Alice";
if (name) {
    std::cout << "length: " << name->size() << "\n";  // operator->
}
```

至于 `operator*` 和 `value()` 的选择，笔者的建议是这样的：在您**已经检查过** `has_value()` 的代码路径里，用 `*opt` 就够了，其实它在性能和语义上都更划算。**没有检查**的情况下用 `value()` 更安全，空的时候它会抛 `std::bad_optional_access`，您拿到的是一个明确的异常，而不是 UB。不过它们俩都没有 `value_or()` 顺手，因为 `value_or()` 直接把"空值了怎么办"一并处理掉了。

### value_or：有值用值，没值用默认

`value_or()` 算得上 `optional` 里最实用的一个 API 了。您给它一个默认值，它有值的时候就把持有的值还给您，空的时候就还您那个默认值：

```cpp
std::optional<std::string> get_config(const std::string& key);

// 读取配置，未配置则使用默认值
std::string host = get_config("server_host").value_or("localhost");
int port = get_config("server_port")
    .transform([](const std::string& s) { return std::stoi(s); })
    .value_or(8080);
```

> 代码里的 `transform` 是 C++23 才加进来的新东西，咱们放到后面专门的一节里拆开看。

## optional 的内存布局

咱们把 `optional<T>` 的内部存储拆开看，看到的通常就是两块东西：一块是存放 `T` 的对齐缓冲区，另一块是记录状态的 `bool` 标志位，负责告诉咱们到底有没有值。所以 `sizeof(std::optional<T>)` 通常会大于 `sizeof(T)`。

```cpp
#include <optional>

std::cout << "sizeof(int):              " << sizeof(int) << "\n";            // 4
std::cout << "sizeof(optional<int>):    " << sizeof(std::optional<int>) << "\n";    // 典型：8
std::cout << "sizeof(double):           " << sizeof(double) << "\n";         // 8
std::cout << "sizeof(optional<double>): " << sizeof(std::optional<double>) << "\n"; // 典型：16
std::cout << "sizeof(string):           " << sizeof(std::string) << "\n";    // 典型：32
std::cout << "sizeof(optional<string>): " << sizeof(std::optional<std::string>) << "\n"; // 典型：40
```

咱们把 `optional` 内部的存储画成了一张图（`optional<int>` 的有值状态和无值状态各占一格）：

![std::optional 的内存布局：对齐缓冲区加 bool 标志位](./04-optional-layout.drawio)

实际的 `sizeof` 结果取决于标准库的实现和平台的对齐要求。不过咱们真正要认的事实只有一条：`optional<T>` 大约比 `T` 大出一个对齐后的 `bool` 的大小。受对齐的影响，有时候实际增加的字节还会比预期多一些。您可别把它当成 `optional` 的设计缺陷，它是在栈上直接存放 `T` 的值，不涉及堆上的分配，所以额外的开销是合理的。

`optional` 把持有的对象和"是否有值"的标志放在同一个对象的内部。析构的时候，`optional` 里面要是存着值的话，就会自动调用 `T` 的析构函数。这一切都是自动的，咱们不需要手工管理。

## optional 和指针，差在语义上

`optional<T>` 和 `T*` 都能表达出"可能没有值"的意思，不过您可别把它们当成同一件事的两种写法，它们的语义是截然不同的。

`optional<T>` 走的是值语义：它持有（或者打算持有）一个完整的 `T` 对象，咱们拷贝 `optional` 的时候就会把 `T` 的值一起拷过去（有值的话），析构 `optional` 的时候也会把 `T` 一起析构掉。它表达的意思是"这里有一个 `T`，又或者暂时是空的"。

`T*` 走的则是引用语义：它指向的是某个外部的 `T` 对象（或者为空）。咱们拷贝指针的时候拷的只是地址，对象本身是原地不动的。它表达的意思是"别处有一个 `T`，指针可能指的就是它"。

```cpp
std::optional<int> opt = 42;
int* ptr = &opt.value();  // 指向 optional 内部的 int

opt = 123;                // optional 重新赋值，旧的 42 被销毁
// ptr 现在可能指向 123（取决于实现），也可能悬空——不要这么用

std::optional<int> opt2 = opt;  // 拷贝：opt2 是独立的副本，持有 123
int* ptr2 = &raw;               // 假设 raw 是某个 int 变量
std::optional<int> opt3 = *ptr2;  // 拷贝 ptr2 指向的值——与 ptr2 无关
```

笔者的一般原则是这样的：您要表达"值可能存在也可能不存在"，`optional` 就是干这件事的。您要表达"指向某个外部对象的可空引用"，指针才是合适的工具。咱们不要拿 `optional` 去模拟指针，也别拿指针去扮演 `optional` 的角色，它们俩的职责本来就不同。

## 用 optional 做函数返回值

`optional` 最常见的用途就是做函数的返回值。它的语义非常明确：函数可能交回的是一个有效值，也可能给咱们一个"无值"。调用方必须在类型系统的层面处理掉"无值"的情况。

### 查找操作

```cpp
#include <optional>
#include <vector>
#include <string>

std::optional<std::size_t> find_index(
    const std::vector<int>& v, int target)
{
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == target) return i;
    }
    return std::nullopt;
}

// 调用方
auto idx = find_index(data, 42);
if (idx) {
    std::cout << "found at index " << *idx << "\n";
} else {
    std::cout << "not found\n";
}
```

跟用 `-1` 做哨兵值的那一版比起来，`optional` 把"可能为空"直接写进了类型签名。调用方当然还是可能忘记检查，不过这一忘，可比 `int` 返回值那里显眼多了，出了事的代价也不一样。`-1` 忘了查，程序就带着错误值悄悄跑下去了。您直接写 `data[*find_index(data, 42)]` 而不检查，赶上没找到的分支，解引用的就是空值，当场就是 UB 了。

### 工厂函数

工厂函数（factory function）说的就是由类提供的静态创建函数，它替您把对象造出来。至于把构造函数收进 `private`、把创建的入口只留一个，这些不算工厂函数的定义，只是眼前这个 `Connection` 的典型写法：

```cpp
class Connection {
public:
    static std::optional<Connection> create(const std::string& addr)
    {
        // 尝试建立连接
        if (addr.empty()) return std::nullopt;  // 无效参数
        // ... 实际连接逻辑
        return Connection(addr);
    }

private:
    explicit Connection(std::string addr) : addr_(std::move(addr)) {}
    std::string addr_;
};

// 使用
auto conn = Connection::create("192.168.1.1");
if (conn) {
    // 连接成功
} else {
    // 连接失败
}
```

## 用 optional 做函数参数

您也可以把 `optional` 用在函数参数上，表示"这个参数是可选的"。它比函数重载和默认参数的写法都灵活，因为调用方可以在运行的时候决定要不要提供值：

```cpp
void print_greeting(const std::string& name,
                    std::optional<std::string> title = std::nullopt)
{
    if (title) {
        std::cout << "Hello, " << *title << " " << name << "!\n";
    } else {
        std::cout << "Hello, " << name << "!\n";
    }
}

print_greeting("Alice");                    // Hello, Alice!
print_greeting("Bob", std::string("Dr."));  // Hello, Dr. Bob!
```

不过笔者要提醒一句：咱们可别过度使用 `optional` 参数。一个参数在大多数情况下都需要提供的话，那用默认值也许就更合适了。`optional` 参数适合的是"有时有、有时没有，而且两种情况的含义完全不同"的场景。

## C++23 给 optional 加的三个 monadic 操作

C++23 给 `std::optional` 引入了三个 monadic 操作：`and_then`、`transform` 和 `or_else`。monadic（单子式）这个词是从函数式编程里借来的，理论咱们不用背，您只需要看懂它们各自做什么，链式处理 `optional` 的代码就顺手了。

### transform：对值做变换

咱们在前面 `value_or` 一节里见过它：`get_config("server_port")` 后面跟着的那次 `.transform(...)` 调用，说好了要放到后面专门拆开看，现在到了兑现的时候。`transform` 接收的是一个函数：`optional` 有值的时候，咱们传进去的函数会去变换那个值，变换的结果重新包成一个 `optional` 还给您。`optional` 为空的时候，它就直接还您一个空的 `optional`。

```cpp
std::optional<int> parse_int(const std::string& s)
{
    try {
        return std::stoi(s);
    } catch (...) {
        return std::nullopt;
    }
}

// C++20 风格：手动检查
std::optional<std::string> input = get_input();
std::optional<int> result;
if (input) {
    result = parse_int(*input);
}

// C++23 风格：链式 transform
auto result2 = get_input().transform([](const std::string& s) -> int {
    return std::stoi(s);  // 简化示例，实际应处理异常
});
```

### and_then：链式组合可能失败的操作

咱们把没有 `and_then` 的老写法和链式的新写法都摆在下面的代码里，您对比着看：

```cpp
std::optional<User> find_user(int id);
std::optional<std::string> get_email(const User& u);

// C++20 风格：嵌套 if
auto user = find_user(42);
if (user) {
    auto email = get_email(*user);
    if (email) {
        std::cout << "Email: " << *email << "\n";
    }
}

// C++23 风格：链式 and_then
find_user(42)
    .and_then(get_email)
    .transform([](const std::string& email) {
        std::cout << "Email: " << email << "\n";
        return email;
    });
```

您刚才看到的两层 `if` 嵌套，就是 `and_then` 要收拾的局面：`find_user` 要是交回一个空的 `optional`，后面的 `get_email` 就压根不该执行了。`and_then` 接收的函数返回的也是 `optional`，当前的 `optional` 有值的时候，咱们给的函数就会被调用，它的结果会原样交出来。要是它为空的话，就直接返回一个空的 `optional`。这类"上一步的结果是下一步的输入，而且每一步都可能失败"的场景，拿 `and_then` 来写就比 `transform` 更顺了。

### or_else：处理空值的情况

`transform` 和 `and_then` 碰上空的 `optional`，都是把空原样传给了下一步，什么也不说。那要是空了呢？总得有人出来处理吧。`or_else` 就是在这个位置上出场的：`optional` 为空的时候，它会调用您给的函数，一般咱们拿它做日志记录，或者给一个替代的方案：

```cpp
auto email = find_user(42)
    .and_then(get_email)
    .or_else([] {
        std::cerr << "Failed to get email\n";
        return std::optional<std::string>("fallback@example.com");
    });
```

咱们把三个操作组合起来，就能写出很流畅的链式代码，多层嵌套的 `if` 语句也跟着少下去。您的编译器要是还不支持 C++23，自己写一个 map 辅助函数也是不难的。map 是函数式语言里的叫法，说的就是 `transform` 这类对值做变换的操作：您拿 `has_value()` 一查，有值就把值交给变换的函数，没值的话就返回 `nullopt`，效果是类似的。

## 实战：用 optional 做延迟初始化

咱们还可以用 `optional` 实现延迟初始化（lazy initialization），也就是把对象的构造推迟到真正要用的时候。它在对象构造代价较高、而"是否需要"在编译期定不下来的场景里特别有用：

```cpp
class ExpensiveResource {
public:
    ExpensiveResource() { /* 耗时的初始化 */ }
    void do_work() { /* ... */ }
};

class Service {
public:
    void process()
    {
        if (!resource_) {
            resource_.emplace();  // 首次使用时才构造
        }
        resource_->do_work();
    }

private:
    std::optional<ExpensiveResource> resource_;  // 初始为空
};
```

咱们用它做延迟初始化，比用 `std::unique_ptr` 的版本更优，因为对象就存放在 `optional` 自己的内部缓冲区里，省掉了 `unique_ptr` 到堆上申请的那一步。

## 嵌入式实战：配置项与传感器读取

咱们再看嵌入式系统里的场景：传感器的数据不一定每次都能成功读取，可能是传感器没就绪的缘故，也可能是总线那边超时了。配置项也不一定总是存在的，它的 `optional` 读法，前面 `value_or` 一节的 `get_config` 已经给过，这里就不重复了。咱们下面就拿传感器当例子，`optional` 可以把"可能失败"的读取表达得很干净：

```cpp
#include <optional>
#include <cstdint>

struct SensorReading {
    float temperature;
    uint32_t timestamp;
};

class TemperatureSensor {
public:
    std::optional<SensorReading> read()
    {
        if (!is_ready()) return std::nullopt;

        SensorReading r;
        r.temperature = read_raw_value() * kScale;
        r.timestamp = get_tick();
        return r;
    }

private:
    bool is_ready();
    float read_raw_value();
    uint32_t get_tick();

    static constexpr float kScale = 0.0625f;
};

// 使用
void print_temperature(TemperatureSensor& sensor)
{
    auto reading = sensor.read();
    if (reading) {
        std::printf("Temp: %.1f C (at %u)\n",
                    reading->temperature,
                    static_cast<unsigned>(reading->timestamp));
    } else {
        std::printf("Sensor not ready\n");
    }
}
```

`optional` 在这个场景里的价值，是"读取失败"这件事被写进了返回类型，您要是不检查 `has_value()`，摸到的就是 UB，失败想悄悄溜过去也是溜不掉的。返回 `0.0f` 的老做法就做不到这一点，它全靠调用方在脑子里存着"0.0 可能表示失败"的约定，谁忘了约定，谁就把失败当成了一次正常的读数。

## 参考资源

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [cppreference: std::bad_optional_access](https://en.cppreference.com/w/cpp/utility/optional/bad_optional_access)
- [C++23 Monadic operations for std::optional](https://en.cppreference.com/w/cpp/utility/optional#Monadic_operations)
- [C++ Core Guidelines: Optional](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#f21-to-return-multiple-out-values-prefer-returning-a-struct)
