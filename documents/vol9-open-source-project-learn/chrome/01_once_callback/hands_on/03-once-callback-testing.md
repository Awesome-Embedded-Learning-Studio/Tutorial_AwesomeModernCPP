---
chapter: 1
cpp_standard:
- 23
description: 系统设计 once_callback 的测试用例，对比与 Chromium 原版和标准库方案的性能差异，总结设计取舍
difficulty: advanced
order: 3
platform: host
prerequisites:
- once_callback 设计指南（一）：动机与接口设计
- once_callback 设计指南（二）：逐步实现
reading_time_minutes: 12
related:
- 回调取消与组合模式
tags:
- host
- cpp-modern
- advanced
- 回调机制
- 函数对象
title: once_callback 设计指南（三）：测试策略与性能对比
---
# once_callback 设计指南（三）：测试策略与性能对比

代码写到这儿,`OnceCallback` 接口和实现都齐了。但笔者写完没急着收工——这种东西不拿测试压一遍,自己心里都没底。这一篇咱们就把测试策略和性能账一次性算清:它到底对不对、跟 Chromium 原版差多少、差的那些咱们认不认。

## 按"不变量"切测试

测试怎么组织,笔者一开始也犯过嘀咕。按功能分容易漏,因为功能是写给自己看的——您写的时候想得到啥,就测了啥,死角天然存在。后来换了按**不变量**分,这一刀切下去舒服多了:每个不变量本身就是一句"我保证永远成立",测试干的就是把这句话按各种姿势折磨一遍,看它崩不崩。崩了就是真的错,没崩这一类就算过。

测试代码挂在 Catch2 上,依赖用 CMake + CPM 拉。下面列的用例跟 `code/volumn_codes/vol9/chrome_design/test/test_once_callback.cpp` 里的实际代码一一对应,您手里有那份代码就能逐条跑。

### A 类：基本调用与返回值

最基本的:构造一个回调,跑一下,看返回值对不对。

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

void 返回走的是 `if constexpr (std::is_void_v<ReturnType>)` 的另一条分支,这两条用例就是给编译期分支逻辑上保险。

### B 类：移动语义

这一类盯两件事:move-only 约束别假开、移动操作别把状态搞丢。

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

move-only capture 那条用例是 `std::make_unique<int>(42)` 塞进 lambda——这玩意儿要是底层退回 `std::function` 而不是 `std::move_only_function`,直接编译都过不去,所以这条用例顺手给"咱们到底有没有真用上 move-only"兜了底。移动语义那条验证移动构造后源对象落回 `kEmpty`、`is_null()` 报真,目标对象还能照常跑。

这里笔者得拎出一个自己绕过半天的点:移动只是转交所有权,**不消费**。真正消费回调的是 `run()`。这俩看着都"动了 cb",但语义完全两码事。Chromium 那边也是同一套规矩——`PostTask(FROM_HERE, std::move(cb))` 只是把所有权搬进任务队列,回调在被真正执行之前一直活着。

### C 类：单次调用约束

A 类、B 类已经把正常调用路径趟了一遍,C 类专盯一件事:左值调用必须编不过。这个约束咱们是用 deducing this + `static_assert` 拍在签名上的,所以它压根不归运行时管——您要是手滑写成 `cb.run()` 而不是 `std::move(cb).run()`,编译器当场就拦下来,顺手把"得用 std::move"喂到错误信息里。编译过 = 验证过,跑都不用跑。

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

`bind_once` 蹚了两种典型场景:普通 lambda 的部分参数绑定、成员函数绑定。成员函数那条笔者得多说两句——`&Calc::multiply` 是成员函数指针,`&calc` 是对象指针,`std::invoke` 在底下把它展开成 `(calc.*multiply)(5, 8)`。坑在哪儿:`&calc` 是个裸指针,`bind_once` 不管它死活。要是 `calc` 在回调真正跑之前就先一步析构了,`std::invoke` 就会顺着悬空指针摸到一堆已经释放的内存。Chromium 在这里准备了三档保险——`base::Unretained` 显式声明"这指针安全自负"、`base::Owned` 直接接管所有权、`base::WeakPtr` 让对象析构时自动取消回调。咱们这个简化版暂时把这份责任甩给调用方,留到取消令牌那篇再回来收。

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

取消这一类压三个动作:令牌还活着时不取消、令牌失效后 void 回调老老实实不执行、令牌失效后非 void 回调抛 `std::bad_function_call`。第三条笔者得停一下解释——咱们对已取消的非 void 回调选择抛异常,原因是调用方眼里它要的是一个返回值,可取消态下咱们手里压根没有"有意义的值"可给。返个默认值骗它?那比抛异常更阴险,bug 会沿着这个假值往后传。Chromium 这一手更狠,直接 `CHECK` 失败把程序拉爆,咱们选异常纯粹是因为它在测试里好抓、好验证——这是教学版的取舍,不是设计上更优。

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
    REQUIRE(result == "20");  // (5*2)+10 = "20"
}

TEST_CASE("then with void first callback", "[then]") {
    int value = 0;
    auto cb = OnceCallback<void(int)>([&value](int x) { value = x; })
                  .then([&value] { return value * 3; });
    int result = std::move(cb).run(7);
    REQUIRE(result == 21);
}
```

`then()` 这一类的三条用例各压一种姿势:两级非 void 管道、跨类型的多级管道、void 前缀回调。多级管道这条笔者觉得最能说明问题——`(5*2)+10 = 20` 这个数最后被 `std::to_string` 折成字符串 `"20"`,一路上每一级的返回类型都被 `then()` 推导对了,而 `std::move_only_function` 在几种完全不同类型的 lambda 之间做的类型擦除也没崩。void 前缀那条专门压 `if constexpr (std::is_void_v<ReturnType>)` 分支——第一个回调往外部 `value` 写 7,第二个回调靠引用把 `value` 读出来乘 3 得 21。

### 测试框架与构建配置

测试框架选的 Catch2 v3,依赖让 CPM(CMake Package Manager)自动拉。CMake 配置很省心:

```cmake
# test/CMakeLists.txt
CPMAddPackage("gh:catchorg/Catch2@3.7.1")

add_executable(test_once_callback test_once_callback.cpp)
target_link_libraries(test_once_callback PRIVATE once_callback Catch2::Catch2WithMain)
target_compile_options(test_once_callback PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME test_once_callback COMMAND test_once_callback)
```

笔者用 `REQUIRE` 不用 `assert`,理由很实在:`REQUIRE` 报错会甩出失败的表达式、文件、行号,而且同一个 `TEST_CASE` 里后续断言还会继续跑;`assert` 一炸整个程序就停了,您一次只能看一个错。`REQUIRE_THROWS_AS` 专门压异常类型——取消机制那条测试就靠它确认抛的是 `std::bad_function_call` 而不是别的什么。

跑测试的姿势就一句:`build/` 目录下 `cmake --build . && ctest`。

---

## 性能账:跟 Chromium 原版对一对

### 对象大小

最直观的差就在 sizeof 上。咱们写个最小程序量一下:

```cpp
#include <functional>
#include <iostream>
#include "once_callback/once_callback.hpp"

int main() {
    std::cout << "sizeof(std::function<void()>):      "
              << sizeof(std::function<void()>) << " bytes\n";
    std::cout << "sizeof(std::move_only_function<void()>): "
              << sizeof(std::move_only_function<void()>) << " bytes\n";
    // Chromium OnceCallback<void()> ≈ 8 bytes（一个指针）

    using namespace tamcpp::chrome;
    std::cout << "sizeof(OnceCallback<void()>): "
              << sizeof(OnceCallback<void()>) << " bytes\n";
    // 我们的 OnceCallback 大约是：
    // move_only_function (32) + status (1) + token ptr (16) + padding
    // 预估 56-64 bytes
}
```

GCC 上跑出来大致是这套数:`std::function<void()>` 约 32 字节,`std::move_only_function<void()>` 约 32 字节,咱们的 `OnceCallback<void()>` 加上 `Status` 枚举和可选的 `CancelableToken` 指针,大约 56-64 字节。Chromium 的 `OnceCallback<void()>` 就 8 字节——一个指向 `BindState` 的 `scoped_refptr`,没了。

差从哪儿来?根子在存储策略。Chromium 把所有东西——可调用对象也好、绑定的参数也好——全塞进堆上的 `BindState`,回调对象自己只捏一个指针。咱们这版靠 `std::move_only_function` 的 SBO 把小对象直接内联塞进回调对象,堆分配是省了,代价是对象本体胖了一圈。

### 分配行为

`std::move_only_function` 的 SBO 阈值是实现定义的,典型在 2-3 个指针(16-24 字节)上下。捕获很轻的 lambda,比如 `[x = 42]` 或 `[&ref]`,一般塞得进 SBO,不触发堆分配;要是 lambda 拉了一票数据进来,比如一个 `std::string` 加几个 `int`,构造时就得多分一次堆。

Chromium 那一套是固定堆分配——`new BindState<Functor, BoundArgs...>` 总会跑一次,但**只跑一次**,就发生在 `BindOnce` 那一刻。之后 `OnceCallback` 的移动操作就只是复制一个 8 字节指针,极轻。咱们这版小对象时不分配(SBO 兜住),可一旦要移,就得把整个 `std::move_only_function`(32 字节)加上 `token_` 指针一起搬走,代价明显高一截。

两种策略谁也没法通吃。高频投递的小回调(浏览器是 Chrome 的主战场),Chromium 那套占便宜——移得便宜、大小齐整对 CPU 缓存友好。低频的大回调(比如一次性初始化任务),咱们这套反而划算——少分一次堆。挑哪一套,看您项目的频率分布。

### 间接调用开销

调用开销这两条路是平的:都是一次间接调用。`std::move_only_function::operator()` 底下靠函数指针或虚表派发到具体可调用对象;Chromium 的 `BindState::polymorphic_invoke_` 也是函数指针派发。`-O2` 下这一层间接编译器消不掉,所以两种方案在调用这一环上等价。

### 咱们让出了什么、换回了什么

把账算明白。

让出去的是对象的紧凑性(56-64 字节对 8 字节),换回来的是实现干净——不用自己撸引用计数、函数指针表、`TRIVIAL_ABI` 注解。移动那块也付出了代价(搬 32 字节 + 指针 vs 复制 8 字节),换回来的是小对象的零堆分配。引用计数共享这块咱也让了,没法让多个回调共用同一份 `BindState`,但 `OnceCallback` 本来就是独占语义,共享这事儿它压根用不上。

这套取舍在教学场景里、在绝大多数实际项目里都站得住。您的项目要是真压到 Chromium 那种性能要求,可以直接照着 Chromium 源码再榨一层——核心思路前三篇已经摊开了,剩下的是工程细节。

---

## 文件落在哪儿

`OnceCallback` 这一组的设计、实现、测试到这儿算是收口了,完整文件清单如下,您要找对应代码照着摸就行:

```text
documents/vol9-open-source-project-learn/chrome/hands_on/
├── 01-once-callback-design.md           # 设计篇：动机与接口
├── 02-once-callback-implementation.md   # 实现篇：逐步实现
└── 03-once-callback-testing.md          # 验证篇：测试与性能
```

对应的可编译代码(头文件 + 测试)在项目代码目录下:

```text
code/volumn_codes/vol9/chrome_design/
├── CMakeLists.txt
├── cmake/CPM.cmake
├── cancel_token/
│   └── cancel_token.hpp                 # 取消令牌
├── once_callback/
│   ├── CMakeLists.txt
│   ├── once_callback.hpp                # 主接口（模板声明）
│   └── once_callback_impl.hpp           # 实现（模板定义）
└── test/
    ├── CMakeLists.txt                   # Catch2 测试配置
    └── test_once_callback.cpp           # 完整测试用例
```

---

测试这一篇围着六个不变量——基本调用、移动语义、单次调用、参数绑定、取消机制、链式组合——拆出 12 条 Catch2 用例,`OnceCallback` 的核心行为差不多都压在底下了。性能那边跟 Chromium 原版一对,大小、分配、调用三环的账都摆出来了:咱们拿紧凑换简洁,这笔交易在绝大多数场景里划算,真要榨到 Chromium 那个量级,再回去啃源码也不迟。

`OnceCallback` 这一组到这里告一段落。后面接着要碰的是 `RepeatingCallback`(可复制、可重复调用的版本),还有把 `Unretained` / `Owned` / `WeakPtr` 这套生命周期辅助函数补到 `bind_once` 上——后者恰好是下一个主题 `WeakPtr` 的入口。

## 参考资源

- [Chromium base/functional/ 源码目录](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/)
- [cppreference: std::move_only_function](https://en.cppreference.com/w/cpp/utility/functional/move_only_function)
- [Google Test 文档](https://google.github.io/googletest/)
- [Google Benchmark 文档](https://github.com/google/benchmark)
