---
chapter: 1
cpp_standard:
- 23
description: 系统化设计六类测试用例验证 OnceCallback 的所有核心行为，对比与 Chromium 原版和标准库方案的性能差异
difficulty: beginner
order: 6
platform: host
prerequisites:
- OnceCallback 实战（二）：核心骨架搭建
- OnceCallback 实战（三）：bind_once 实现
- OnceCallback 实战（四）：取消令牌设计
- OnceCallback 实战（五）：then 链式组合
reading_time_minutes: 8
related:
- OnceCallback 前置知识（五）：std::move_only_function
tags:
- host
- cpp-modern
- beginner
- 回调机制
- 函数对象
title: OnceCallback 实战（六）：测试与性能对比
---
# OnceCallback 实战（六）：测试与性能对比

核心骨架、`bind_once`、取消令牌、`then()` 链式组合——四块都拼完了,代码能编过,跑起来也对。但笔者写完没敢松口气,因为"能跑"和"在各种边角都对"中间还隔着一整套测试。这一篇咱们把测试补齐,顺便把笔者自己最在意的一件事摊开量一量:这套用 `std::move_only_function` 撸出来的东西,跟 Chromium 那两千多行手写的原版比,到底胖了多少、慢了多少,这些肉是从哪儿换来的。

## 测试框架搭建

笔者用 Catch2 v3 做测试框架,依赖走 CPM(CMake Package Manager)自动拉。

```cmake
# test/CMakeLists.txt
CPMAddPackage("gh:catchorg/Catch2@3.7.1")

add_executable(test_once_callback test_once_callback.cpp)
target_link_libraries(test_once_callback PRIVATE once_callback Catch2::Catch2WithMain)
target_compile_options(test_once_callback PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_once_callback COMMAND test_once_callback)
```

笔者当初从 `assert()` 换到 Catch2 主要是图两个东西:`REQUIRE` 会把失败的表达式、文件名、行号一起吐出来,而且同一个 `TEST_CASE` 里后续检查还能接着跑(不像 `assert` 一炸就停);`REQUIRE_THROWS_AS` 则专门盯异常类型。盯异常这一条对咱们取消机制那段特别有用,后面会看到。

跑测试就是老套路,在 `build/` 下 `cmake --build . && ctest`。

---

## 六类测试用例

笔者把测试拆成六类,每一类只盯一个设计不变量。为什么按不变量分、不按功能分?因为功能列表写完容易自我感动,觉得"该测的都测了",其实边角全漏掉;不变量是"这个东西在任何情况下都必须成立"的硬约束,围着它转,边界自然会冒出来。

### A 类：基本调用与返回值

```cpp
TEST_CASE("non-void return", "[once_callback]") {
    OnceCallback<int(int, int)> cb([](int a, int b) { return a + b; });
    int result = std::move(cb).run(3, 4);
    REQUIRE(result == 7);
}

TEST_CASE("void return", "[once_callback]") {
    bool called = false;
    OnceCallback<void()> cb([&called] { called = true; });
    std::move(cb).run();
    REQUIRE(called);
}
```

最朴素的两个:非 void 回调要把返回值带出来,void 回调得正常跑完。void 这条走的是 `if constexpr (std::is_void_v<ReturnType>)` 的另一条分支——笔者特意留着,因为这正是模板里两条路径最容易只测一条的地方。

### B 类：移动语义

```cpp
TEST_CASE("move-only capture", "[once_callback]") {
    auto ptr = std::make_unique<int>(42);
    OnceCallback<int()> cb([p = std::move(ptr)] { return *p; });
    int result = std::move(cb).run();
    REQUIRE(result == 42);
}

TEST_CASE("move semantics: source becomes null", "[once_callback]") {
    OnceCallback<int()> cb([] { return 1; });
    OnceCallback<int()> cb2 = std::move(cb);
    REQUIRE(cb.is_null());

    int result = std::move(cb2).run();
    REQUIRE(result == 1);
}
```

第一段 move-only capture 是笔者心里的硬指标:它直接证明咱们底层确实是 `std::move_only_function` 而不是图省事用的 `std::function`——后者这行压根编不过。第二段测移动构造之后源对象变没,这是 OnceCallback 那套"转移即掏空"的契约。

这里有个概念笔者一开始自己都绕过弯:移动只是搬家,不消费。真正把回调跑掉的只有 `run()`。`OnceCallback cb2 = std::move(cb1)` 之后,回调活得好好的,只是住址换了,直到 `cb2.run()` 才算被消费掉。把这两件事搞混,后面写取消令牌的时候会一脸懵。

### C 类：单次调用约束

这一类没有运行时测试,因为约束是编译期挡下来的——deducing this 配 `static_assert`,`cb.run()`(没 move)直接编不过,只有 `std::move(cb).run()` 才放行。编译能过,本身就是验证通过。笔者一开始还想给这条补个 `TEST_CASE`,后来想想算了,这条不变量本质就是"写错代码会编译失败",硬要测的话用 `static_assert(!std::is_invocable_v<...>)` 那套反而绕,不如让编译器当裁判。

### D 类：参数绑定

```cpp
TEST_CASE("bind_once basic", "[bind_once]") {
    auto bound = bind_once<int(int)>([](int a, int b) { return a * b; }, 5);
    int result = std::move(bound).run(8);
    REQUIRE(result == 40);
}

TEST_CASE("bind_once with member function", "[bind_once]") {
    struct Calc {
        int multiply(int a, int b) { return a * b; }
    };
    Calc calc;
    auto bound = bind_once<int(int)>(&Calc::multiply, &calc, 5);
    int result = std::move(bound).run(8);
    REQUIRE(result == 40);
}
```

两类绑定都过一遍:普通 lambda 的部分参数绑定,加上成员函数绑定。成员函数那条笔者特意把 `&calc` 这种裸指针的写法留在测试里,是想反复提醒一件事——生命周期责任全压在调用方肩上,这块的坑前面文章专门拆过,这里不再展开。

### E 类：取消机制

```cpp
TEST_CASE("is_cancelled respects cancel token", "[once_callback]") {
    auto token = std::make_shared<CancelableToken>();
    OnceCallback<void()> cb([] {});
    cb.set_token(token);

    REQUIRE_FALSE(cb.is_cancelled());
    token->invalidate();
    REQUIRE(cb.is_cancelled());
}

TEST_CASE("cancelled void callback does not execute", "[once_callback]") {
    auto token = std::make_shared<CancelableToken>();
    bool called = false;
    OnceCallback<void()> cb([&called] { called = true; });
    cb.set_token(token);
    token->invalidate();

    std::move(cb).run();
    REQUIRE_FALSE(called);
}

TEST_CASE("cancelled non-void callback throws", "[once_callback]") {
    auto token = std::make_shared<CancelableToken>();
    OnceCallback<int()> cb([] { return 1; });
    cb.set_token(token);
    token->invalidate();

    REQUIRE_THROWS_AS(std::move(cb).run(), std::bad_function_call);
}
```

取消机制这块笔者花的时间最多,因为它有三个分支得分别测:令牌还活着,不取消;令牌失效配上 void 回调,静默不执行;令牌失效配上非 void 回调,抛 `std::bad_function_call`。第三个为什么必须抛?因为调用方在等一个返回值,您总不能默默吞掉还给个默认值,那 bug 就藏到运行时去了。三个用例正好把这三条分支钉死。

### F 类：Then 组合

```cpp
TEST_CASE("then chains two callbacks", "[then]") {
    auto cb = OnceCallback<int(int)>([](int x) { return x * 2; })
                  .then([](int x) { return x + 10; });
    int result = std::move(cb).run(5);
    REQUIRE(result == 20);  // 5 * 2 + 10
}

TEST_CASE("then multi-level pipeline", "[then]") {
    auto pipeline = OnceCallback<int(int)>([](int x) { return x * 2; })
                        .then([](int x) { return x + 10; })
                        .then([](int x) { return std::to_string(x); });
    std::string result = std::move(pipeline).run(5);
    REQUIRE(result == "20");
}

TEST_CASE("then with void first callback", "[then]") {
    int value = 0;
    auto cb = OnceCallback<void(int)>([&value](int x) { value = x; })
                  .then([&value] { return value * 3; });
    int result = std::move(cb).run(7);
    REQUIRE(result == 21);
}
```

三种组合都得走一遍:两级非 void 管道是最常见的用法;多级管道特意让它跨类型边界(int 走到 string),验证 `then` 的类型推导在返回值类型变化时没掉链子;void 前缀回调那一例是笔者后来补的,因为 `then` 接在 void 回调后面时,下一个环节拿不到前一步的"返回值",得靠外部状态接力——这个边界第一版笔者就漏了。

---

## 性能对比：与 Chromium 原版

测试都绿了,接下来是笔者自己最好奇的一段:咱们这版搭出来的 OnceCallback,跟 Chromium 那套手写了引用计数和函数指针表的原版摆一起,到底差多少。先把话说前头——差距是实打实的,笔者不打算粉饰。

### 对象大小

```cpp
std::cout << "sizeof(std::function<void()>):        "
          << sizeof(std::function<void()>) << " bytes\n";
std::cout << "sizeof(std::move_only_function<void()>): "
          << sizeof(std::move_only_function<void()>) << " bytes\n";
// Chromium OnceCallback<void()> ≈ 8 bytes

std::cout << "sizeof(OnceCallback<void()>): "
          << sizeof(OnceCallback<void()>) << " bytes\n";
// 我们的：move_only_function (32) + status (1) + token ptr (16) + padding
// 预估 56-64 bytes
```

GCC 上典型数字是这样的:`std::function` 大约 32 字节,`std::move_only_function` 也是 32 字节上下,咱们的 `OnceCallback` 叠上 status 和 token 指针之后撑到 56 到 64 字节。Chromium 那个?8 字节。一个指针的价钱。

差出七倍这事笔者一开始也愣了一下,后来顺着存储策略一捋就明白了。Chromium 把绑定的参数、函数指针、引用计数这些状态一股脑塞进堆上的 `BindState`,回调对象本体只捏一根指针。咱们走的是 `std::move_only_function` 的 SBO 路子,小 lambda 直接内联在对象里,省了一次堆分配,代价是对象本身胖了一圈。

### 分配行为

这一节其实是咱们方案唯一占便宜的地方。`std::move_only_function` 的 SBO 阈值通常落在两三个指针大小(16 到 24 字节),捕获几个参数的 lambda 基本都能塞进去,不触发堆分配。捕获一大坨的大 lambda 才会在构造时去堆上要内存。

Chromium 那边反过来,永远堆分配(`new BindState`),但只分配一次,之后 OnceCallback 的移动就是复制一根 8 字节指针,轻得不像话。咱们这边小对象虽然不分配,可一旦要移动,得复制 32 字节往上的内联缓冲。一边省分配、一边省移动,各占一头。

### 间接调用开销

真到了调用这一步,两边打平,都是一次间接函数调用。`std::move_only_function::operator()` 和 Chromium 的 `polymorphic_invoke_` 走的是同一种分派方式。`-O2` 下这个间接调用谁也消不掉,跨编译单元的函数指针,编译器不敢内联。

### 取舍总结

| 指标 | 我们的方案 | Chromium 方案 |
|------|-----------|--------------|
| 回调对象大小 | 56-64 字节 | 8 字节 |
| 小 lambda 堆分配 | 不分配（SBO） | 总是分配 |
| 移动代价 | 复制 32+ 字节 | 复制 1 个指针 |
| 实现代码量 | ~200 行 | ~2000+ 行 |

表里这四行笔者反复看过几遍,觉得最值钱的是最后一行,代码量差了一个数量级。咱们不用手写引用计数,不用维护函数指针表,不用挂 `TRIVIAL_ABI` 注解去跟编译器讨价还价,这些东西全让 `std::move_only_function` 兜了。换来的是对象胖了七倍、移动贵了几倍。小 lambda 零堆分配这条,在投递频率不高的场景里反而是个意外的好处。

这笔账划不划算,得看您拿它干嘛。教学、原型、大多数业务回调,笔者觉得值;真要塞进 Chromium 那种一个进程里挂着上万个回调、还要被 `[[clang::trivial_abi]]` 推进寄存器传参的热路径——那还是老老实实去抄人家两千行。咱们这版的定位一直很清楚:把机制讲透,把取舍摆在台面上,至于哪头重,您自己掂量。

## 参考资源

- [Chromium base/functional/ 源码目录](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [Catch2 文档](https://github.com/catchorg/Catch2/tree/devel/docs)
