---
chapter: 4
cpp_standard:
- 17
description: 用 variant 替代 union，配合 visit 实现类型安全的多态
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 3: Lambda 基础'
reading_time_minutes: 13
related:
- std::optional
- 错误处理的现代方式
tags:
- host
- cpp-modern
- intermediate
- variant
- 类型安全
title: std::variant：类型安全的联合体
---
# std::variant：类型安全的联合体

`std::variant`（C++17 引入）是 `union` 的现代替代品。它要解决的问题很具体：在“同一时刻只持有多种类型之一”的约束下，怎么把类型安全保住。裸 `union` 自己不记得当前放的是哪个成员，`variant` 可就不一样了：它随时知道自己持有的是什么类型，您访问时它检查，所持对象的构造和析构也归它管。这一篇咱们就从裸 `union` 的毛病入手，一步步把 `variant` 的机制和用法弄清楚。

## 裸 union 的两处老毛病

咱们直接看下面这段代码怎么出事：

```cpp
union Data {
    int i;
    float f;
    char* s;
};

Data d;
d.i = 42;
// 现在 d.f 是什么？没人知道——因为 union 不知道你上次写的是哪个成员
std::cout << d.f << "\n";  // UB（未定义行为）：把 int 的位模式当 float 读
```

毛病就出在 `union` 自己身上：它不记录当前持有的是哪个成员。想安全地用，写代码的人就得在旁边自己维护一个“标签”，跟踪现在活跃的是谁。标签要是忘了更新，或者跟实际的状态对不上，读出来的就是未定义行为（UB，undefined behavior：标准对程序的行为不再做任何规定），编译器一条提示都不会给咱们。

更麻烦的地方在构造和析构上：`union` 不会替咱们构造成员，也不会替咱们析构。`std::string` 就是最典型的例子，它属于非平凡类型（non-trivial：构造和析构带着申请、释放内存的实际动作），您把它塞进 `union`、想让它活过来，咱们得亲手写一句 placement new（在一块指定的内存上构造对象的 new 写法），用完了，还得亲手调用它的析构函数送它走。

```cpp
union BadUnion {
    BadUnion() : i(0) {}   // 默认构造被隐式删除，得自己动手写
    ~BadUnion() {}         // 析构也一样被隐式删除
    int i;
    std::string s;         // C++11 起允许放进 union，但生命周期全归您管
};

BadUnion u;
// u.s = "hello";  // UB！没有先构造 s
new (&u.s) std::string("hello");  // placement new
// ... 用完后必须手动析构
u.s.~basic_string();
```

这样的手工活笔者一行都不想多写：每一步都得靠人记着，咱们要是漏掉一步，轻一点的下场是资源泄漏，重一点就直奔未定义行为了。`std::variant` 把这些全接管了：构造、切换、析构，咱们一步都不用碰。

## variant 的基本用法

### 构造与赋值

`std::variant<Types...>` 在任一时刻持有的，都是 `Types...` 里恰好一种类型的值。您不给初值、直接默认构造，它构造的就是第一个备选类型（要是想让“空”也算一种合法的备选，可以放一个空的 `std::monostate` 占位）：

```cpp
#include <variant>
#include <string>
#include <iostream>

int main()
{
    // 默认构造：持有 int（第一个备选），值为 0
    std::variant<int, double, std::string> v;

    // 赋值：自动切换到对应类型
    v = 42;                        // 持有 int
    v = 3.14;                      // 持有 double
    v = std::string("hello");      // 持有 std::string

    // 构造时直接指定
    std::variant<int, std::string> v2 = std::string("world");
}
```

咱们每赋一次值，`variant` 就自动把旧值销毁、把新值构造好了。生命周期这件事您一次都不用亲手管，全部由它的内部机制接手了。

### 访问值

咱们要访问 `variant` 里存的值，常用的手段有三样：

```cpp
std::variant<int, double, std::string> v = 3.14;

// 方式一：std::get<T> —— 类型不匹配时抛出 std::bad_variant_access
double d = std::get<double>(v);   // OK
// int bad = std::get<int>(v);    // 抛出异常！

// 方式二：std::get_if<T> —— 不抛异常，返回指针
if (auto* ptr = std::get_if<double>(&v)) {
    std::cout << "double: " << *ptr << "\n";
}

// 方式三：std::holds_alternative<T> —— 只检查类型
if (std::holds_alternative<double>(v)) {
    std::cout << "it's a double\n";
}
```

您只想查类型、不取值，`std::holds_alternative` 就够了。想拿值的指针、又不想处理异常，该用的是 `std::get_if`。您要是确信类型没记错，也接受不匹配时当场抛异常提醒您，那就轮到 `std::get` 了。

## std::visit 与访问者模式

咱们真用起来，访问 `variant` 的主力是 `std::visit`。它要的输入有两样：一个可调用对象（visitor、访问者）加若干个 `variant` 对象，然后它按 `variant` 当前持有的类型，把调用分派给对应的重载。它比 `switch-case` 安全在哪呢？编译器会替咱们核对，所有备选类型是不是都处理到了。

### 使用 lambda 的简单 visit

```cpp
std::variant<int, double, std::string> v = std::string("hello");

std::visit([](auto&& arg) {
    std::cout << arg << "\n";
}, v);
```

这里的 `auto&&` 是万能引用（forwarding reference），咱们传左值、传右值进来都行。`visit` 会按 `v` 当前持有的类型，把这个 lambda 实例化出对应的版本来调。咱们要是对所有类型都做同一件事，写一个 lambda 就够了。

### 重载集合：处理不同类型

更常见的场面是各类型各有各的逻辑：int 走 int 的处理，string 走 string 的。这时候咱们要的就是“重载集合”：一个可调用对象，对每一种备选类型都有对应的重载。C++17 给了咱们一个经典写法：

```cpp
// 重载集合工具（C++17 惯用法）
template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

// C++17 推导指引
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;
```

咱们看它把多个 lambda 的 `operator()` 全部继承到一起，攒成了一个对多种类型都有重载的可调用对象。用的时候长这样：

```cpp
std::variant<int, double, std::string> v = 3.14;

std::visit(Overloaded{
    [](int i)         { std::cout << "int: " << i << "\n"; },
    [](double d)      { std::cout << "double: " << d << "\n"; },
    [](const std::string& s) { std::cout << "string: " << s << "\n"; }
}, v);
```

装载与分发做成了动画，您可以播放、暂停，也可以按步进键单步看 index 怎么变、箭头指向哪个分支：

<Anim id="variant-visit" />

漏了哪个类型都过不了编译，报错就落在了 `visit` 调用那一行，把匹配不上的调用指给您看。

> 咱们别指望标准库替咱们收编这套写法：`std::visit` 从 C++17 定稿起，它的签名就是一个 visitor 配若干个 `variant`，从不接受多个可调用对象的写法。`std::overload` 的提案（P0051R3）没被采纳，所以手写 `Overloaded` 到今天仍是主流。visit 家族真正的新动静，是 C++26 给 `variant` 添了成员版的 `variant::visit`，跟多 lambda 没什么关系，咱们知道有这回事就行。

### 返回值的 visit

咱们还能让 visitor 带返回值。要求只有一条：各个 lambda 的返回类型得能转换成同一个类型。咱们看例子：

```cpp
std::variant<int, double, std::string> v = 42;

auto type_name = std::visit(Overloaded{
    [](int)    -> std::string { return "int"; },
    [](double) -> std::string { return "double"; },
    [](const std::string&) -> std::string { return "string"; }
}, v);

std::cout << "type is: " << type_name << "\n";  // "type is: int"
```

## 拿 variant 替代运行时多态

`variant` 还有一项重要用途：咱们可以拿它顶替虚函数那套运行时多态，英文的说法叫“闭式层次结构”（closed hierarchy），意思是备选类型在定义的那一刻就收拢在一个固定的列表里。咱们熟悉的虚函数多态，靠的是堆分配、虚函数表指针和引用语义，`variant` 把值直接存在了栈上，也就省了虚函数那层调用的开销。

```cpp
#include <variant>
#include <iostream>
#include <memory>
#include <vector>

// ---- 方式一：传统虚函数多态 ----
struct ShapeBase {
    virtual ~ShapeBase() = default;
    virtual double area() const = 0;
};

struct CircleV : ShapeBase {
    double radius;
    explicit CircleV(double r) : radius(r) {}
    double area() const override { return 3.14159 * radius * radius; }
};

struct RectangleV : ShapeBase {
    double width, height;
    RectangleV(double w, double h) : width(w), height(h) {}
    double area() const override { return width * height; }
};

// ---- 方式二：variant + visit ----
struct Circle {
    double radius;
    explicit Circle(double r) : radius(r) {}
};

struct Rectangle {
    double width, height;
    Rectangle(double w, double h) : width(w), height(h) {}
};

using Shape = std::variant<Circle, Rectangle>;

double area(const Shape& s)
{
    return std::visit(Overloaded{
        [](const Circle& c)    { return 3.14159 * c.radius * c.radius; },
        [](const Rectangle& r) { return r.width * r.height; }
    }, s);
}
```

咱们把两种用法摆到一起对比：

```cpp
// 虚函数方式：需要指针/引用，需要堆分配
std::vector<std::unique_ptr<ShapeBase>> shapes_v;
shapes_v.push_back(std::make_unique<CircleV>(5.0));
shapes_v.push_back(std::make_unique<RectangleV>(3.0, 4.0));

for (const auto& s : shapes_v) {
    std::cout << s->area() << "\n";
}

// variant 方式：值语义，栈上存储
std::vector<Shape> shapes;
shapes.push_back(Circle(5.0));
shapes.push_back(Rectangle(3.0, 4.0));

for (const auto& s : shapes) {
    std::cout << area(s) << "\n";
}
```

`variant` 这边的优势，您一条条看下来都很实在：它给的是值语义，咱们全程不用写 `new` 和 `delete`。内存是连续的，`vector` 里存的就是值本身，访问的时候对缓存也友好。类型检查也全落在了编译期，`visit` 的每个分支都是编译期定下来的。代价当然也不是没有：每新增一种形状，您都得回头改 `Shape` 的 `variant` 定义，类型层次要是“开放”的（第三方可以扩展新类型），虚函数仍然是更合适的选择。

## 异常安全与 valueless_by_exception

咱们再讲一个比较特殊的状态，它的名字就叫 `valueless_by_exception`。它是这么来的：`variant` 正在切换类型（比如赋值、`emplace`，也就是原地构造新值），新值的构造函数抛了异常，它就可能落进“无值”的状态。标准在这里给的就是可能性口径：异常发生的时候旧值还保不保得住，由实现说了算。

```cpp
struct ThrowCopy {
    ThrowCopy() = default;
    ThrowCopy(const ThrowCopy&) { throw std::runtime_error("boom"); }
};

std::variant<int, ThrowCopy> v = 42;
try {
    ThrowCopy tc;
    v = tc;  // 切换到 ThrowCopy：拷贝构造抛异常，切换没走完
} catch (const std::runtime_error&) {
    // v 现在是 valueless_by_exception 状态
    std::cout << "valueless: " << v.valueless_by_exception() << "\n";  // 1（true）
}
```

上面代码的输出是笔者在本机实测的。打印出来的 valueless 是 1。笔者又顺手探了一眼 index()，它这时候交回的也是 `variant_npos`（按有符号看是 -1）。

有意思的是，同一套 libstdc++ 的另一头也见得着：碰上 trivially copyable 的小个头类型，emplace 会在临时的 variant 里把新值构造好，构造成功了才搬进来，中途要是抛了异常，咱们手里的旧值是原地不动的。判定条件是 libstdc++ 自己定的（类型得 trivially copyable、个头也不能太大），标准里的“可能”两个字，就是给这些实现差异留的余地。

它处在这个状态的时候，咱们一调 `std::visit`，拿到的就是 `std::bad_variant_access` 异常，`std::get` 抛的也是异常。所以您的代码里要是真可能出现它，咱们最好在访问之前检查一下。

其实吧，正常使用里咱们很难见到 `valueless_by_exception`，它只在“构造新值的时候抛了异常”这类场景下才可能出现。您要是把所有备选类型的构造函数都写成 `noexcept` 的（或者干脆不用异常），那就完全不用替这个状态操心了。

## 消息类型系统

消息传递是 `variant` 最合适的一类实战场景。在事件驱动的架构里，消息队列中的消息可能有多种类型，每种类型的载荷（payload：消息里实际装的数据）都不一样，咱们用 `variant` 加 `visit` 处理，写出来的东西非常干净。往下看一份消息系统的骨架：

```cpp
#include <variant>
#include <string>
#include <vector>
#include <cstdint>
#include <iostream>
#include <queue>

// 消息类型定义
struct Heartbeat {
    uint32_t source_id;
};

struct TextMessage {
    uint32_t source_id;
    std::string content;
};

struct DataPacket {
    uint32_t source_id;
    std::vector<uint8_t> payload;
};

struct Disconnect {
    uint32_t source_id;
    std::string reason;
};

using Message = std::variant<Heartbeat, TextMessage, DataPacket, Disconnect>;

// 消息处理器
class MessageHandler {
public:
    void on_message(const Message& msg)
    {
        std::visit([this](auto&& m) { handle(m); }, msg);
    }

    void process_queue()
    {
        while (!queue_.empty()) {
            on_message(queue_.front());
            queue_.pop();
        }
    }

    void push(Message msg) { queue_.push(std::move(msg)); }

private:
    std::queue<Message> queue_;

    void handle(const Heartbeat& h)
    {
        std::cout << "Heartbeat from " << h.source_id << "\n";
    }

    void handle(const TextMessage& t)
    {
        std::cout << "Text from " << t.source_id << ": " << t.content << "\n";
    }

    void handle(const DataPacket& d)
    {
        std::cout << "Data from " << d.source_id
                  << ", size=" << d.payload.size() << "\n";
    }

    void handle(const Disconnect& dc)
    {
        std::cout << "Disconnect from " << dc.source_id
                  << ": " << dc.reason << "\n";
    }
};
```

好处要等您新增一种消息类型（比如 `FileTransfer`）才显出来：`visit` 的调用处会直接编译报错，提醒您必须给 `handle` 补上对应的重载。

## 配置值与 AST 节点

### 配置值

配置系统里经常要存不同类型的值：整数、浮点数、字符串、布尔值，咱们拿 `variant` 来装正合适。具体怎么把一个字符串认成这些类型，咱们看 `parse_value` 的骨架：

```cpp
using ConfigValue = std::variant<int, double, std::string, bool>;

struct ConfigEntry {
    std::string key;
    ConfigValue value;
};

// 读取配置
ConfigValue parse_value(const std::string& s)
{
    // 尝试解析为 int
    try {
        std::size_t pos;
        int i = std::stoi(s, &pos);
        if (pos == s.size()) return i;
    } catch (...) {}

    // 尝试解析为 double
    try {
        std::size_t pos;
        double d = std::stod(s, &pos);
        if (pos == s.size()) return d;
    } catch (...) {}

    // 尝试解析为 bool
    if (s == "true")  return true;
    if (s == "false") return false;

    // 默认作为字符串
    return s;
}
```

### AST 节点

编译器或解释器的前端里，抽象语法树（AST：把源码结构解析成的那棵树）的节点类型，咱们也能拿 `variant` 表示。节点是什么样子，咱们看一份极简的定义：

```cpp
struct NumberLiteral { double value; };
struct StringLiteral { std::string value; };
struct BinaryExpr;
struct UnaryExpr;

using Expr = std::variant<
    NumberLiteral,
    StringLiteral,
    std::unique_ptr<BinaryExpr>,
    std::unique_ptr<UnaryExpr>
>;

struct BinaryExpr {
    Expr left;
    std::string op;
    Expr right;
};

struct UnaryExpr {
    std::string op;
    Expr operand;
};
```

这里咱们用的是 `std::unique_ptr<BinaryExpr>`，不是直接的 `BinaryExpr`，原因出在不完整类型上（incomplete type：只声明了名字、还没给出完整定义的类型），`variant` 装不了它。递归的数据结构，咱们就得靠指针（或者 `std::unique_ptr`）把循环依赖断开。

## 内存布局与性能考量

`variant` 的大小，等于最大那个备选类型的大小，再加一小块记录“当前是哪个备选”的元数据字段。所以，您手里明明只存了一个 `int`，`variant<int, std::string>` 也至少有 `sizeof(std::string) + sizeof(size_t)` 那么大。

```cpp
std::cout << "sizeof(variant<int, double, string>): "
          << sizeof(std::variant<int, double, std::string>) << "\n";
// 典型输出：40（64 位平台上，string 占 32 字节，int 占 4 字节，double 占 8 字节）
std::cout << "sizeof(string): " << sizeof(std::string) << "\n";
// 典型输出：32
```

> 咱们补一句 int 的大小：标准只规定它至少 16 bits（2 字节），常见的平台一律给 4 字节，完整的说明您可以在这个[网址](https://en.cppreference.com/cpp/language/types)上读。当然，这个事情您别当八股文背诵。
> 可以参考 [YukunJ](https://github.com/YukunJ) 老师提供的[案例](https://godbolt.org/z/sbvEMW56G)。

这样的大小，绝大多数的应用都完全接受得了。倒是内存极端受限的嵌入式场景，您可能得掂量一下，选 `variant` 还是手写 `union` 加 `enum` 标签的老方案。不过按笔者的看法，`variant` 带来的类型安全收益，通常远大于几个字节的内存开销。

## 参考资源

- [cppreference: std::variant](https://en.cppreference.com/w/cpp/utility/variant)
- [cppreference: std::visit](https://en.cppreference.com/w/cpp/utility/variant/visit2)
- [cppreference: std::bad_variant_access](https://en.cppreference.com/w/cpp/utility/variant/bad_variant_access)
- [C++ Core Guidelines: C++ union](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#c181-avoid-naked-unions)
