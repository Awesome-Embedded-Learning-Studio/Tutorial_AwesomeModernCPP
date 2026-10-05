---
chapter: 5
cpp_standard:
- 17
description: 用结构化绑定优雅地解包 pair、tuple、数组和结构体
difficulty: intermediate
order: 1
platform: host
prerequisites:
- 'Chapter 4: std::variant'
- 'Chapter 4: std::optional'
reading_time_minutes: 11
related:
- if/switch 初始化器
tags:
- host
- cpp-modern
- intermediate
title: 结构化绑定：一行解包多个值
---
# 结构化绑定：一行解包多个值

笔者写代码的时候，总会遇到一个别扭的场景：一个函数返回了多个值，咱们得把返回的东西一个一个拆开赋给变量。用 `pair` 的时候，咱们写 `result.first`、`result.second`，变量的含义全得靠猜。而 `tuple` 的 `std::get<0>(t)` 写起来也不好看。C++11 引入的 `std::tie` 缓解过这事，不过老实讲，它的语法也谈不上优雅：您得提前把变量都声明好，塞值的活儿才轮到 `tie`。有没有跟 Python 的 `a, b = func()` 一样爽的拆分写法？真有了，孩子们。

C++17 终于给了咱们一个正经的答案，它就是所谓的结构化绑定（Structured Binding）。咱们一行就能把 `pair`、`tuple`、数组、结构体全拆开，直接拿到有名字的变量。语义跟着就清晰了，运行时的开销也是零。

------

## 从 pair 和 tuple 讲起

### pair：最常见的多返回值

`std::pair` 是标准库里最常见的"打包两个值"的方式：`std::map::insert` 返回的就是 `pair<iterator, bool>`，而咱们遍历 map 的时候，拿到的每个元素又是 `pair<const Key, Value>`。还没有结构化绑定的日子里，咱们只能这样写：

```cpp
auto result = m.insert({1, "one"});
if (result.second) {
    std::cout << "Inserted: " << result.first->second << '\n';
}
```

`result.second` 是什么意思？您不查文档的话，根本读不出它的语义。结构化绑定则直接把语义写进了变量名里：

```cpp
auto [it, inserted] = m.insert({1, "one"});
if (inserted) {
    std::cout << "Inserted: " << it->second << '\n';
}
```

`.first`、`.second` 是怎么各自绑到一个名字上的，做成了动画。您可以播放、暂停，也能按步进键一段一段地看：

<Anim id="structured-bindings" />

拿它写范围 for 遍历 map 的时候，那可就优雅到不行了：以前咱们得写 `it->first`、`it->second`，现在写的则是 `[key, value]`：

```cpp
std::map<int, std::string> sensor_names = {
    {1, "Temperature"},
    {2, "Humidity"},
    {3, "Pressure"}
};

for (const auto& [id, name] : sensor_names) {
    std::cout << "Sensor " << +id << ": " << name << '\n';
}
```

这里有个细节：循环体里写的是 `+id` 而不是 `id`。原因在于 `uint8_t` 的 `operator<<` 会把它当字符输出，而 `+` 做的整型提升（integral promotion），会把它变成 `int` 类型的值再打印。咱们口说无凭，您自己点"动手试一试"跑一遍最直白：

<OnlineCompilerDemo
  title="动手验证：+id 的整型提升"
  source-path="code/examples/vol2/35_uint8_promotion.cpp"
  description="在线对比同一个 uint8_t：不加 + 打印成字符 A，加了 + 整型提升后打印成数字 65。"
  run-options="-std=c++17"
  allow-run
/>

咱们拿同样的 `id = 65` 试：您不加 `+`，看到的就是字符 `A`。加了 `+`，看到的才是数字。

### tuple：超过两个值的情况

等函数要返回三个甚至更多的值，`std::tuple` 就是很自然的选择了。结构化绑定的写法跟 pair 完全一致，咱们直接看：

```cpp
std::tuple<int, std::string, double> query_database(int id) {
    return {id, "sensor_" + std::to_string(id), 23.5};
}

auto [record_id, name, value] = query_database(42);
```

### 与 std::tie 的对比

C++11 的 `std::tie` 也能做类似的事情，但体验差了不少。它要求您提前把所有变量声明好，赋值的工作再交给 `tie`：

```cpp
int record_id;
std::string name;
double value;
std::tie(record_id, name, value) = query_database(42);
```

咱们对比一下就很明显了：结构化绑定的声明和解包一步到位，而 `std::tie` 得拆成两步。这里笔者倒要替 `tie` 说句公道话：它内部用的是引用，所以 tuple 里装着不可拷贝的类型（比如 `std::unique_ptr`）也能处理，因为引用的绑定压根不涉及拷贝嘛。不过论语法，还是结构化绑定的写法更简洁，而且它能支持的语义更多：按值、按引用、按转发引用都覆盖了。

------

## 原生数组与结构体

### 原生数组

固定大小的原生数组也能直接解包。咱们处理一些固定格式的数据时，那可就方便了：

```cpp
int rgb[3] = {255, 128, 0};
auto [r, g, b] = rgb;
```

二维数组的每一行，咱们也可以放进循环里解包，您看：

```cpp
int matrix[2][3] = {
    {1, 2, 3}, {4, 5, 6}
};
for (auto& row : matrix) {
    auto [a, b, c] = row;
    std::cout << a << ' ' << b << ' ' << c << '\n';
}
```

请您留意，结构化绑定只支持一维数组的直接解包。您不能写 `auto [a, b, c, d, e, f] = matrix`，因为 `matrix` 的本质是 `int[2][3]`，大小是 2 而不是 6。

### 结构体和类

咱们把条件说清楚：只要结构体的所有非静态数据成员都是 `public` 的，咱们就能拿结构化绑定直接拆开。编译器会照着声明的顺序绑定它们：

```cpp
struct SensorReading {
    uint8_t sensor_id;
    float value;
    uint32_t timestamp;
    bool is_valid;
};

SensorReading reading{5, 23.5f, 1234567890, true};
auto [id, val, ts, valid] = reading;
```

您不需要懂任何模板元编程，只要结构体成员是公有的就能用。恐怕没有比这更直观的用法了。

值得咱们留意的细节还有两个。位域（bit field）是完全支持的，`mutable` 成员也有自己的特殊行为：绑定到的"匿名变量"有可能被 `const` 修饰，而 `mutable` 成员不受它的约束，您仍然可以修改它。

------

## 三种绑定语义

结构化绑定做的并不总是拷贝，这一点咱们得看仔细：真正决定底层匿名变量类型的，是 `auto` 前面的修饰符：

- **`auto [...]`**——按值拷贝。绑定变量引用的就是拷贝。
- **`auto& [...]`**——绑定到左值引用。可以修改原对象。
- **`const auto& [...]`**——绑定到 const 左值引用。只读访问，不拷贝。
- **`auto&& [...]`**——转发引用。既能绑定左值也能绑定右值。

咱们拿一个例子把这几种区分开：

```cpp
std::pair<int, int> range{1, 10};

// 拷贝：r1、r2 引用的是匿名拷贝，不影响 range
auto [r1, r2] = range;

// 引用：直接操作原对象
auto& [r3, r4] = range;
r3 = 5;  // range.first 变成 5
```

您跑一下就能看到：`auto&` 改的是原对象，`auto` 改的是拷贝。下面的程序您点"动手试一试"就能直接跑：

<OnlineCompilerDemo
  title="动手验证：值绑定 vs 引用绑定"
  source-path="code/examples/vol2/36_binding_semantics.cpp"
  description="在线对比两种绑定语义：auto& 绑定改了 range.first（变 5），auto 拷贝绑定的 r1 不受影响（还是 1）。"
  run-options="-std=c++17"
  allow-run
/>

咱们把底层机制摊开看：编译器会替咱们声明一个匿名变量，类型的选择权在 `auto`/`auto&`/`const auto&`/`auto&&` 手里，初始化用的是右侧的表达式。而每个绑定变量，都是匿名变量的成员的引用。按值的情况下，引用的则是拷贝出来的成员。

```cpp
// auto [x, y] = get_point(); 大致等价于：
auto __anonymous = get_point();
auto& x = __anonymous.first;   // 引用匿名变量的成员
auto& y = __anonymous.second;
```

所以绑定变量本身永远是引用，它们引用的是隐藏的匿名对象的成员。您没法拿到"绑定变量本身"的地址，只能拿到它所引用的子对象的地址。

您请注意：当您用 `auto&` 的时候右侧得是左值，碰上临时对象就不行了，比如 `std::make_pair(1, 2)` 的返回值。您再拿 `auto&` 去接，编译就过不去了，因为非 const 的引用没办法绑定到右值。这时您就该改用 `const auto&` 了，或者直接走 `auto` 的按值拷贝。

```cpp
// 错误：auto& 不能绑定到临时对象
auto& [x, y] = std::make_pair(1, 2);

// 正确：const 引用可以延长临时对象生命周期
const auto& [x, y] = std::make_pair(1, 2);

// 或者直接拷贝
auto [x, y] = std::make_pair(1, 2);
```

------

## 让自定义类型支持绑定：Tuple-Like Protocol

如果您的类有私有成员，就没法走结构体的路子直接解绑了。不过 C++ 也留了别的路，就是让编译器把您的类当作 "tuple-like" 类型来处理。您只需要准备三样东西：

1. 特化 `std::tuple_size<YourType>`，告诉编译器有多少个元素。
2. 特化 `std::tuple_element<I, YourType>`，告诉编译器第 `I` 个元素的类型。
3. 在 `YourType` 的命名空间中提供 `get<I>()` 函数，返回第 `I` 个元素。

```cpp
#include <utility>
#include <cstdint>

class SensorData {
public:
    SensorData(uint8_t id, float value) : id_(id), value_(value) {}

    template<std::size_t I>
    auto& get() {
        if constexpr (I == 0) return id_;
        else if constexpr (I == 1) return value_;
    }

    template<std::size_t I>
    const auto& get() const {
        if constexpr (I == 0) return id_;
        else if constexpr (I == 1) return value_;
    }

private:
    uint8_t id_;
    float value_;
};

// 特化 tuple_size：告诉编译器有 2 个元素
template<>
struct std::tuple_size<SensorData> : std::integral_constant<std::size_t, 2> {};

// 特化 tuple_element：告诉编译器每个元素的类型
template<>
struct std::tuple_element<0, SensorData> { using type = uint8_t; };

template<>
struct std::tuple_element<1, SensorData> { using type = float; };
```

配合 `get<I>` 的 ADL 重载，咱们现在就可以愉快地解包了：

```cpp
SensorData data{5, 23.5f};
auto [id, value] = data;    // id = 5, value = 23.5
```

实跑的入口就在下面，您点"动手试一试"直接跑。当然，您还得给 `id` 加上 `+` 才能打印成数字：

<OnlineCompilerDemo
  title="动手验证：自定义类型的结构化绑定"
  source-path="code/examples/vol2/37_sensor_data_binding.cpp"
  description="在线验证 tuple-like 协议：特化 tuple_size/tuple_element 后，带私有成员的 SensorData 也能 auto [id, value] 解包。"
  run-options="-std=c++17"
  allow-run
/>

> 需要咱们打起精神的地方，是 `get<I>()` 函数放的位置：它必须定义在类所在的命名空间中（ADL 规则），编译器才能顺利地找到它。而 `std` 那边的特化有单独的要求：`tuple_size` 和 `tuple_element` 的特化您得写进 `std`，`get` 函数放在类所在的命名空间即可。

咱们回头看 `std::pair`、`std::tuple`、`std::array`：它们能被拆开，靠的也正是同一套机制，它的名字就叫 "tuple-like protocol"。

------

## C++20 的变化

C++20 对结构化绑定做了几处调整，主要跟 `constexpr` 的上下文有关，咱们挨个看。

您在 `constexpr` 函数里也能用结构化绑定。于是编译期计算函数返回的多个值，咱们照样能一行接住：

```cpp
constexpr auto get_point() {
    return std::make_pair(3, 4);
}

constexpr bool test_structured_binding() {
    auto [x, y] = get_point();
    return x == 3 && y == 4;
}

static_assert(test_structured_binding());
```

不过您要注意，不能在命名空间作用域直接声明 `constexpr` 的结构化绑定，比如 `constexpr auto [x, y] = get_point();` 这样的写法就是编译错误。原因其实也很简单：结构化绑定本质上是一组引用变量的声明，而不是单个变量的声明。

咱们再聊 lambda 捕获，这边有个常见的误会要澄清：C++17 其实就支持直接捕获结构化绑定变量。您看，下面的代码拿 C++17 就能直接跑了：

```cpp
std::map<int, std::string> m = { { 1, "one"}, {2, "two"} };

for (const auto& [k, v] : m) {
    auto callback = [k, v] {  // C++17 就支持直接捕获
        std::cout << k << ": " << v << '\n';
    };
    callback();
}
```

C++20 新增的是初始化捕获语法（`key = k`），某些情况下它反而更灵活。不过 `[=]` 默认捕获不会自动捕获结构化绑定变量，您需要显式把它们列出来。

------

## 性能：零开销的语法糖

性能上它对您没有任何负担：结构化绑定自身没有任何运行时的开销，纯粹是编译期的语法变换。编译器替咱们创建匿名变量，再让绑定变量引用它的成员，而且全部发生在编译期。

```cpp
// 这两种写法生成的汇编代码完全一样
auto [x, y] = get_point();

// 等价于
auto __tmp = get_point();
auto x = __tmp.first;
auto y = __tmp.second;
```

笔者不能空口就下"汇编完全一样"的判断，实测用的编译器是 GCC 16.1.1。咱们给两种写法各跑一遍 `g++ -std=c++17 -O2 -S`，然后做个 `diff` 就清楚了：

```bash
g++ -std=c++17 -O2 -S sb_structured.cpp
g++ -std=c++17 -O2 -S sb_manual.cpp
diff sb_structured.s sb_manual.s
```

咱们看 `diff` 的输出：报出的差异只有 `.file` 里的源文件名一处，而实际的指令完全一致：

```text
_Z1fv:                  # f()，两个版本一模一样
    movl    $7, %eax    # 直接返回 3 + 4 = 7
    ret
```

您看编译器把 `get_point()` 内联以后做了什么：它直接常量折叠成了 `movl $7, %eax`，结构化绑定连一条多余的指令都没生成。所以给您的建议也很简单：大结构体咱们用 `const auto&` 避免拷贝，而小类型（内置类型、小 struct）用 `auto` 直接按值拷贝更省事。`auto&&` 则留给泛型的代码，但类型要是已经知道了，明确地写 `auto` 或 `const auto&` 反而更清晰。

------

## 容易出错的地方

### 生命周期问题

您用 `auto&&` 绑定临时对象的时候，匿名变量的生命周期会被延长到绑定变量的作用域结束，所以用 `auto&&` 或 `const auto&` 是安全的。但如果您拿到了绑定变量的指针或引用，又把它传了出去，悬空的风险就来了：

```cpp
const auto& [x, y] = std::make_pair(1, 2);
// x, y 在这个作用域内有效，安全
// 但如果 &x 被存到外部，作用域结束后就悬空了
```

### 不能直接当返回值

结构化绑定的变量名不能直接拿来当函数返回值。您想返回解包后的值，那就得重新打包了：

```cpp
auto [x, y] = get_point();
// 不能 return x, y; 必须重新打包
return std::make_pair(x, y);

// 或者直接返回函数结果
return get_point();
```

### 不能用于类成员声明

您也不能在类的成员声明里使用结构化绑定：

```cpp
class MyClass {
    auto [x, y] = get_point();  // 编译错误
};
```

您需要存储解包后的值的话，用结构体或者 `pair`/`tuple` 的成员来代替。

------

## 在线运行

pair、tuple、数组、结构体的解包都收在一个示例里了，您在线跑一遍，就能挨个体验了：

<OnlineCompilerDemo
  title="结构化绑定：pair、tuple、数组与结构体解包"
  source-path="code/examples/vol2/11_structured_bindings.cpp"
  description="在线运行并观察结构化绑定在 pair、tuple、数组和结构体上的解包效果。"
  allow-run
/>

## 参考资源

- [cppreference: Structured binding declaration](https://en.cppreference.com/w/cpp/language/structured_binding)
- [Structured bindings in C++17, 8 years later - C++ Stories](https://www.cppstories.com/2025/structured-bindings-cpp26-updates/)
- [Adding structured bindings to your classes - Sy Brand](https://tartanllama.xyz/structured-bindings/)
