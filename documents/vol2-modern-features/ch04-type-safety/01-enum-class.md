---
chapter: 4
cpp_standard:
- 11
- 14
- 17
- 20
description: 告别整数隐式转换，用 enum class 构建类型安全的枚举
difficulty: intermediate
order: 1
platform: host
reading_time_minutes: 13
related:
- 强类型 typedef
- std::variant
tags:
- host
- cpp-modern
- intermediate
- enum_class
- 类型安全
title: enum class 与强类型枚举
---
# enum class 与强类型枚举

笔者动笔以前，翻了一回自己以前写的 C 风格代码。满屏幕的 `enum Color { Red, Green, Blue };`，配上随处可见的 `if (color == 1)`，看得笔者直皱眉。

上一章咱们跟 lambda 泡了一整章，这一章回到一个更小、出场率却极高的东西上：枚举。像开头那样的代码，搁在没法动的老项目里，咱们也就认了。可到了 2026 年还接着这么写，就真的说不过去了。老 `enum` 的毛病凑在一块儿就是三样：隐式整数转换、命名污染、没法前向声明。每一样都够您在 code review 里挨一顿说。

`enum class`，C++11 引入的强类型枚举，就是冲着它们来的。您可别把它当成单纯的语法糖，它给的是类型安全层面的保证：老 `enum` 一路放行的那些误用，换成它之后全都变成了编译错误。

## C 风格 enum 的三个老毛病

咱们把老 `enum` 摊开检查一遍，看看它的毛病到底出在哪儿。

### 毛病一：隐式转换成整数

老式 `enum` 的值能隐式转换成 `int`，中间不需要您写任何显式转换。乍一看还挺方便的。可是方便久了，您手上就会多出这样的代码：

```cpp
enum Color { Red, Green, Blue };
enum Fruit { Apple, Orange, Banana };

void paint(int c);

paint(Red);       // OK，隐式转成 int
paint(Orange);    // 也 OK！但语义完全错了
paint(42);        // 编译通过，运行时才知道出问题

if (Red == Apple) {
    // 居然编译通过，而且为 true！因为都是 0
}
```

上面的 `paint(Orange)` 编译照过，`Red == Apple` 更是直接判成了真。对编译器而言这些值全是整数，所以比较也好、传参也好，它都不加区分地放行了。这样的 bug 在代码量大的时候极难追踪，`paint(Orange)` 这样的调用，编译器从头到尾不会给咱们一条警告。

> 您要是自己跑一遍会发现，跨枚举的比较（比如 `Red == Apple`）倒是会收到一条默认开的 `-Wenum-compare` 警告，不过它也只是警告而已，编译是照样能过的。把 `Orange` 塞给 `paint(int)` 的传参才是真正一点提示都没有的。

### 毛病二：枚举值污染外部作用域

老式 `enum` 的第二样毛病，是把所有枚举值一股脑暴露到外部作用域里去了。您要是定义了两个枚举，恰好都用了 `None` 或者 `Error` 这样的常用名字，冲突马上就来了：

```cpp
enum Status { None, Ok, Error };
enum Permission { None, Read, Write, Execute };  // 编译错误！None 重定义

// 常见的变通方案：加前缀
enum Status { Status_None, Status_Ok, Status_Error };
enum Permission { Perm_None, Perm_Read, Perm_Write, Perm_Execute };
```

加前缀这一招您多半也见过、也确实管用，不过它靠的是手工约定而不是语言机制。每个团队的前缀风格还都不一样，等团队多了，维护成本就跟着上去了。

### 毛病三：没法前向声明

第三样毛病的根源出在声明上。前向声明（forward declaration：只写名字、暂时不给完整定义的声明）在头文件管理里很常用，偏偏 C 风格 `enum` 用不了。原因在于它的底层类型（underlying type：枚举值实际按哪种整数类型来存）由编译器自行决定，编译器还没见到完整定义的时候，它的大小就定不下来了，所以没法前向声明。除非您手动指定底层类型，可那样一来就谈不上“纯 C 风格”了。头文件的依赖管理也因此很别扭。

```cpp
// status.h
enum Status { Ok, Error };  // 必须看到完整定义

// device.h
// enum Status;  // 编译错误！无法前向声明
class Device {
public:
    Status get_status() const;  // 必须包含 status.h
};
```

咱们把三条毛病摆在一块儿看，基本就是类型安全的反面教材。而 `enum class` 对每一条毛病，都给出了对症的修法。

## enum class 的三个改进

### 作用域隔离

头一样改进是作用域隔离：`enum class` 的枚举值不再泄漏到外部作用域，您想用哪个值，都得通过 `EnumName::Value` 的写法来访问：

```cpp
enum class Color { Red, Green, Blue };
enum class Fruit { Apple, Orange, Banana };

Color c = Color::Red;   // 正确
// Color c = Red;        // 编译错误！Red 不在外部作用域
// Fruit f = Color::Red; // 编译错误！类型不匹配
```

`Color::Red` 和 `Fruit::Apple` 从此就各归各的了，撞名或者混用再也发生不了。而跨类型的每一种误用，编译器在编译期就能替您拦下来。

### 禁止隐式转换

第二样改进对准的是隐式转换：`enum class` 不再隐式转换成任何整数类型了。您要转成整数，就得显式写 `static_cast` 这样的转换：

```cpp
enum class Color : uint8_t { Red, Green, Blue };

// int x = Color::Red;                          // 编译错误！
int x = static_cast<int>(Color::Red);           // OK，显式转换

void paint(Color c);
paint(Color::Red);      // OK
// paint(0);             // 编译错误！
// paint(static_cast<Color>(0));  // OK 但不推荐——绕过类型检查
```

您可能会觉得每次都写 `static_cast` 太麻烦。笔者倒愿意为这点麻烦多打几个字。某个地方需要把枚举值当整数用，您就必须显式写出来。这意味着您在那个位置做了一个有意识的决定，而不是无意中被编译器放过了。

咱们把前面的反例与修正写法并排成一张图，左边是老 `enum` 的静默放行，右边是 `enum class` 的编译期拦截：

![C 风格 enum 隐式转换放行与 enum class 编译期拦截对比](./01-enum-class-compare.drawio)

### 指定底层类型与前向声明

第三样改进是底层类型可以指定了：您不写的时候默认也是 `int`。等您把底层类型定下来，编译器只看声明就知道它的大小了，前向声明跟着就可行了：

```cpp
// status.h —— 前向声明
enum class Status : uint8_t;

// device.h —— 只需要前向声明
class Device {
public:
    Status get_status() const;
    void set_status(Status s);
};

// status.cpp —— 完整定义
enum class Status : uint8_t { kOk = 0, kError = 1, kBusy = 2 };
```

咱们在头文件里只需要一句前向声明，完整定义挪进了 `.cpp` 文件，头文件之间的循环依赖就这么断开了。做嵌入式的时候，您还可以把底层类型指定成 `uint8_t`，让枚举变量稳稳地只占一个字节：

```cpp
enum class SensorState : uint8_t {
    kOff = 0,
    kInit = 1,
    kReady = 2,
    kError = 3
};

static_assert(sizeof(SensorState) == 1, "SensorState should be 1 byte");
```

## 位运算与 enum class

位标志（bitmask：把每一个二进制位当成一个独立的开关来用）在 C 风格的代码里非常常见，咱们拿枚举值拼权限更是常事：

```cpp
// C 风格：天然支持位运算（因为隐式转换成 int）
enum Permission { Read = 1, Write = 2, Execute = 4 };
int perms = Read | Write;  // OK
```

`enum class` 把隐式转换禁了，所以 `Color::Red | Color::Green` 这样的写法直接编不过。咱们想让 `Permission` 支持位运算，运算符就得自己动手重载了：

```cpp
#include <type_traits>

enum class Permission : uint32_t {
    kNone    = 0,
    kRead    = 1 << 0,
    kWrite   = 1 << 1,
    kExecute = 1 << 2
};

// 辅助函数：枚举值到底层类型的转换
template <typename E>
constexpr auto to_underlying(E e) noexcept
{
    return static_cast<std::underlying_type_t<E>>(e);
}

constexpr Permission operator|(Permission a, Permission b) noexcept
{
    return static_cast<Permission>(to_underlying(a) | to_underlying(b));
}

constexpr Permission operator&(Permission a, Permission b) noexcept
{
    return static_cast<Permission>(to_underlying(a) & to_underlying(b));
}

constexpr Permission operator^(Permission a, Permission b) noexcept
{
    return static_cast<Permission>(to_underlying(a) ^ to_underlying(b));
}

constexpr Permission operator~(Permission a) noexcept
{
    return static_cast<Permission>(~to_underlying(a));
}

constexpr Permission& operator|=(Permission& a, Permission b) noexcept
{
    a = a | b;
    return a;
}

constexpr Permission& operator&=(Permission& a, Permission b) noexcept
{
    a = a & b;
    return a;
}

// 辅助判断：是否有任何标志位被设置
constexpr bool has_any_flag(Permission flags) noexcept
{
    return to_underlying(flags) != 0;
}

// 辅助判断：是否包含特定标志位
constexpr bool has_flag(Permission flags, Permission flag) noexcept
{
    return to_underlying(flags & flag) != 0;
}
```

咱们接着看用起来的样子：

```cpp
Permission user_perms = Permission::kRead | Permission::kWrite;

if (has_flag(user_perms, Permission::kWrite)) {
    // 用户有写权限
}

user_perms |= Permission::kExecute;  // 添加执行权限
user_perms &= ~Permission::kWrite;   // 移除写权限
```

长是长了点，毕竟六个运算符全得咱们亲手写。换来的东西也很实在：您没法再把 `Permission` 和 `Color` 的值混在一起做位运算了。到了实际项目里，大家一般会把运算符收进通用的头文件，复用的时候靠模板或者宏。

写 `to_underlying` 的时候您可能已经想到了，这样的辅助函数标准库迟早会有的。它真的来了：`std::to_underlying` 已经在 C++23 里正式进入了标准库，上面的手写版本可以直接换成 `<utility>` 里的它。位运算符这块标准库眼下没有现成的对应物，所以手动重载运算符，仍然是最主流的做法。

> 咱们也顺带提一句，位标志专用的类型包装器 `std::flags` 还停在提案阶段没进来呢。

## switch 匹配与编译器警告

咱们把 `enum class` 和 `switch` 放在一起用，配合是相当顺的。`enum class` 的值都得走限定名（就是前面一直写的 `EnumName::Value` 那样的带前缀完整写法），编译器因此知道全部可能的取值，您漏写分支的时候，它就能给出警告了：

```cpp
enum class NetworkState : uint8_t {
    kDisconnected,
    kConnecting,
    kConnected,
    kError
};

std::string_view to_string(NetworkState state)
{
    switch (state) {
    case NetworkState::kDisconnected: return "disconnected";
    case NetworkState::kConnecting:   return "connecting";
    case NetworkState::kConnected:    return "connected";
    // 如果缺少 kError 分支，-Wswitch 会发出警告
    }
    return "unknown";
}
```

笔者的建议很直接：**拿 `enum class` 做 `switch` 的时候不要写 `default` 分支**。道理是这样的：写了 `default`，编译器就当您把“其他”情况全处理完了，`-Wswitch` 警告也就跟着失效了。可您要不写，以后新增枚举值的时候，编译器就会在所有漏掉的 `switch` 处给出警告，bug 也就被拦在了编译期。

对应的编译器选项是 GCC/Clang 的 `-Wswitch`。GCC 要等到您打开 `-Wall` 才会带上它，Clang 那边默认就是开的。更严格的还有 `-Wswitch-enum`（写了 `default` 也照样警告）。您把选项加进项目的 `CMakeLists.txt`，就是个不错的工程实践。

## C++20 的 using enum

作用域隔离是个不折不扣的好东西，不过您要是在一个函数里翻来覆去地用同一个枚举，`EnumName::` 就得一遍一遍地写，读着也确实够啰嗦的。C++20 新增了 `using enum` 声明，一口气能把某个枚举的值全部引进当前作用域：

```cpp
enum class TokenType {
    kNumber, kString, kIdentifier,
    kPlus, kMinus, kStar, kSlash,
    kLeftParen, kRightParen, kEof
};

std::string_view token_to_string(TokenType type)
{
    // 把所有枚举值引入函数作用域
    using enum TokenType;

    switch (type) {
    case kNumber:     return "number";
    case kString:     return "string";
    case kIdentifier: return "identifier";
    case kPlus:       return "+";
    case kMinus:      return "-";
    case kStar:       return "*";
    case kSlash:      return "/";
    case kLeftParen:  return "(";
    case kRightParen: return ")";
    case kEof:        return "eof";
    }
    return "unknown";
}
```

`using enum` 的生效范围只到当前块（花括号以内），出了块就没了，所以外部作用域不会被污染。咱们还能把它用进类定义里：

```cpp
class Lexer {
public:
    using enum TokenType;  // 所有枚举值成为类的成员

    TokenType next_token();
    bool is_operator(TokenType t);
};
```

这里有一个容易出问题的地方，您动手以前要心里有数：`using enum` 会把所有枚举值都一股脑地引进当前作用域。两个枚举要是有同名的值，又被咱们同时 `using enum`，冲突就来了。所以您在用以前，得确认枚举全部的值，还有它们跟当前作用域里的名字不冲突。

## 实战应用：状态机与错误码

### 状态机

您在嵌入式和协议解析里会反复见到状态机，算是最常见的模式之一。咱们用 `enum class` 表示状态、拿 `switch` 写状态转移，清晰和安全都占上了：

```cpp
#include <cstdio>

enum class DeviceState : uint8_t {
    kIdle,
    kInitializing,
    kRunning,
    kSuspending,
    kError
};

class DeviceController {
public:
    void on_event(const char* event)
    {
        switch (state_) {
        case DeviceState::kIdle:
            if (is_start(event)) {
                state_ = DeviceState::kInitializing;
                std::printf("State: Idle -> Initializing\n");
                do_init();
            }
            break;
        case DeviceState::kInitializing:
            if (is_init_done(event)) {
                state_ = DeviceState::kRunning;
                std::printf("State: Initializing -> Running\n");
            } else if (is_error(event)) {
                state_ = DeviceState::kError;
                std::printf("State: Initializing -> Error\n");
            }
            break;
        case DeviceState::kRunning:
            if (is_stop(event)) {
                state_ = DeviceState::kSuspending;
                std::printf("State: Running -> Suspending\n");
            } else if (is_error(event)) {
                state_ = DeviceState::kError;
                std::printf("State: Running -> Error\n");
            }
            break;
        case DeviceState::kSuspending:
            if (is_suspend_done(event)) {
                state_ = DeviceState::kIdle;
                std::printf("State: Suspending -> Idle\n");
            }
            break;
        case DeviceState::kError:
            if (is_reset(event)) {
                state_ = DeviceState::kIdle;
                std::printf("State: Error -> Idle\n");
            }
            break;
        }
    }

    DeviceState get_state() const noexcept { return state_; }

private:
    DeviceState state_ = DeviceState::kIdle;

    void do_init() { /* ... */ }

    static bool is_start(const char* e)      { return e[0] == 'S'; }
    static bool is_init_done(const char* e)  { return e[0] == 'D'; }
    static bool is_stop(const char* e)       { return e[0] == 'T'; }
    static bool is_suspend_done(const char* e) { return e[0] == 's'; }
    static bool is_error(const char* e)      { return e[0] == 'E'; }
    static bool is_reset(const char* e)      { return e[0] == 'R'; }
};
```

代码的好处在您新增状态时才体现出来：哪天给 `DeviceState` 加了一个 `kPaused`，编译器就会在所有缺这个分支的 `switch` 处发出警告，前提还是您没写 `default`。状态转移的逻辑一条都漏不掉了。

### 错误码

咱们拿 `enum class` 做错误码，比 `#define` 和裸 `int` 都安全多了：

```cpp
#include <string_view>

enum class ErrorCode : int {
    kOk = 0,
    kInvalidArgument = 1,
    kNotFound = 2,
    kPermissionDenied = 3,
    kTimeout = 4,
    kInternalError = 5
};

struct Result {
    ErrorCode code;
    std::string_view message;

    bool is_ok() const noexcept { return code == ErrorCode::kOk; }
};

Result open_file(const char* path)
{
    if (!path || path[0] == '\0') {
        return {ErrorCode::kInvalidArgument, "path is empty"};
    }
    // ... 实际的文件打开逻辑
    return {ErrorCode::kOk, "success"};
}
```

好处也是很直接：调用方没法随手塞一个 `42` 进去当错误码了，能用的只有 `ErrorCode` 类型的值。这样的检查动作很简单，大项目里却实打实帮您省下大把调试时间。

## C 与 C++ 接口互操作

实际项目里也少不了跟 C 接口打交道的场景：您手上写的 C++ 代码用的是 `enum class`，底下对接的 C 库要的却是 `int` 或者 `uint32_t`。两边对不上的时候，就得显式转换了：

```cpp
extern "C" void hal_set_mode(uint8_t mode);

enum class HalMode : uint8_t {
    kSleep = 0,
    kNormal = 1,
    kBoost = 2
};

void set_device_mode(HalMode mode)
{
    // enum class -> 底层类型 -> C 接口
    hal_set_mode(static_cast<uint8_t>(mode));
}
```

您要是经常做转换，`to_underlying`（或者 C++23 的 `std::to_underlying`）能帮您省下几行 `static_cast`。不过按笔者的经验，转换一般集中在接口层（adapter：专门跟外部接口对接的那一层），业务逻辑里倒是难得见着几处，所以代码量并不大。

## 在线运行

咱们光看不练可不算数，您到下面的例子里亲手跑一遍，把 C 风格 `enum` 的问题和强类型的改进都亲眼验证一下：

<OnlineCompilerDemo
  title="enum class：强类型枚举与类型安全"
  source-path="code/examples/vol2/10_enum_class.cpp"
  description="在线运行并观察 C 风格 enum 的隐式转换问题和 enum class 的类型安全改进。"
  allow-run
/>

## 参考资源

- [cppreference: Enumeration declaration](https://en.cppreference.com/w/cpp/language/enum)
- [cppreference: std::to_underlying (C++23)](https://en.cppreference.com/w/cpp/utility/to_underlying)
- [C++20 using enum (P1099R5)](https://en.cppreference.com/w/cpp/language/enum#Using-enum-declaration)
- [C++ Core Guidelines: Enum.2](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#enum2-use-enumerations-to-represent-sets-of-related-named-constants)
