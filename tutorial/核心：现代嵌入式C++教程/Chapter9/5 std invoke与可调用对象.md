# 嵌入式C++教程——std::invoke与可调用对象

## 引言

你在写一个通用回调系统，需要处理各种类型的可调用对象：普通函数、成员函数、Lambda、仿函数。每种类型的调用语法都不一样——函数指针直接调，成员函数要用`.*`或`->*`，仿函数像函数一样调，Lambda又是另一种类型。于是你写了一堆模板特化，代码变得又臭又长。

然后你发现了`std::invoke`，这是C++17给我们的"万能调用器"。不管什么类型的可调用对象，统一一个接口搞定。

> 一句话总结：**`std::invoke`是可调用对象的统一调用接口，能以统一方式处理函数、成员函数、Lambda和仿函数，在编写泛型代码时特别有用。**

------

## 问题的由来：可调用对象的调用语法差异

让我们先看看各种可调用对象的调用方式有多乱。这不是说C++设计有问题，而是历史包袱+灵活性带来的代价。

```cpp
#include <iostream>
#include <functional>

// 普通函数
void free_function(int x) {
    std::cout << "free_function: " << x << std::endl;
}

// 仿函数
struct Functor {
    void operator()(int x) const {
        std::cout << "Functor: " << x << std::endl;
    }
};

// 带成员函数的类
struct Widget {
    void member_function(int x) {
        std::cout << "Widget::member_function: " << x << std::endl;
    }

    int data = 42;
};
```

现在的问题是，你要写一个泛型函数来"调用"这些东西。如果没有`std::invoke`，你需要这样写：

```cpp
// 传统写法：需要针对不同类型做不同处理
template<typename Func, typename... Args>
void call_traditional(Func&& f, Args&&... args) {
    // 对普通函数和仿函数可以直接调
    std::forward<Func>(f)(std::forward<Args>(args)...);
}

// 但成员函数怎么办？你需要知道它是成员函数
template<typename R, typename C, typename... Args>
void call_member(R (C::*mem_func)(Args...), C* obj, Args&&... args) {
    (obj->*mem_func)(std::forward<Args>(args)...);
}

// 使用时很麻烦
Widget w;
call_traditional(free_function, 42);          // OK
call_traditional(Functor{}, 42);              // OK
call_member(&Widget::member_function, &w, 42); // 要用专门版本
```

这还只是冰山一角。考虑`const`成员函数、引用限定符、`std::function`、Lambda……你会发现这个特化矩阵会无限膨胀。

------

## std::invoke登场：统一调用接口

`std::invoke`的存在就是为了解决这个混乱。它的核心思想是：**无论什么可调用对象，都用同一个语法调用**。

```cpp
#include <functional>

// 最简单的泛型调用器
template<typename Func, typename... Args>
void call_universal(Func&& f, Args&&... args) {
    std::invoke(std::forward<Func>(f), std::forward<Args>(args)...);
}

// 使用：所有可调用对象统一处理
Widget w;

// 普通函数
call_universal(free_function, 42);

// 仿函数
call_universal(Functor{}, 42);

// Lambda
call_universal([](int x) { std::cout << "Lambda: " << x << std::endl; }, 42);

// 成员函数——第一个对象参数会被自动识别
call_universal(&Widget::member_function, &w, 42);

// 成员变量
call_universal(&Widget::data, w) = 100;
std::cout << "widget.data = " << w.data << std::endl;
```

看到那个成员函数调用了吗？传统写法需要`(obj->*mem_func)(args)`，用`std::invoke`只需要`std::invoke(mem_func, obj, args)`。它在内部自动处理了所有这些细节。

------

## invoke的底层机制：它怎么知道怎么调用？

`std::invoke`的实现原理其实不复杂，就是一堆编译期类型判断。我们来看一个简化版的实现思路：

```cpp
namespace detail {

    // 检测类型是否是成员函数指针
    template<typename T>
    struct is_member_function_pointer : std::false_type {};

    template<typename R, typename C, typename... Args>
    struct is_member_function_pointer<R (C::*)(Args...)> : std::true_type {};

    template<typename R, typename C, typename... Args>
    struct is_member_function_pointer<R (C::*)(Args...) const> : std::true_type {};

    // 普通可调用对象的分支
    template<typename Func, typename... Args,
             std::enable_if_t<!is_member_function_pointer<std::decay_t<Func>>::value, int> = 0>
    auto invoke_impl(Func&& f, Args&&... args) {
        return std::forward<Func>(f)(std::forward<Args>(args)...);
    }

    // 成员函数指针的分支
    template<typename R, typename C, typename... Args, typename Obj, typename... Rest>
    auto invoke_impl(R (C::*mem_func)(Args...), Obj&& obj, Rest&&... rest) {
        if constexpr (std::is_pointer_v<std::decay_t<Obj>> ||
                      std::is_same_v<std::decay_t<Obj>, std::reference_wrapper<C>>) {
            return (obj.*mem_func)(std::forward<Rest>(rest)...);
        } else {
            return (std::forward<Obj>(obj).*mem_func)(std::forward<Rest>(rest)...);
        }
    }

}

template<typename Func, typename... Args>
auto invoke(Func&& f, Args&&... args) {
    return detail::invoke_impl(std::forward<Func>(f), std::forward<Args>(args)...);
}
```

当然这是极度简化的版本，标准库的实现要考虑更多边界情况。但核心思想就是：**编译期判断类型，分派到对应的调用方式**。

------

## 嵌入式场景：统一的事件处理系统

在嵌入式开发中，我们经常需要注册各种回调——可能是全局函数、成员函数、Lambda。用`std::invoke`可以让这个系统变得非常干净。

```cpp
#include <functional>
#include <array>
#include <cstdint>

// 简单的回调管理系统
class CallbackManager {
public:
    // 注册回调——可以接受任何可调用对象
    template<typename Func>
    void register_callback(int id, Func&& callback) {
        callbacks[id] = Callback{std::forward<Func>(callback)};
    }

    // 触发回调——统一使用invoke调用
    void trigger(int id, uint32_t timestamp) {
        if (id >= 0 && id < callbacks.size() && callbacks[id].invoke) {
            callbacks[id].invoke(timestamp);
        }
    }

private:
    // 类型擦除的回调存储
    struct Callback {
        // 用std::function存储，或者用更轻量的手动类型擦除
        std::function<void(uint32_t)> invoke;
    };

    std::array<Callback, 8> callbacks{};
};

// ========== 使用示例 ==========

class LEDController {
    int pin;
    int flash_count = 0;

public:
    LEDController(int p) : pin(p) {}

    // 成员函数作为回调
    void on_timer(uint32_t timestamp) {
        flash_count = (flash_count + 1) % 10;
        // GPIO_Write(pin, flash_count % 2);
        std::cout << "LED on pin " << pin << " flash: " << flash_count << std::endl;
    }
};

// 全局函数作为回调
void global_timer_handler(uint32_t timestamp) {
    std::cout << "Global timer fired at " << timestamp << std::endl;
}

void setup_system() {
    CallbackManager manager;
    LEDController led(5);

    // 注册全局函数
    manager.register_callback(0, global_timer_handler);

    // 注册Lambda
    manager.register_callback(1, [](uint32_t ts) {
        std::cout << "Lambda callback at " << ts << std::endl;
    });

    // 注册成员函数——需要捕获对象和成员函数指针
    // 这里用invoke的地方在于内部统一调用
    manager.register_callback(2, [&led](uint32_t ts) {
        std::invoke(&LEDController::on_timer, led, ts);
    });

    // 触发测试
    manager.trigger(0, 1000);
    manager.trigger(1, 2000);
    manager.trigger(2, 3000);
}
```

你可能注意到那个`std::invoke(&LEDController::on_timer, led, ts)`。这里就是`invoke`的威力——你不需要记住成员函数的调用语法，只需要按顺序传递"要调用什么"和"参数列表"。

------

## 实战：用invoke实现通用的forwarding wrapper

有时候我们需要包装一个函数调用，在调用前后做一些额外工作（比如日志、计时、锁管理）。`std::invoke`让这种包装器变得非常通用：

```cpp
#include <chrono>
#include <iostream>

// 通用包装器：测量任何可调用对象的执行时间
template<typename Func, typename... Args>
auto timed_invoke(Func&& func, Args&&... args) {
    auto start = std::chrono::high_resolution_clock::now();

    // 前置操作

    // 统一调用
    auto result = std::invoke(std::forward<Func>(func),
                             std::forward<Args>(args)...);

    // 后置操作
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "Call took " << duration.count() << " us" << std::endl;

    return result;
}

// 使用示例
struct Calculator {
    int add(int a, int b) const {
        // 模拟耗时操作
        volatile int x = a + b;
        return x;
    }
};

int compute(int x, int y) {
    volatile int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        sum += x * y;
    }
    return sum;
}

void test_timed_invoke() {
    Calculator calc;

    // 包装普通函数
    timed_invoke(compute, 5, 3);

    // 包装Lambda
    timed_invoke([]() { return compute(10, 20); });

    // 包装成员函数
    timed_invoke(&Calculator::add, calc, 5, 3);
}
```

这种包装器在嵌入式系统中特别有用——你可以用它来统一测量关键路径的执行时间，而不用为每种函数类型写专门版本。

------

## invoke与完美转发：参数怎么传？

`std::invoke`会保留参数的值类别，配合完美转发使用时效果最好：

```cpp
template<typename Func, typename... Args>
auto forward_and_call(Func&& func, Args&&... args) {
    // invoke内部会正确处理转发
    return std::invoke(std::forward<Func>(func),
                      std::forward<Args>(args)...);
}

// 测试值类别保持
void take_by_value(int x) {
    std::cout << "by value: " << x << std::endl;
}

void take_by_lvalue_ref(int& x) {
    std::cout << "by lvalue ref: " << x << std::endl;
    x *= 2;
}

void take_by_rvalue_ref(int&& x) {
    std::cout << "by rvalue ref: " << x << std::endl;
}

void test_forwarding() {
    int value = 10;

    // 左值被转发为左值引用
    forward_and_call(take_by_lvalue_ref, value);
    std::cout << "after lvalue ref: " << value << std::endl;

    // 右值被转发为右值引用
    forward_and_call(take_by_rvalue_ref, 42);
}
```

这里的关键是`std::invoke`不会改变参数的值类别——它只是把参数原样传给可调用对象。

------

## invoke_result与编译期返回类型推导

C++17还提供了`std::invoke_result_t`，可以在编译期获取`invoke`调用的返回类型：

```cpp
#include <type_traits>

struct Multiplier {
    double operator()(int x, int y) const {
        return static_cast<double>(x) * y;
    }
};

void test_invoke_result() {
    // 编译期推导返回类型
    using Result1 = std::invoke_result_t<decltype(&Multiplier::operator()), Multiplier, int, int>;
    static_assert(std::is_same_v<Result1, double>);

    // 用于模板约束
    template<typename Func, typename... Args>
    auto call_and_print(Func&& func, Args&&... args)
        -> std::invoke_result_t<Func, Args...>
    {
        using Ret = std::invoke_result_t<Func, Args...>;
        if constexpr (std::is_void_v<Ret>) {
            std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
            std::cout << "(void return)" << std::endl;
        } else {
            Ret result = std::invoke(std::forward<Func>(func),
                                   std::forward<Args>(args)...);
            std::cout << "result: " << result << std::endl;
            return result;
        }
    }

    // 使用
    call_and_print(Multiplier{}, 3, 4);  // 输出: result: 12
    call_and_print([](){ std::cout << "hello" << std::endl; });  // 输出: hello (void return)
}
```

在写泛型库的时候，这个特性特别有用——你可以在编译期就确定返回类型，而不是等到运行时才发现类型不匹配。

------

## 性能考虑：invoke有没有开销？

这是个好问题。让我们从编译器的角度看看`std::invoke`到底做了什么：

```cpp
// 直接调用
int direct = add(3, 4);

// 通过invoke调用
int indirect = std::invoke(add, 3, 4);
```

在`-O2`优化级别下，这两种写法生成的汇编代码**几乎完全一样**。`std::invoke`本身只是一层薄薄的包装，编译器很容易把它内联掉。

看个实际例子：

```cpp
// 编译器 explorer: -O2 优化
int add(int a, int b) { return a + b; }

template<typename F, typename... Args>
auto wrapper(F&& f, Args&&... args) {
    return std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
}

void test() {
    int x = wrapper(add, 3, 4);
}
```

生成的汇编（简化）：

```asm
; x 直接被计算为常量 7
; 整个 wrapper 和 invoke 都被优化掉了
mov eax, 7
```

当然，如果你的可调用对象是通过类型擦除存储的（比如`std::function`），那确实会有间接调用的开销。但这个开销不是`std::invoke`带来的，而是类型擦除本身的代价。

**结论**：在模板代码中使用`std::invoke`基本零开销，编译器会把它优化到和直接调用一样。

------

## 嵌入式开发中的注意事项

### 1. 代码体积

`std::invoke`是模板，每次实例化会产生新的代码。如果你在编译期实例化了成百上千种不同签名，确实会增加代码体积。但这个问题不是`invoke`独有的，任何模板代码都这样。在热路径上使用，通常没问题；在冷路径上，可以考虑统一类型签名。

### 2. 编译期诊断

`std::invoke`的错误信息有时候不太友好。因为它是泛型代码，类型匹配失败时，编译器可能吐出一长篇模板实例化堆栈。遇到这种情况，用`static_assert`配合`invoke_result_t`做早期检查：

```cpp
template<typename Func, typename... Args>
void safe_call(Func&& func, Args&&... args) {
    using Expected = void;  // 期望的返回类型
    using Actual = std::invoke_result_t<Func, Args...>;

    static_assert(std::is_same_v<Actual, Expected>,
                 "Return type mismatch!");

    std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
}
```

### 3. 与C API的交互

嵌入式系统经常要与C API打交道，这类API不认识C++的可调用对象。你需要用一层适配：

```cpp
extern "C" {
    using c_callback_t = void(*)(void* user_data, int event);

    void register_c_callback(c_callback_t cb, void* user_data);
}

class CppWrapper {
    std::function<void(int)> callback_;

public:
    template<typename Func>
    void set_callback(Func&& cb) {
        callback_ = std::forward<Func>(cb);
        register_c_callback(
            [](void* user_data, int event) {
                auto* self = static_cast<CppWrapper*>(user_data);
                std::invoke(self->callback_, event);  // 用invoke调用
            },
            this
        );
    }
};
```

------

## 实战案例：命令解析器

让我们用`std::invoke`实现一个简单的命令解析系统。这在嵌入式CLI（命令行接口）开发中很常见：

```cpp
#include <string>
#include <unordered_map>
#include <sstream>
#include <functional>
#include <iostream>

class CommandParser {
public:
    // 注册命令——接受任何可调用对象
    template<typename Func>
    void register_command(const std::string& name, Func&& handler) {
        commands_[name] = CommandWrapper{std::forward<Func>(handler)};
    }

    // 执行命令
    bool execute(const std::string& line) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        auto it = commands_.find(cmd);
        if (it == commands_.end()) {
            std::cout << "Unknown command: " << cmd << std::endl;
            return false;
        }

        return it->second.invoke(iss);
    }

private:
    struct CommandWrapper {
        std::function<bool(std::istringstream&)> invoke;
    };

    std::unordered_map<std::string, CommandWrapper> commands_;
};

// ========== 使用示例 ==========

// 全局函数
bool handle_set(std::istringstream& iss) {
    std::string key, value;
    iss >> key >> value;
    std::cout << "Set " << key << " = " << value << std::endl;
    return true;
}

class ConfigManager {
    std::unordered_map<std::string, int> config_;

public:
    // 成员函数作为命令处理器
    bool handle_get(std::istringstream& iss) {
        std::string key;
        iss >> key;
        auto it = config_.find(key);
        if (it != config_.end()) {
            std::cout << key << " = " << it->second << std::endl;
        } else {
            std::cout << key << " not found" << std::endl;
        }
        return true;
    }

    bool handle_list(std::istringstream&) {
        std::cout << "Config entries:" << std::endl;
        for (const auto& [k, v] : config_) {
            std::cout << "  " << k << " = " << v << std::endl;
        }
        return true;
    }
};

int main() {
    CommandParser parser;
    ConfigManager config;

    // 注册各种类型的处理器
    parser.register_command("set", handle_set);
    parser.register_command("get", [&config](std::istringstream& iss) {
        return std::invoke(&ConfigManager::handle_get, config, iss);
    });
    parser.register_command("list", [&config](std::istringstream& iss) {
        return std::invoke(&ConfigManager::handle_list, config, iss);
    });

    // 执行命令
    parser.execute("set baudrate 115200");
    parser.execute("get baudrate");
    parser.execute("list");
    parser.execute("unknown");  // Unknown command

    return 0;
}
```

这个例子里，`std::invoke`让我们能够统一处理全局函数、Lambda和成员函数。命令注册的代码非常干净，调用者不需要关心底层是什么类型。

------

## 小结

`std::invoke`是个小而强大的工具：

- **统一接口**：函数、成员函数、Lambda、仿函数，一个语法全搞定
- **泛型友好**：写模板库时，不需要为每种可调用类型写特化
- **零开销**：编译器优化后和直接调用一样快
- **类型安全**：编译期检查，错误比C风格回调更容易发现

在嵌入式开发中，`std::invoke`特别适合用于：
- 事件系统和回调管理
- 命令解析器和CLI
- 通用包装器（日志、计时、锁等）
- 需要存储多种可调用类型的容器

下一章我们将探讨函数式错误处理模式，看看如何把`optional`、`expected`这些工具组合起来，写出既优雅又健壮的错误处理代码。
