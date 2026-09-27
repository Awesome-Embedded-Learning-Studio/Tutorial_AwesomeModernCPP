---
chapter: 3
cpp_standard:
- 14
- 17
- 20
description: 高阶函数、组合、柯里化——C++ 中的函数式编程技巧
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'Chapter 3: Lambda 基础'
- 'Chapter 3: Lambda 捕获机制深入'
- 'Chapter 3: 泛型 Lambda 与模板 Lambda'
- 'Chapter 3: std::function、std::invoke 与可调用对象'
reading_time_minutes: 15
related:
- 'C++20 Ranges:范围与视图'
tags:
- host
- cpp-modern
- intermediate
- lambda
- 函数对象
title: 函数式编程模式
---
# 函数式编程模式

聊到函数式编程的时候，C++ 圈子里常有人把它划到 Haskell 那一派的名下，觉得它跟 C++ 的关系不大。笔者早些年也是这么划的，后来真的回头数了一遍才发现，C++ 其实从 C++11 开始就一直在吸收这套理念：lambda 让匿名函数成了一等公民，`std::function` 把各式可调用对象装进了同一个类型，`std::algorithm` 一族的算法，本质上就是 map、filter、reduce 的变体。不过，C++ 没有把这些东西包装成那么“纯函数式”的接口而已。

这一章走到了最后一篇，零件其实都备齐了：lambda 的捕获、泛型 lambda 的 `auto` 参数、`std::function` 的类型擦除，前四篇里咱们都拆过了。这一篇咱们把它们合起来用：函数当得了参数、当得了返回值、也进得了容器。咱们就从高阶函数看起。

---

## 高阶函数——接受或返回函数的函数

咱们说的高阶函数（Higher-Order Function），就是接受或返回函数的函数。参数里有函数的算，返回值是函数的也算，两头都占的同样算。C++ 里的实现，靠的是模板参数或者 `std::function`，04 篇里咱们刚把它们的底细摸过一遍。咱们直接看一个实际的例子，就是一个通用的重试机制。咱们传给它的，是一个可能失败的操作、一个判断要不要重试的谓词、外加最大重试次数：

```cpp
#include <iostream>
#include <functional>
#include <random>

// 高阶函数：接受"操作"和"判断函数"作为参数
template<typename Operation, typename ShouldRetry>
auto with_retry(Operation&& op, ShouldRetry&& should_retry, int max_attempts)
    -> std::invoke_result_t<Operation>
{
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        try {
            auto result = op();
            return result;
        } catch (const std::exception& e) {
            if (attempt == max_attempts || !should_retry(attempt, e)) {
                throw;
            }
            std::cout << "Attempt " << attempt << " failed: " << e.what()
                      << ", retrying...\n";
        }
    }
    throw std::runtime_error("unreachable");
}

// 使用示例
void demo_higher_order() {
    int call_count = 0;

    auto result = with_retry(
        [&call_count]() -> int {
            call_count++;
            if (call_count < 3) {
                throw std::runtime_error("connection timeout");
            }
            return 42;
        },
        [](int attempt, const std::exception& e) {
            return attempt < 5;   // 最多重试 5 次
        },
        5
    );

    std::cout << "Result: " << result << "\n";   // Result: 42
}
```

STL 里的高阶函数，您其实已经用了很多：`std::sort` 接受的是比较函数，`std::transform` 接受的是变换函数，`std::find_if` 接受的则是谓词。这些算法干的都是同一件事，它们把策略从算法里抽了出来，交给调用的人去决定。排序的骨架不用动，比大比小、谁排前面谁排后面的规则，全看您传进去的是什么函数。

### 返回函数的函数

高阶函数的另一半本事是返回函数。拿它来构造可配置的策略对象特别顺手，咱们写一个返回预设阈值过滤器的函数，您感受一下：

```cpp
auto make_threshold_filter(int threshold) {
    return [threshold](const std::vector<int>& data) {
        std::vector<int> result;
        std::copy_if(data.begin(), data.end(), std::back_inserter(result),
                    [threshold](int x) { return x > threshold; });
        return result;
    };
}

auto filter_above_50 = make_threshold_filter(50);
auto filter_above_80 = make_threshold_filter(80);
```

不过这里有个要留神的地方：不同分支要是返回了不同类型的 lambda，而每个 lambda 的闭包类型又都是独一无二的，直接返回的类型就对不上了。咱们看这个编译不过的例子：

```cpp
// ❌ 编译错误：不同分支的 lambda 类型不同
auto make_counter(bool start_high) {
    if (start_high) {
        return []() { return 100; };  // 闭包类型 A
    } else {
        return []() { return 0; };    // 闭包类型 B
    }
}
```

编译器吐出来的报错能刷一整屏，您头一回见多半会愣一下。咱们想统一返回类型，就得请出 04 篇里讲过的类型擦除，用的工具就是 `std::function`：

```cpp
// ✅ 正确：用 std::function 统一类型
std::function<int()> make_counter(bool start_high) {
    if (start_high) {
        return []() { return 100; };
    } else {
        return []() { return 0; };
    }
}
```

代价就是 `std::function` 带进来的那一点运行时开销，类型擦除和可能的堆分配都算在里头，不过大多数场景下，这点开销咱们可以忽略不计。要是真碰上了热路径，04 篇里笔者量过数字，它比直接调用慢了 7 到 9 倍。

---

## 函数组合——compose 与 pipe

咱们常说的函数组合（function composition），说的是把多个函数串起来的写法，前一个的输出正好作为后一个的输入。咱们在数学上把它记作 `compose(f, g)(x) = f(g(x))`，管道风格的记法是 `pipe(g, f)(x) = f(g(x))`，`g` 跑完了再轮到 `f`，顺着数据流动的方向走。C++ 里的实现，最干净的路子是泛型 lambda 加 `auto` 返回类型推导，03 篇攒下的本事在这儿用得上：

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

// compose：f(g(x))
auto compose = [](auto f, auto g) {
    return [f = std::move(f), g = std::move(g)](auto&&... args) {
        return f(g(std::forward<decltype(args)>(args)...));
    };
};

// pipe：先 g 后 f（语义更直觉）
auto pipe = [](auto g, auto f) {
    return [g = std::move(g), f = std::move(f)](auto&&... args) {
        return f(g(std::forward<decltype(args)>(args)...));
    };
};

void demo_composition() {
    auto double_it = [](int x) { return x * 2; };
    auto add_one = [](int x) { return x + 1; };
    auto to_string = [](int x) { return std::to_string(x); };

    // compose(add_one, double_it)(5) = add_one(double_it(5)) = add_one(10) = 11
    auto composed = compose(add_one, double_it);
    std::cout << composed(5) << "\n";    // 11

    // 多层组合
    auto pipeline = compose(to_string, compose(add_one, double_it));
    std::cout << pipeline(5) << "\n";    // "11"
}
```

组合两个函数的时候还算清爽，函数多了以后，嵌套的 `compose` 调用就读不动了。咱们升级一个可变参数版本的 `compose_all`，一次把多级组合的调用收拾利索：

```cpp
// 多函数组合：从右到左依次应用
template<typename F>
auto compose_all(F f) {
    return f;
}

template<typename F, typename... Fs>
auto compose_all(F f, Fs... rest) {
    return [f = std::move(f), ...rest = std::move(rest)](auto&&... args) {
        return f(compose_all(rest...)(std::forward<decltype(args)>(args)...));
    };
}

// pipe_all：从左到右依次应用（更直觉）
template<typename F>
auto pipe_all(F f) {
    return f;
}

template<typename F, typename... Fs>
auto pipe_all(F f, Fs... rest) {
    return [f = std::move(f), ...rest = std::move(rest)](auto&&... args) {
        return pipe_all(rest...)(f(std::forward<decltype(args)>(args)...));
    };
}

void demo_multi_compose() {
    auto double_it = [](int x) { return x * 2; };
    auto add_one = [](int x) { return x + 1; };
    auto negate_it = [](int x) { return -x; };

    // pipe: 5 -> add_one -> double_it -> negate_it
    // 5 -> 6 -> 12 -> -12
    auto pipeline = pipe_all(add_one, double_it, negate_it);
    std::cout << pipeline(5) << "\n";   // -12
}
```

对了，眼下的 `compose_all` 和 `pipe_all` 走的是递归展开的路子，C++17 的折叠表达式（fold expression）还没用上。03 篇 `make_pipeline` 里的那行 `((current = transforms(current)), ...)`，用的就是它。`pipe_all` 按从左到右的顺序应用函数，您读代码的顺序就是数据流经的顺序，所以读起来非常自然。

---

## 偏应用——绑定部分参数

偏应用（partial application）指的是把函数的一部分参数提前固定住的写法，返回一个只等剩余参数的新函数。标准库为这件事准备了 `std::bind`，不过到了现代 C++，lambda 通常是更好的选择，写出来的代码更清晰，报出来的错误信息也更友好，也没有 `std::bind` 那些奇怪的边界情况。咱们直接用 lambda 写：

```cpp
#include <iostream>
#include <functional>

// 用 lambda 实现偏应用
auto make_adder(int base) {
    return [base](int x) { return base + x; };
}

// 更通用的偏应用：固定前 N 个参数
auto partial = [](auto f, auto... fixed_args) {
    return [f = std::move(f), ...fixed_args = std::move(fixed_args)](auto&&... rest_args) {
        return f(fixed_args..., std::forward<decltype(rest_args)>(rest_args)...);
    };
};

void demo_partial_application() {
    auto add = [](int a, int b, int c) { return a + b + c; };

    // 固定第一个参数为 1
    auto add1 = partial(add, 1);
    std::cout << add1(2, 3) << "\n";   // 6

    // 固定前两个参数
    auto add1_2 = partial(add, 1, 2);
    std::cout << add1_2(3) << "\n";    // 6

    // 更实用的例子：创建预设阈值的过滤器
    auto make_threshold_filter = [](int threshold) {
        return [threshold](const std::vector<int>& data) {
            std::vector<int> result;
            std::copy_if(data.begin(), data.end(),
                        std::back_inserter(result),
                        [threshold](int x) { return x > threshold; });
            return result;
        };
    };

    auto filter_above_50 = make_threshold_filter(50);
    auto filter_above_80 = make_threshold_filter(80);

    std::vector<int> data = {12, 45, 67, 89, 23, 90};
    auto r1 = filter_above_50(data);   // {67, 89, 90}
    auto r2 = filter_above_80(data);   // {89, 90}
}
```

偏应用在事件处理和策略模式的场景里特别好使。您在配置阶段把某些参数固定下来，运行阶段只传剩下的参数就行。比起郑重其事写一个完整的策略类，一个偏应用的 lambda 轻量得多。

### 柯里化——了解概念即可

咱们经常把柯里化（currying）和偏应用混为一谈，它俩其实不是一回事。柯里化干的事，是把一个多参数函数转换成一串单参数函数的链式调用，也就是 `f(a, b, c)` 会被拆成 `f(a)(b)(c)` 的调用链。偏应用干的是固定部分参数，返回一个参数更少的函数。而柯里化让函数每次只收一个参数，收完了就返回下一个函数，等所有参数凑齐了才算完。咱们拿上面那个三参数的 `add` 对照着看最直观：偏应用固定住的是头一个参数，返回的函数等的是剩下两个。要按柯里化的路子，`add(a)` 返回的函数只等一个 `b`，把 `b` 也收下了才轮得到 `c`。

柯里化在 C++ 里的实用性其实不如偏应用。C++ 本来就支持多参数的函数调用，咱们没必要把函数全拆成单参数的链，日常更常用的还是偏应用。那咱们为什么还要讲柯里化？因为它点出了一件事：函数可以一步一步地“特化”，咱们每给定一个参数，手里就多出一个更新的、更具体的函数。

---

## map/filter/reduce——STL 算法的函数式写法

map（映射）、filter（过滤）、reduce（归约）是函数式编程处理数据的三种基本操作，STL 算法给每一种都备了对应的工具，`std::transform` 干的是 map 的活，`std::copy_if` / `std::remove_if` 干的是 filter 的活，`std::accumulate` 干的是 reduce 的活。map 和 filter 您在前面的代码里已经见过面了。reduce 对咱们算个新朋友，不过它的活您在 02 篇手写过：那个用引用捕获往 `sum` 里攒数的累加 lambda，干的就是归约。咱们把三步连起来，数据从上一步的输出流进下一步的输入，就成了数据处理管道，咱们画在下面：

![filter、map、reduce 数据处理管道](./05-functional-patterns-pipeline.drawio)

咱们用一个完整的数据处理管道，把这三步真的走一遍：

```cpp
#include <algorithm>
#include <numeric>
#include <vector>
#include <iostream>
#include <string>

struct SensorReading {
    std::string sensor_id;
    double value;
    uint32_t timestamp;
};

void demo_map_filter_reduce() {
    std::vector<SensorReading> readings = {
        {"temp_01", 23.5, 1000},
        {"temp_01", 24.1, 2000},
        {"temp_02", 45.0, 1000},
        {"temp_01", 22.8, 3000},
        {"temp_02", 47.3, 2000},
        {"temp_01", 25.0, 4000},
        {"temp_02", 44.5, 3000},
        {"temp_03", 18.2, 1000},
    };

    // === Filter：只保留 temp_01 的读数 ===
    std::vector<SensorReading> filtered;
    std::copy_if(readings.begin(), readings.end(),
                std::back_inserter(filtered),
                [](const SensorReading& r) { return r.sensor_id == "temp_01"; });

    // === Map：提取温度值 ===
    std::vector<double> values(filtered.size());
    std::transform(filtered.begin(), filtered.end(),
                  values.begin(),
                  [](const SensorReading& r) { return r.value; });

    // === Reduce：计算平均值 ===
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    double avg = sum / static_cast<double>(values.size());

    std::cout << "temp_01 readings: ";
    for (double v : values) std::cout << v << " ";
    std::cout << "\n";
    std::cout << "Average: " << avg << "\n";
    // temp_01 readings: 23.5 24.1 22.8 25
    // Average: 23.85
}
```

### 封装成可复用的函数式工具

上面那三段式的写法还能再往前走一步：把 map 和 filter 的逻辑包进泛型 lambda，调用的时候一行就够，咱们动手包两个：

```cpp
auto functional_map = [](const auto& container, auto func) {
    using Value = std::decay_t<decltype(func(*container.begin()))>;
    std::vector<Value> result;
    result.reserve(container.size());
    std::transform(container.begin(), container.end(),
                  std::back_inserter(result), func);
    return result;
};

auto functional_filter = [](const auto& container, auto pred) {
    using Value = std::decay_t<typename std::decay_t<decltype(container)>::value_type>;
    std::vector<Value> result;
    std::copy_if(container.begin(), container.end(),
                std::back_inserter(result), pred);
    return result;
};

// 链式调用示例：过滤偶数 -> 翻倍
std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
auto evens = functional_filter(data, [](int x) { return x % 2 == 0; });
auto doubled = functional_map(evens, [](int x) { return x * 2; });
```

这么写的缺点也明摆着：每次操作都会创建一个新的 `std::vector`，filter 和 map 的环节一多，临时容器就一个接一个地冒出来。攒下的开销有多大，篇末咱们拿数字说话。C++20 的 Ranges 库，靠“惰性求值”（lazy evaluation）解决了这个问题：它的视图（view）不急着算结果，等您真正迭代它的时候，才按需地把数据算出来。

---

## 不可变数据思维

函数式编程还有一条基本的主张：尽量别修改数据，要新的结果就创建新的数据。头一回听的时候会觉得挺浪费，可您顺着这个主张想一遍，好处就都在眼前了。咱们不去改数据，数据竞争也就无从谈起了，这是线程安全的起点。输入定了输出就定了，您读代码的时候，行为是能推理的。旧的数据一直都在，想做撤销和重做的时候，旧版本还留在咱们手里。在 C++ 里完全守住不可变是不现实的，但咱们可以挑关键路径来用这套思维，比如写一个“排序但不动原始数据”的函数：

```cpp
#include <vector>
#include <algorithm>

// 不可变风格：返回新容器，不修改原始数据
std::vector<int> sorted_copy(const std::vector<int>& input) {
    std::vector<int> result = input;        // 复制
    std::sort(result.begin(), result.end()); // 排序副本
    return result;                           // NRVO 优化掉返回值的复制
}
```

在现代 C++ 里（-O2/O3 的优化级别下），返回 `std::vector` 时多出来的复制，几乎全被 NRVO（具名返回值优化）或者移动语义优化掉了，所以不可变风格的开销没有看上去那么大。100 万元素的排序，笔者量过 `sorted_copy` 和直接修改原数据的 `std::sort` 的差距：只慢约 1.5%，而这 1.5% 主要花在输入数据的初始复制上，而不是返回值的复制。在确实需要保留原始数据的场景下，这个代价咱们完全可以接受。

> **性能数据来源**：笔者那套 `code/volumn_codes/vol2/ch03-lambda/test_immutability_nrvo.cpp`，在 GCC 15.2.1 上跑的，开了 `-O2`。

---

## 实战应用

### 数据处理管道

咱们来搭一个日志处理的管道，走的还是过滤、变换、归约的三段式。这沿用的是 Unix 管道的思想，每个阶段只干自己的事，而数据从上一个阶段流进下一个阶段。

```cpp
struct LogEntry {
    std::string level;
    std::string message;
    int timestamp;
};

void demo_pipeline() {
    std::vector<LogEntry> logs = {
        {"ERROR", "Disk full", 100}, {"INFO", "User login", 150},
        {"ERROR", "Network timeout", 250}, {"ERROR", "Database error", 350},
    };

    // Filter：只保留 ERROR
    std::vector<LogEntry> errors;
    std::copy_if(logs.begin(), logs.end(), std::back_inserter(errors),
                [](const LogEntry& e) { return e.level == "ERROR"; });

    // Map：提取消息
    std::vector<std::string> messages(errors.size());
    std::transform(errors.begin(), errors.end(), messages.begin(),
                  [](const LogEntry& e) { return e.message; });

    // Reduce：拼接
    std::string report = std::accumulate(
        messages.begin(), messages.end(), std::string{"Errors:\n"},
        [](const std::string& acc, const std::string& msg) {
            return acc + "  - " + msg + "\n";
        });
    std::cout << report;
}
```

### 事件过滤器链

“过滤器链”说的就是把一组谓词函数组合起来的用法，数据得通过全部的过滤器才能被接受。您在请求验证、数据校验的场景里用它会非常顺手。每个过滤器都是独立的纯函数，同样的输入只会得到同样的输出，外部的状态它也不碰。咱们可以单独测试、单独替换任何一个。您想加一条新的过滤规则？写一个 lambda 塞进数组就行了，已有的代码一行都不用动。

```cpp
struct Request {
    std::string source;
    int priority;
    std::string payload;
};

void demo_filter_chain() {
    using Filter = std::function<bool(const Request&)>;
    auto combine = [](std::vector<Filter> filters) -> Filter {
        return [filters = std::move(filters)](const Request& r) {
            return std::all_of(filters.begin(), filters.end(),
                              [&r](const Filter& f) { return f(r); });
        };
    };

    auto combined = combine({
        [](const Request& r) { return r.priority >= 0 && r.priority <= 10; },
        [](const Request& r) { return r.source == "trusted"; },
        [](const Request& r) { return r.payload.size() <= 1024; },
    });

    std::cout << std::boolalpha;
    std::cout << combined({"trusted", 5, "hello"}) << "\n";    // true
    std::cout << combined({"unknown", 5, "hello"}) << "\n";    // false
}
```

---

## Ranges 预告——C++20 的惰性视图

前面咱们用 map/filter/reduce 处理数据的时候，每次操作都会创建一个新的 `std::vector` 临时对象，管道的步骤一多，这些中间容器就攒出了不小的开销。这笔开销笔者量过：对于 100 万元素、带 filter 和 transform 的管道，老写法比 C++20 Ranges 慢了约 16 倍，还得为中间结果额外分配多个临时的容器、额外内存约 4 MB。16 倍的差距不算小了，咱们看代码也能对上号：临时容器一层一层地复制，而 Ranges 的视图把这些中间层全免了，它靠的就是前面说过的惰性求值。

> **性能数据来源**：还是笔者那套 `code/volumn_codes/vol2/ch03-lambda/test_ranges_performance.cpp`，在 GCC 15.2.1 上跑的，开了 `-O2`。

```cpp
#include <ranges>
#include <vector>
#include <iostream>
#include <algorithm>

void demo_ranges_preview() {
    std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    // Ranges：惰性管道，无中间容器
    auto result = data
        | std::views::filter([](int x) { return x % 2 == 0; })   // 偶数
        | std::views::transform([](int x) { return x * 2; })      // 翻倍
        | std::views::take(3);                                     // 取前3个

    std::cout << "Ranges result: ";
    for (int x : result) {
        std::cout << x << " ";   // 4 8 12
    }
    std::cout << "\n";
}
```

这个管道说的是三步：滤偶数、翻倍、取前三个。需要咱们多看一眼的是 `|` 运算符，它把多个视图操作串成了一条管道。整条管道在构建的时候什么都不做，等咱们的 `for` 循环迭代起来了，计算才真正地开始。中间容器没有了，多余的数据复制也没有了。

咱们把两种写法的对照做成了动画，您可以按步进键单步看：老写法逐层物化中间容器，Ranges 的管道则是走一步才算一步。

<Anim id="ranges-lazy-pipeline" />

Ranges 的 `views::filter` 和 `views::transform` 对应函数式编程的 filter 和 map，`views::take` 和 `views::drop` 对应 Haskell 的 `take` 和 `drop`，`views::join` 对应的则是 `concat`。咱们把对应的关系摆在一起看，您大概能看出来，Ranges 就是 C++ 收进标准库的函数式数据处理方案。它的细节，卷四会替咱们深入展开。

---

## 参考资源

- [STL algorithms - cppreference](https://en.cppreference.com/w/cpp/algorithm)
- [C++20 Ranges - cppreference](https://en.cppreference.com/w/cpp/ranges)
