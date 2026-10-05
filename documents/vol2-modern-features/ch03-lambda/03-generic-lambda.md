---
chapter: 3
cpp_standard:
- 14
- 17
- 20
description: 从 auto 参数到模板参数，lambda 的泛型编程能力
difficulty: intermediate
order: 3
platform: host
prerequisites:
- 'Chapter 3: Lambda 基础'
- 'Chapter 3: Lambda 捕获机制深入'
reading_time_minutes: 13
related:
- 函数式编程模式
tags:
- host
- cpp-modern
- intermediate
- lambda
- 泛型
title: 泛型 Lambda 与模板 Lambda
---
# 泛型 Lambda 与模板 Lambda

前两篇的主线里，咱们写的 lambda 大多把参数类型定死：`int`、`uint32_t`、`const std::string&`。第一篇后段咱们还见过一眼 `auto` 参数，它留下的内容正好由这一篇接着讲完。真到了项目里，很多 lambda 的逻辑其实对类型是中性的：排序的比较器只要求类型支持 `<`，累加器只要求类型支持 `+` 就行了。要是咱们为每种类型都各写一份 lambda，那就等于回到了 C++98 仿函数的老路上：同一段逻辑换了个类型就得再抄一遍，写出来的东西重复又冗余。C++14 给了 lambda 泛型的能力（`auto` 参数）。C++20 又往前走了一步，直接让 lambda 拥有了显式模板参数列表。咱们就从 C++14 的 `auto` 说起。

---

## C++14 泛型 lambda——auto 参数

您还记得第一篇见过的那个 `add` 吧：参数直接写了 `auto`。这样的 lambda 在标准里有个正式的名字，咱们叫它泛型 lambda（generic lambda）。`auto` 参数是 C++14 引入的写法，在咱们这些调用者看来，它的行为和模板函数一模一样：不同类型的参数进来，编译器就为每种类型各实例化了一份 `operator()`。

```cpp
// 泛型 lambda：接受任何支持 operator+ 的类型
auto add = [](auto a, auto b) {
    return a + b;
};

int xi = add(3, 4);                         // int
double xd = add(3.14, 2.72);                // double
std::string xs = add(std::string("hi "), std::string("there"));
```

您看上面三次调用：同一个 `add`，喂给它的整数、浮点、字符串全都接住了，咱们一行模板语法都没写。

咱们把一个 lambda 对象和 `operator()` 实例的对应关系画了出来：

![泛型 lambda 的实例化：一份 lambda 对应多份 operator() 实例](./03-generic-lambda-instant.drawio)

### 底层实现：模板调用运算符

那编译器在背后把它翻译成的闭包类型，长什么样呢？咱们看个简化版（闭包类型，就是编译器替每个 lambda 生成的那个类）：

```cpp
// 你写的
auto add = [](auto a, auto b) { return a + b; };

// 编译器生成的（简化）
struct ClosureType {
    template<typename T1, typename T2>
    auto operator()(T1 a, T2 b) const {
        return a + b;
    }
};
```

咱们对着数一遍：每个 `auto` 参数都对应着闭包类型 `operator()` 上的一个模板参数。写了两个 `auto`，`operator()` 就成了一个双模板参数的成员函数模板。既然 `operator()` 的本质是模板，泛型 lambda 也就享有了模板的那套能力，比如 SFINAE（Substitution Failure Is Not An Error 的缩写，意思是替换失败了也不算编译错误，只是把不匹配的候选从重载集里筛掉），还有显式实例化之类的手段。

### 多种类型的 auto 参数

还有一个细节值得咱们留意：每个 `auto` 都是独立的模板参数，各自的推导互不影响。

```cpp
auto multiply = [](auto a, auto b) {
    return a * b;
};

multiply(3, 4.5);    // int * double -> double
multiply(2.0f, 3);   // float * int -> float
```

如果您希望两个参数是同一个类型，C++14 里就得借助 `std::common_type_t` 求公共类型的技巧绕一下。到了 C++20 就省事了，咱们可以直接用模板参数表达，本篇后面咱们就会讲到。

---

## if constexpr 在 lambda 中

咱们离 C++20 还差一节。中间这版 C++17 倒是也没闲着，它带来的 `if constexpr`，可以在编译期根据类型信息选择不同的代码路径。而放进泛型 lambda 里，它就特别好用：咱们可以根据参数的类型特征，给不同类型挑不同的实现。

```cpp
#include <type_traits>
#include <iostream>
#include <vector>
#include <string>

auto process = [](auto& container) {
    using T = std::decay_t<decltype(container)>;

    if constexpr (std::is_same_v<T, std::string>) {
        std::cout << "Processing string: " << container << "\n";
    } else if constexpr (std::is_same_v<T, std::vector<int>>) {
        std::cout << "Processing int vector, size: " << container.size() << "\n";
    } else {
        std::cout << "Processing unknown type\n";
    }
};

void demo_if_constexpr() {
    std::string s = "hello";
    std::vector<int> v = {1, 2, 3};
    double d = 3.14;

    process(s);  // Processing string: hello
    process(v);  // Processing int vector, size: 3
    process(d);  // Processing unknown type
}
```

真正需要咱们打起精神的地方，是它的丢弃行为：不满足条件的分支会在编译期被丢弃（discarded），不参与最终的代码生成。于是咱们可以在不同分支里使用某种类型特有的操作（比如 `container.size()`），只要该分支在当前的实例化中不满足条件，编译器就不会检查它的语义正确性。不过有一点得当心：被丢弃的分支仍然要做基本的语法检查，也不能包含无法解析的模板依赖名称。

更实用的场景是处理不同的迭代器类型：随机访问迭代器可以用下标访问，前向迭代器可用的就只剩 `++` 了。而按类型挑实现这件事，正是 `if constexpr` 的本职，您以后写通用算法会反复用到它。

---

## C++20 模板 lambda——显式模板参数

泛型 lambda 的 `auto` 参数用起来确实方便，可 `auto` 也有几处别扭的地方：推导出来的具体类型叫什么名字，您没有一个直接写出来的办法。您没办法给模板参数施加约束，也没办法在 lambda 内部引用推导出来的类型去声明别的变量。C++20 干脆给 lambda 加上了显式模板参数列表，一举解决了这些问题：

```cpp
// C++20 模板 lambda：显式声明模板参数
auto add_explicit = []<typename T>(T a, T b) {
    return a + b;
};

add_explicit(3, 4);       // T = int
add_explicit(3.0, 4.0);   // T = double
// add_explicit(3, 4.0);  // 编译错误：T 不能同时是 int 和 double
```

咱们看方括号后面挂出来的那串 `<typename T>`：写法和普通模板的参数列表完全一致，平常怎么写模板函数，这里就怎么写。两个参数都声明成了 `T`，调用的时候就必须传同一类型。您看注释掉的那行 `add_explicit(3, 4.0)`，它就是过不了编译的例子。而 C++14 的 `auto` 参数恰好就做不到同型的约束。

### 在 lambda 内部使用模板参数名

咱们拿到模板参数名 `T`，在 lambda 体内就能放心使用了，比 `auto` 灵活多了：

```cpp
#include <vector>
#include <iostream>

// 用模板参数名创建同类型的容器或变量
auto transform_to_vector = []<typename T>(const std::vector<T>& input) {
    std::vector<T> result;
    result.reserve(input.size());
    for (const auto& elem : input) {
        result.push_back(elem * 2);
    }
    return result;
};

void demo_template_param_name() {
    std::vector<int> data = {1, 2, 3, 4, 5};
    auto doubled = transform_to_vector(data);
    for (int x : doubled) {
        std::cout << x << " ";   // 2 4 6 8 10
    }
    std::cout << "\n";
}
```

要是用 C++14 的 `auto` 参数，您拿到的是 `const std::vector<int>&`，但元素类型您在 lambda 内部并不知道，咱们得靠 `decltype` 去推。有了 C++20 的模板参数 `T`，一切都直截了当，`std::vector<T>` 的声明直接写就行。

### 配合 Concepts 进行约束

C++20 的 Concepts（概念，给模板参数加编译期约束的机制）和模板 lambda 配合得正好。咱们可以用 `requires` 从句对模板参数施加约束，让 lambda 只接受满足特定概念的类型：

```cpp
#include <concepts>
#include <iostream>
#include <sstream>
#include <string>

// 只接受整数类型
auto int_only = []<std::integral T>(T a, T b) {
    return a + b;
};

// 只接受浮点类型
auto float_only = []<std::floating_point T>(T a, T b) {
    return a + b;
};

// 自定义概念：支持序列化的类型
template<typename T>
concept Serializable = requires(T t, std::ostream& os) {
    { serialize(t, os) } -> std::same_as<void>;
};

auto serialize_and_log = []<Serializable T>(const T& obj) {
    std::ostringstream oss;
    serialize(obj, oss);
    std::cout << "Serialized: " << oss.str() << "\n";
};

void demo_concepts() {
    int_only(1, 2);         // OK
    // int_only(1.0, 2.0); // 编译错误：double 不满足 std::integral

    float_only(1.0, 2.0);   // OK
    // float_only(1, 2);   // 编译错误：int 不满足 std::floating_point
}
```

Concepts 约束带来的不只是编译期类型安全，错误信息也比传统 SFINAE 的做法友好得多。您传错了类型，编译器会直接告诉您"约束不满足"，还会指出具体是哪个概念失败了，而不是甩出一大堆模板实例化堆栈。这个对比咱们不用去本地开编译器。下面的 demo 把三种只允许整数的写法（concepts、`static_assert`、SFINAE）放在同一个文件里，默认是能编过的。您把 main 里被注释的错误调用一次放开一个去点“运行”，结果区给出的就是报错的原文，咱们把三种放在一起比：

<OnlineCompilerDemo
  title="动手对比：三种约束写法的报错质量"
  source-path="code/examples/vol2/56_concepts_sfinae_errors.cpp"
  description="默认编过，输出 3/7/11。放开被注释的错误调用（一次一个）再点「运行」：concepts 版直说 integral<T> 约束不满足、挂在哪一步推导上；static_assert 版给出您自己写的消息；SFINAE 版只说 no matching function，外加一串 enable_if 候选。"
  run-options="-std=c++20"
  allow-run
/>

### 调用模板 lambda 时显式指定模板参数

有时候您不想让编译器推导模板参数，而是想自己显式指定。模板 lambda 其实也支持显式的调用写法，只是写出来的语法有点特殊：

```cpp
auto identity = []<typename T>(T x) { return x; };

// 正常调用，编译器推导 T = int
auto r1 = identity(42);

// 显式指定模板参数
auto r2 = identity.template operator()<int>(42);
```

咱们得承认，`.template operator()<T>()` 这套语法确实难看了点，不过您实际上很少需要显式调用它，大部分时候编译器的推导就够用了。咱们真需要显式指定的场合主要有两类：一类是您想强制某种转换（比如把 `int` 强制作为 `double` 处理），另一类是 lambda 内部用 `if constexpr` 根据模板参数选择不同的分支。

---

## 递归 Lambda

lambda 本身是匿名的，没有名字的它自然没办法在函数体里调用自己。可递归又是编程中很常见的需求，咱们写阶乘、写斐波那契，靠的全是递归。咱们有几种绕过去的办法。

### 方式 1：用 `std::function` 包装

咱们最直观的做法，是把 lambda 存进 `std::function`（`<functional>` 里的通用可调用对象包装器）。存好了之后，lambda 就能靠这个变量名调用自己了：

```cpp
#include <functional>
#include <iostream>

void demo_recursive_std_function() {
    std::function<int(int)> factorial = [&factorial](int n) {
        if (n <= 1) return 1;
        return n * factorial(n - 1);
    };

    std::cout << factorial(5) << "\n";   // 120
}
```

笔者的实际测试（代码在 `code/volumn_codes/vol2/ch03-lambda/test_recursive_lambda_performance.cpp`）表明，在 -O2 的优化下，`std::function` 版本的递归调用比模板化实现慢约 75-145 倍，具体多少取决于递归深度和编译器的优化能力。慢在哪儿？`std::function` 的调用涉及类型擦除（type erasure，把具体类型藏到统一接口后面的技术），每一层递归都要经过虚函数表做一次间接的调用。性能敏感的代码里，这笔开销咱们得掂量掂量。

### 方式 2：泛型 lambda + auto&& 参数（Y 组合子思路）

更高效的方式，是利用泛型 lambda 的特性，把"自身引用"当作参数传了进去。这是 Y 组合子（Y combinator）思路的简化版。Y 组合子您可能头一回听说：它是 lambda 演算里的不动点组合子，出自数学家 Haskell Curry 的工作，专门让没有名字的函数也能递归。

```cpp
#include <iostream>

// Y 组合子辅助函数：接受一个高阶函数，返回它的不动点
template<typename F>
class YCombinator {
    F f_;
public:
    explicit YCombinator(F f) : f_(std::move(f)) {}

    template<typename... Args>
    decltype(auto) operator()(Args&&... args) {
        return f_(*this, std::forward<Args>(args)...);
    }
};

template<typename F>
YCombinator(F) -> YCombinator<F>;

void demo_y_combinator() {
    auto factorial = YCombinator([](auto&& self, int n) -> int {
        if (n <= 1) return 1;
        return n * self(n - 1);
    });

    std::cout << factorial(5) << "\n";   // 120
    std::cout << factorial(10) << "\n";  // 3628800
}
```

真正需要咱们看明白的，是第一个参数：泛型 lambda 的 `auto&& self` 接住的是 `YCombinator` 对象本身的引用，lambda 内部就靠 `self(n - 1)` 完成递归的调用。而 `YCombinator::operator()` 作为模板函数，编译器可以把整条调用链彻底地内联掉。

笔者拿同一份基准代码，在 g++ 15.2.1 -O2 下真跑了一轮（`1,000,000` 次 `factorial(10)` 调用），拿到的数据如下：

- `std::function` 版本：~18,700 µs（类型擦除开销，难以优化）
- Y Combinator 版本：~130-250 µs（模板化，可完全内联）
- 性能提升：约 75-145 倍

咱们在实际项目里该怎么选？递归深度不大或调用频率不高的场合，`std::function` 的简洁性可能更重要。性能要紧的代码，则更适合 Y 组合子或直接传递自身引用的方式。

### 方式 3：C++14 泛型 lambda 直接传自身

要是您不想写 Y 组合子辅助类，还有一个取巧的办法：给 lambda 加一个 `auto&&` 参数，调用的时候把 lambda 自己一并传进去：

```cpp
#include <iostream>

void demo_self_ref() {
    // fibonacci
    auto fib = [](auto&& self, int n) -> long long {
        if (n <= 1) return n;
        return self(self, n - 1) + self(self, n - 2);
    };

    std::cout << fib(fib, 10) << "\n";   // 55
}
```

代价也很直白：调用的人必须手动把 lambda 自身传进去，写的是 `fib(fib, 10)` 而不是 `fib(10)`。写法确实是有点怪的，不过在不需要封装到 API 的内部逻辑里，咱们是可以接受它的。

咱们把三种递归写法的调用链做成了动画，您可以逐步看：`std::function` 每层都要绕回包装器，Y 组合子把自身引用一路传了下去，自身传递写出来的是 `fib(fib, 10)`。

<Anim id="recursive-lambda-ways" />

---

## 通用示例

### 通用比较器

咱们从比较器说起。`std::sort` 要的只是一个能比较两个元素的函数，咱们按哪个字段比，就交给调用的人来决定。

```cpp
#include <algorithm>
#include <vector>
#include <string>

// 通用比较器：按任意字段排序
template<typename Projection>
auto make_comparator(Projection proj) {
    return [proj = std::move(proj)](const auto& a, const auto& b) {
        return proj(a) < proj(b);
    };
}

struct Employee {
    std::string name;
    int age;
    double salary;
};

void demo_generic_comparator() {
    std::vector<Employee> employees = {
        {"Alice", 30, 85000.0},
        {"Bob", 25, 72000.0},
        {"Charlie", 35, 92000.0},
    };

    // 按年龄排序
    std::sort(employees.begin(), employees.end(),
             make_comparator([](const auto& e) { return e.age; }));

    // 按薪资降序排序
    std::sort(employees.begin(), employees.end(),
             make_comparator([](const auto& e) { return -e.salary; }));

    // 按名字排序
    std::sort(employees.begin(), employees.end(),
             make_comparator([](const auto& e) -> const auto& { return e.name; }));
}
```

### 通用变换器

变换器这边咱们走得更远：连"对容器做什么"都被做成了参数。

```cpp
#include <vector>
#include <algorithm>
#include <iterator>

// 通用变换：对容器中的每个元素应用变换函数
auto make_transformer = [](auto func) {
    return [f = std::move(func)](auto& container) {
        std::transform(container.begin(), container.end(),
                      container.begin(), f);
        return container;
    };
};

// 链式变换
auto make_pipeline = [](auto... transforms) {
    return [=](auto input) {
        auto current = std::move(input);
        // 依次应用每个变换（C++17 fold expression）
        ((current = transforms(current)), ...);
        return current;
    };
};

void demo_generic_transformer() {
    auto double_it = make_transformer([](int x) { return x * 2; });
    auto add_one = make_transformer([](int x) { return x + 1; });

    std::vector<int> data = {1, 2, 3, 4, 5};
    auto result = double_it(data);    // {2, 4, 6, 8, 10}
}
```

您对着 `((current = transforms(current)), ...)` 这一行看，它就是 C++17 的折叠表达式（fold expression）。咱们可以把一包变换挨个地套到 `current` 上。

### 多态容器操作

最后咱们看容器操作。有了泛型 lambda 配合模板函数，咱们可以写出不依赖具体容器类型的通用算法。下面这个例子用泛型 lambda 打印任意类型的容器，只要容器的元素支持 `operator<<`：

```cpp
#include <iostream>
#include <vector>
#include <list>
#include <array>
#include <set>

auto print_container = [](const auto& container) {
    using T = std::decay_t<decltype(container)>;
    std::cout << "[";
    bool first = true;
    for (const auto& elem : container) {
        if (!first) std::cout << ", ";
        std::cout << elem;
        first = false;
    }
    std::cout << "]\n";
};

void demo_polymorphic_container() {
    std::vector<int> v = {1, 2, 3};
    std::list<double> l = {1.1, 2.2, 3.3};
    std::array<std::string, 2> a = {"hello", "world"};
    std::set<int> s = {5, 3, 1, 4, 2};

    print_container(v);   // [1, 2, 3]
    print_container(l);   // [1.1, 2.2, 3.3]
    print_container(a);   // [hello, world]
    print_container(s);   // [1, 2, 3, 4, 5]
}
```

您看 `demo_polymorphic_container` 里那四行调用，同一个 `print_container` 把四种容器全接住了，注释里的四行输出一个都没有少。咱们拿 `auto` 参数配上范围 for 循环，一个 lambda 就吃下所有支持迭代的容器，也就用不着再为哪种容器补一份重载了。

---

## 参考资源

- [Lambda expressions - cppreference](https://en.cppreference.com/w/cpp/language/lambda)
- [C++20 template lambdas (P0428)](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0428r2.pdf)
- [Recursive lambdas in C++14-23](https://www.dev0notes.com/intermediate/recursive_lambdas.html)

## 验证代码

本篇的性能对比和概念验证代码，您可以在 `code/volumn_codes/vol2/ch03-lambda/` 找到：

- `test_recursive_lambda_performance.cpp`：递归 lambda 不同实现的性能基准测试
- `test_concepts_error_messages.cpp`：Concepts 与 SFINAE 错误信息质量对比

咱们用 CMake 编译运行：

```bash
cd code/volumn_codes/vol2/ch03-lambda
cmake -B build
cmake --build build
./build/test_recursive_lambda_performance
./build/test_concepts_error_messages
```
