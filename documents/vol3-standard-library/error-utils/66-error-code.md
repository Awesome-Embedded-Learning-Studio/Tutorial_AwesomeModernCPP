---
chapter: 7
cpp_standard:
- 11
- 17
description: 讲透 std::error_code 的 error+category 双层结构、errc 为何是 condition 而非 code、system_error 怎么把错误码包成异常，以及从零搭一套自定义 category 让自定义错误码无缝融入标准体系
difficulty: intermediate
order: 66
platform: host
prerequisites:
- "expected：把错误提升为类型"
- "variant：类型安全的联合体"
reading_time_minutes: 16
related:
- "expected：把错误提升为类型"
- "filesystem 与路径操作"
tags:
- host
- cpp-modern
- intermediate
- 基础
title: error_code：错误码体系与自定义 category
---

# error_code：错误码体系与自定义 category

写 C++ 你迟早要面对一个问题:一个函数失败了,怎么把"失败"这件事告诉调用方。语言给了三条路——返回码、`std::error_code`、异常。这一篇我们不讲异常机制的内部原理(那是另一卷的事),只聚焦中间这条路:`<system_error>` 那套 `error_code` / `error_category` 体系到底是怎么设计的、为什么这么设计,以及怎么把自己模块的错误码无缝塞进这套体系。

先把动机说清楚。C 时代的 `errno` 大家都熟:调用一个系统接口,失败了去读全局的 `errno` 拿一个整数,再用 `strerror(errno)` 查一句话描述。能用,但坑也明显——`errno` 是全局可变状态(线程局部存储救了一半,但语义还是别扭)、错误号是个裸 `int`(编译器不知道 `2` 和 `99` 哪个是错误码哪个是普通返回值)、不同库的错误号还会撞车。C++11 的 `<system_error>` 就是来收拾这个烂摊子的:它没抛弃 `errno` 的"整数错误号"这个低成本模型,而是在外面套了两层结构,把错误号**分类**、**类型化**,还能让自定义错误码和系统错误码用同一套接口。

## 三条错误处理路的取舍

写一个会失败的函数,你大概会在三个选项里选:

- **裸返回码**:函数返回 `int`,0 是成功、非 0 是错误号。最朴素、零开销。问题是调用方拿到一个 `int`,编译器根本分不清它是结果还是错误码,忘判就裸奔;而且 `int` 没分类,跨模块的 `2` 和 `2` 可能完全不是一个意思。
- **异常**:`throw` 一个对象,控制流隐式跳到最近的 `catch`。优点是"成功路径"的代码非常干净,错误沿着调用栈自动往上冒;代价是有运行时开销(即便不抛,某些 ABI 也有 table 开销,而且异常对象的构造/栈展开不便宜),还把"可能抛异常"这个事实藏在了签名里(C++ 没有强制的 `throws` 声明)。
- **`std::error_code`**:函数返回一个轻量对象(本质就是一个 `int` 加一个指向 category 的指针),里面装着"错误号 + 这个号属于哪套体系"。它是显式的——返回类型上就写着 `error_code`,调用方一眼知道要判;它也是零开销的——不抛异常、不栈展开、对象就 16 字节,塞进 `expected<T, error_code>` 里和塞个 `int` 差不多。

这条表格把三者的取舍摆开:

| 维度 | 裸返回码 | `error_code` | 异常 |
|---|---|---|---|
| 显式性 | 差(`int` 看不出) | 好(类型即文档) | 差(藏在签名里) |
| 运行时开销 | 0 | 接近 0(16 字节对象) | 有(即便不抛也有,抛了更贵) |
| 错误分类 | 无 | 有(category) | 有(异常类型) |
| 跨模块/跨库 | 各搞各的 | 标准统一体系 | 各搞各的 |
| 失败可忽略 | 容易忘判 | 容易忘判(`bool` 救一半) | 不能忽略(不 catch 就崩) |

`error_code` 的甜区是那种"失败是常态、是预期的、在热路径上"的场景:文件打不开、网络超时、配置项不合法。这类错误你既不想要异常的开销,又想要一套跨模块统一的、可比较、可查描述的错误体系——这正是 `<system_error>` 拍出来干的事。

## `error_code` 的构成:value + category,两者分离

先看 `error_code` 长什么样。它内部就两样东西:

```cpp
// Standard: C++11
// std::error_code 的语义(简化)
class error_code {
    int value_;                  // 错误号(整数值)
    const error_category* cat_;  // 指向某个 category 单例的指针
};
```

一个 `int` 装错误号,一个指针指向它所属的 `error_category`。关键设计就在这个**分离**:错误号是个裸值,但单独一个"2"没有意义——它可能是 POSIX 的 `ENOENT`(文件不存在),也可能是你自定义模块里的"鉴权失败"。只有配上"它属于哪套错误号体系",这个 `2` 才有确定的含义。`error_category` 就是"错误号体系"这个概念的载体。

`error_category` 是个抽象基类,每个具体 category 是个单例,提供三个核心能力:

- `name()` —— 这套错误号叫什么(比如 `"system"`、`"generic"`、`"my-app"`)。
- `message(ev)` —— 把错误号翻译成人话。
- `default_error_condition(ev)` / `equivalent(...)` —— 跨 category 比较的桥梁(下面专门讲)。

标准库自带两个现成的 category:

- `std::system_category()` —— 对应当前平台的系统错误号(`errno` 那一套,`ENOENT`/`EACCES`/`ETIMEDOUT` ...)。
- `std::generic_category()` —— 对应 POSIX 通用错误号,跨平台稳定(同一套 `errc` 枚举值)。

来跑一个最小的例子,把 `errno` 风格的失败包成 `error_code`:

```cpp
// Standard: C++11
#include <system_error>
#include <iostream>
#include <cstring>

int main()
{
    auto* fp = std::fopen("/no/such/file/here", "r");
    if (fp == nullptr) {
        int e = errno;                                   // 原始错误号
        std::error_code ec{e, std::system_category()};   // 包成 error_code

        std::cout << "value:    " << ec.value() << '\n';
        std::cout << "category: " << ec.category().name() << '\n';
        std::cout << "message:  " << ec.message() << '\n';
    }
    return 0;
}
```

`g++ -std=c++20 -O2`(本机 GCC 16.1.1)跑出来:

```text
value:    2
category: system
message:  No such file or directory
```

那个 `2` 就是 `ENOENT` 的值,`system_category` 知道在当前平台下 `2` 对应"No such file or directory"。注意我们一行 `strerror` 都没写,`message()` 替我们查了——这就是 category 把"号到描述"的映射封装起来的好处。

`error_code` 还有个 `operator bool`:错误号非 0 就返回 `true`(表示有错)。默认构造的 `error_code` 错误号是 0、`bool` 是 `false`(无错)。所以判错就一行 `if (ec)`:

```text
sizeof(error_code)      = 16
default error_code bool = 0
```

16 字节,就是一个 `int`(补齐到 8)加一个指针。这么轻的东西,在函数间传来传去、塞进 `expected` 里,几乎感觉不到开销。

## `errc`、`make_error_code` 和跨 category 比较

到这里有个反直觉的点,新手基本都会踩:标准库给的那套跨平台错误码枚举 `std::errc`,到底是"错误码"还是"错误条件"?

答案是后者。`errc::no_such_file_or_directory` 这种枚举值,**不是一个 `error_code`,而是一个 `error_condition`**。这两个概念要分清:

- **`error_code`** —— 一个**具体的、带平台语义**的错误号。`system_category` 下的 `2` 在 Linux 上是 `ENOENT`,换个平台值可能不同。
- **`error_condition`** —— 一个**抽象的、可移植**的错误条件。`generic_category` 下的 `no_such_file_or_directory` 不管你在哪个平台,语义都一样。

`errc` 是 `error_condition` 的枚举,标准库用 `is_error_condition_enum<std::errc>` 把它标成了条件枚举。这件事我们用 type trait 直接验:

```cpp
// Standard: C++11
std::cout << "is_error_code_enum<errc>      = "
          << std::is_error_code_enum<std::errc>::value << '\n';
std::cout << "is_error_condition_enum<errc> = "
          << std::is_error_condition_enum<std::errc>::value << '\n';
```

```text
is_error_code_enum<errc>      = 0
is_error_condition_enum<errc> = 1
```

后果很直接:`std::error_code ec = std::errc::timed_out;` **编不过**。`errc` 不是 code 枚举,没有那条启用 error_code 隐式构造的路径。要把 `errc` 变成 `error_code`,得显式调 `std::make_error_code`:

```cpp
// Standard: C++11
auto ec = std::make_error_code(std::errc::timed_out);  // 造出来是 generic_category
std::cout << "value=" << ec.value()
          << " cat=" << ec.category().name() << '\n';
// value=110 cat=generic   (110 就是 POSIX 的 ETIMEDOUT)
```

那 `errc` 既然是 condition,它在哪用得上?答案是**比较**。`error_code` 重载了 `==`，可以直接和一个 `errc` 比:

```cpp
// Standard: C++11
int e = ENOENT;   // 2
std::error_code sys_ec{e, std::system_category()};
auto generic_ec = std::make_error_code(std::errc::no_such_file_or_directory);

std::cout << "sys_ec == generic_ec (code==code) ? "
          << (sys_ec == generic_ec) << '\n';
std::cout << "sys_ec == errc::no_such_file_or_directory (code==errc) ? "
          << (sys_ec == std::errc::no_such_file_or_directory) << '\n';
```

```text
sys_ec == generic_ec (code==code) ? 0
sys_ec == errc::no_such_file_or_directory (code==errc) ? 1
```

注意这两个结果的差别,这正是 `default_error_condition` 存在的全部理由:

- `sys_ec == generic_ec` 比的是"两个 `error_code` 完全相等"(value 相等 **且** category 相同)。一个是 `system`、一个是 `generic`,category 不同,所以 `0`(不等)。
- `sys_ec == errc::...` 走的是另一条路:`errc` 先隐式构造成 `error_condition`,然后比较时,`system_category` 的 `default_error_condition(2)` 会把这个系统错误号**映射**成对应的 `generic` 条件——映射出来的正好是 `no_such_file_or_directory`,于是相等。

换句话说,`default_error_condition` 就是 category 自己声明"我这个具体错误号,等价于哪个通用错误条件"。它让"平台相关错误码"和"可移植错误条件"之间有了桥梁:你在 Linux 上拿到 `system_category` 的 `ENOENT`,跟别人手里 `generic_category` 的 `no_such_file_or_directory` 比一把,是相等的——因为它们语义上是同一回事。这就是为什么实践中判错几乎都是 `ec == std::errc::xxx` 这种写法,而不是去比另一个 `error_code`:前者跨 category、跨平台,后者绑死在同一个 category 上。

::: warning errc 不是 error_code
`errc` 是 `error_condition` 枚举,不是 `error_code` 枚举。`std::error_code ec = std::errc::x;` 编不过,得 `std::make_error_code(std::errc::x)`。但 `ec == std::errc::x` 能编过且工作正常——`==` 那条路径走 `default_error_condition` 等价性,不要求 `errc` 是 code 枚举。这个区别是 `<system_error>` 设计的核心之一,记混了要么编译失败,要么写出"看起来对、其实从不命中"的比较。
:::

## `system_error`:把 error_code 包成异常

`error_code` 是显式的返回路径,但有时候你还是想要异常的"自动往上冒"语义——比如在深层调用栈里,一个底层的 `error_code` 失败了你不想一层层手动传上去,想直接 throw。`<system_error>` 给了一个现成的异常类 `std::system_error`,它内部包了一个 `error_code`:

```cpp
// Standard: C++11
#include <system_error>
#include <iostream>
#include <cstring>

int main()
{
    try {
        errno = ENOENT;
        throw std::system_error(
            std::error_code{ENOENT, std::system_category()},
            "打开配置文件失败");
    } catch (const std::system_error& e) {
        std::cout << "what():     " << e.what() << '\n';
        std::cout << "code value: " << e.code().value() << '\n';
        std::cout << "code msg:   " << e.code().message() << '\n';
        std::cout << "category:   " << e.code().category().name() << '\n';
    }
    return 0;
}
```

```text
what():     打开配置文件失败: No such file or directory
code value: 2
code msg:   No such file or directory
category:   system
```

`what()` 把我们传的上下文字符串和 `error_code::message()` 拼在了一起,排错时一目了然。`catch` 到之后还能用 `e.code()` 把里面的 `error_code` 取出来,继续走那一套 `value()/category()/== errc` 的逻辑。这就是 `error_code` 和异常的衔接点:你可以在底层用 `error_code` 这种低成本方式返回,在边界上决定"这个错值得中断控制流",就 `throw system_error{ec, "..."}` 把它升级成异常;反过来 `catch` 到 `system_error` 也能拿回 `error_code`。两条路是互通的,不是非此即彼。

标准库里这一招用得最多的就是 `<filesystem>`(C++17)。`std::filesystem` 的每个可能失败的函数都有两个重载:一个抛 `filesystem_error`(它继承自 `system_error`),另一个接受一个 `std::error_code&` 出参、不抛:

```cpp
// Standard: C++17
#include <filesystem>
#include <system_error>
#include <iostream>

namespace fs = std::filesystem;

int main()
{
    std::error_code ec;
    fs::file_size("/no/such/path", ec);   // 不抛重载:错误塞进 ec
    if (ec) {
        std::cout << "file_size 失败: " << ec.message()
                  << " (cat=" << ec.category().name() << ")\n";
    }
    return 0;
}
```

```text
file_size 失败: No such file or directory (cat=system)
```

调用方自己挑:想要异常中断就调不传 `ec` 的版本,想自己处理、不想被异常打断控制流就传 `ec`。这套"双 API"模式在 `filesystem`、`asio` 里随处可见,本质上就是把 `error_code` 当成了"异常的可选替代",决定权交给调用者。我们下面要讲的自定义 category,正是为了让自己的模块也能融入这套"错误码可以无处不在"的体系。

## 自定义 category:从零搭一套错误码体系

这是本篇的核心。标准库的 `error_code` 体系设计得最妙的一点是:它不是只给系统错误用的——任何模块都可以注册自己的 `error_category`,自己定义一套错误号,然后用 `error_code` 这个统一类型装载、用同一套 `message()`/`== errc` 接口操作。你的模块错误和 POSIX 错误在类型上是平权的。

我们现在就从零搭一套。假设在写一个网络登录模块,错误有这么几种:网络断了、鉴权失败、超时、报文格式错。目标:让 `login()` 返回 `std::error_code`,调用方能用 `ec == MyErrc::kAuthFailed` 判断,也能用 `ec.message()` 拿到中文描述,甚至能让 `kTimeout` 自动等价于标准的 `errc::timed_out`。

### 第一步:定义错误码枚举

```cpp
// Standard: C++11
#include <system_error>
#include <string>
#include <iostream>

enum class MyErrc {
    kSuccess     = 0,
    kNetworkDown = 10,
    kAuthFailed  = 11,
    kTimeout     = 12,
    kBadPayload  = 13,
};
```

注意值从 0 开始、0 留给"成功"——这和 `error_code::operator bool`(非 0 即有错)的约定对齐。

### 第二步:特化 `is_error_code_enum`,启用隐式转换

光有个 enum 还不够,标准库不知道它是个"错误码枚举"。得特化 `std::is_error_code_enum` 把它标记为 `true`,这样 `error_code` 才会启用那条从枚举隐式构造的路径:

```cpp
// Standard: C++11
namespace std {
template <>
struct is_error_code_enum<MyErrc> : true_type {};
}  // namespace std
```

这一步是连接自定义 enum 和 `error_code` 体系的"开关"。前面我们验过 `std::errc` 这个开关是 `0`(它是 condition 枚举),所以 `errc` 不能隐式构 `error_code`;而我们的 `MyErrc` 特化成 `true`,所以 `MyErrc` 可以。

### 第三步:写自定义 category 单例

category 是个继承 `std::error_category` 的类,实现那几个虚函数。它得是个单例——因为 `error_code` 内部存的是指向 category 的指针,比较两个 `error_code` 是否同 category 比的就是指针,所以每个 category 全进程只能有一个实例:

```cpp
// Standard: C++11
class MyCategory : public std::error_category {
public:
    const char* name() const noexcept override {
        return "my-app";
    }

    std::string message(int ev) const override {
        switch (static_cast<MyErrc>(ev)) {
            case MyErrc::kSuccess:     return "成功";
            case MyErrc::kNetworkDown: return "网络不可达";
            case MyErrc::kAuthFailed:  return "鉴权失败";
            case MyErrc::kTimeout:     return "操作超时";
            case MyErrc::kBadPayload:  return "报文格式错误";
            default:                   return "未知错误";
        }
    }

    // 把自定义错误号映射到通用 error_condition,实现跨 category 等价
    std::error_condition default_error_condition(int ev) const noexcept override {
        switch (static_cast<MyErrc>(ev)) {
            case MyErrc::kTimeout:
                return std::make_error_condition(std::errc::timed_out);
            default:
                return {ev, *this};   // 其他用本 category 自身
        }
    }
};

// 单例工厂:全进程唯一实例
const std::error_category& my_category() {
    static MyCategory instance;
    return instance;
}
```

`message()` 把自己的错误号翻译成人话——这下 `ec.message()` 不用我们自己写了。`default_error_condition()` 这一步可选但关键:我们声明了"我的 `kTimeout` 等价于标准的 `errc::timed_out`"。后果是,调用方拿到一个 `MyErrc::kTimeout` 的 `error_code`,可以直接 `if (ec == std::errc::timed_out)` 判断——跨 category、跨"我的模块 vs 标准库",语义打通。

### 第四步:写 `make_error_code` 重载

开关打开了、category 写好了,最后一步是提供一个 `make_error_code(MyErrc)` 函数,告诉标准库"遇到 `MyErrc` 怎么造 `error_code`"。它靠 ADL(参数依赖查找)被找到,所以要么放在 `MyErrc` 所在的命名空间,要么放在 `std` 命名空间(官方推荐前者):

```cpp
// Standard: C++11
std::error_code make_error_code(MyErrc e) {
    return {static_cast<int>(e), my_category()};
}
```

这四步配齐,自定义错误码体系就通了。现在写一个用它的函数:

```cpp
// Standard: C++11
std::error_code login(bool network_ok, bool password_ok) {
    if (!network_ok) return MyErrc::kNetworkDown;   // 隐式转 error_code
    if (!password_ok) return MyErrc::kAuthFailed;
    return MyErrc::kSuccess;
}
```

注意 `return MyErrc::kNetworkDown;` 这一行——因为第二步的 `is_error_code_enum` 特化是 `true`,`error_code` 的 enabling 构造函数被激活,编译器会自动调第四步的 `make_error_code` 把它转成 `error_code`。**隐式转换在这里是工作的**,和 `errc` 的"不能隐式构 error_code"形成对比——这正是"code 枚举"和"condition 枚举"的区别落地。

跑一下完整的:

```cpp
// Standard: C++11
int main()
{
    auto ec1 = login(false, true);
    std::cout << "login(false,true):\n";
    std::cout << "  value    = " << ec1.value() << '\n';
    std::cout << "  category = " << ec1.category().name() << '\n';
    std::cout << "  message  = " << ec1.message() << '\n';
    std::cout << "  bool(ec) = " << static_cast<bool>(ec1) << " (非0=有错)\n";

    auto ec2 = login(true, false);
    std::cout << "\nlogin(true,false): " << ec2.message()
              << " (bool=" << static_cast<bool>(ec2) << ")\n";

    auto ec3 = login(true, true);
    std::cout << "login(true,true):  " << ec3.message()
              << " (bool=" << static_cast<bool>(ec3) << ")\n";

    std::cout << "\n--- 跨 category 等价性 ---\n";
    std::error_code tc{static_cast<int>(MyErrc::kTimeout), my_category()};
    std::cout << "kTimeout message: " << tc.message() << '\n';
    std::cout << "tc == errc::timed_out ? " << (tc == std::errc::timed_out)
              << "  (1=default_error_condition 映射后相等)\n";
    return 0;
}
```

`g++ -std=c++20 -O2 -Wall -Wextra` 编过,跑出来:

```text
login(false,true):
  value    = 10
  category = my-app
  message  = 网络不可达
  bool(ec) = 1 (非0=有错)

login(true,false): 鉴权失败 (bool=1)
login(true,true):  成功 (bool=0)

--- 跨 category 等价性 ---
kTimeout message: 操作超时
tc == errc::timed_out ? 1  (1=default_error_condition 映射后相等)
```

一套完全属于自己的错误码体系,就这样和标准库那套无缝对接了。复盘一下这四步分别在干什么:

1. **定义枚举** —— 把错误号确定下来,0 留给成功。
2. **特化 `is_error_code_enum`** —— 打开开关,告诉标准库"这个 enum 是 error_code 枚举"。
3. **写 category 单例** —— 提供 `name`/`message`/`default_error_condition`,把错误号的"含义"和"跨体系等价关系"封装起来。
4. **写 `make_error_code` 重载** —— 告诉标准库怎么从 enum 造 error_code,靠 ADL 被找到。

::: warning category 必须是单例
`error_code::operator==` 比较"是否同 category"时比的是指针。如果你的 category 不止一个实例,两个逻辑上"同一个 category"的 `error_code` 比出来就会是不等。所以 category 永远用一个函数内的 `static` 局部变量返回,保证全进程唯一。漏了这步,会出现"明明是同一个错误,比较却说不一样"的诡异 bug。
:::

## 和 `expected` 配合:错误码体系的现代用法

到这里你可能会想:`login()` 直接返回 `error_code`,那它成功时怎么把"登录成功的 token"这种结果传出去?`error_code` 只装错误,装不了值。这正是 `std::expected<T, E>` 的用武之地——把 `E` 设成 `error_code`,一个类型同时表达"成功的值"和"失败的错误码":

```cpp
// Standard: C++23
#include <expected>
#include <system_error>
#include <iostream>

std::expected<int, std::error_code> read_sensor(int id)
{
    if (id < 0) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    if (id > 100) {
        return std::unexpected(std::make_error_code(std::errc::result_out_of_range));
    }
    return id * 2;   // 成功:隐式构 expected
}

int main()
{
    if (auto r = read_sensor(5); r) {
        std::cout << "read_sensor(5) = " << *r << '\n';
    }
    if (auto r = read_sensor(-1); !r) {
        std::cout << "read_sensor(-1) 失败: " << r.error().message()
                  << " (cat=" << r.error().category().name() << ")\n";
    }
    if (auto r = read_sensor(200); !r) {
        std::cout << "read_sensor(200) 失败: " << r.error().message() << '\n';
    }
    return 0;
}
```

```text
read_sensor(5) = 10
read_sensor(-1) 失败: Invalid argument (cat=generic)
read_sensor(200) 失败: Numerical result out of range
```

`E = std::error_code` 这条路的甜处在于:你的错误类型是 16 字节的标准轻量对象(前面验过 `sizeof(error_code)=16`),和塞个 `int` 差不多,不会让 `expected` 变臃肿;同时 `r.error().message()` 能直接给出可读描述,`r.error() == std::errc::timed_out` 能跨体系判断。自定义模块的话,把 `E` 设成你自己的 category 的 `error_code` 即可——上面四步搭好的体系,原封不动塞进 `expected` 里用。

这里有个小坑再提醒一下:`std::unexpected(std::errc::x)` **不行**。因为 `errc` 是 condition 枚举,`unexpected(errc)` 会把错误槽的类型推导成 `errc` 而不是 `error_code`,和 `expected<T, error_code>` 对不上,编不过。包 `errc` 必须显式走 `std::make_error_code`。自定义的 `MyErrc` 倒是可以直接 `return std::unexpected(MyErrc::kBoom);`——因为它能隐式转 `error_code`(前面验过),转换会在 `unexpected` 构造时发生。code 枚举和 condition 枚举,在这里又分了一次家。

`expected` 的完整机制(构造、monadic 链式、和异常的性能对比)我们在第 64 篇已经拆透,这里只看它和 `error_code` 这一头怎么咬。一句话总结:**`expected<T, error_code>` 把"值或错误"的现代类型化错误处理,和 `<system_error>` 那套标准化、可分类、跨体系的错误码,焊在了一起**——这是如今 C++ 错误处理最干净的组合之一。

## 几个真实容易踩的点

这一路下来容易翻车的位置集中收一下,都是上面实测验证过的:

::: warning 别把 errc 当 error_code
`std::errc` 是 `error_condition` 枚举(`is_error_condition_enum=1`、`is_error_code_enum=0`),所以 `std::error_code ec = std::errc::x;` 编不过。要造 `error_code` 用 `std::make_error_code(std::errc::x)`,结果是 `generic_category` 下的 code。但 `ec == std::errc::x` 这种比较是 OK 的——走 `default_error_condition` 等价性,不要求 `errc` 是 code 枚举。
:::

::: warning code==code 比的是 value+category,不是语义
`error_code{ENOENT, system_category()} == make_error_code(errc::no_such_file_or_directory)` 结果是 `false`——一个是 `system`、一个是 `generic`,category 不同,直接判不等。语义上它们是同一回事,要这么比得用 `ec == errc::no_such_file_or_directory` 这条 condition 路径。别拿两个 `error_code` 直接 `==` 期望它做语义比较。
:::

::: warning category 必须单例,否则比较崩
`error_code` 比较"同 category"比的是指针。category 类要是没做成单例(比如每次返回临时对象),两个本应相等的 `error_code` 比出来会不等。永远用函数内 `static` 局部变量返回 category 引用。
:::

::: warning unexpected(errc) 编不过
`std::expected<T, std::error_code>` 里塞错误,`std::unexpected(std::errc::x)` 推导出的 `unexpected` 是 `unexpected<errc>`,和 `expected<T, error_code>` 类型不匹配。要 `std::unexpected(std::make_error_code(std::errc::x))`。自定义 code 枚举(如 `MyErrc`)可以省掉 `make_error_code`——它能隐式转 `error_code`。
:::

## 小结

`<system_error>` 这套体系,本质上是把 `errno` 那个"整数错误号"的低成本模型,外面套了一层"分类 + 类型化"的结构,让它既能零开销、又能跨模块统一、还能让自定义错误码无缝融入。几条关键结论收一下:

- **三层错误处理路**:裸返回码(最朴素、无分类)、`error_code`(显式、零开销、有分类、标准统一)、异常(隐式控制流、有开销、自动冒泡)。`error_code` 的甜区是"失败是常态、在热路径、想跨模块统一"。
- **`error_code` = value + category**:错误号是裸值,category 是单例、提供 `name`/`message`/`default_error_condition`。两者分离,让一个 `int` 配上"属于哪套体系"才有确定含义。标准自带 `system_category`(平台 errno)和 `generic_category`(POSIX 通用)。
- **`errc` 是 condition 不是 code**:`std::errc` 是 `error_condition` 枚举,不能隐式构 `error_code`(要 `make_error_code`),但 `ec == errc::x` 能工作——走 `default_error_condition` 等价性,这就是跨 category、跨平台比较的桥梁。
- **`system_error` 异常包 `error_code`**:底层用 `error_code` 低成本返回,边界上 `throw system_error{ec, "..."}` 升级成异常;`filesystem` 的"抛 / 不抛双 API"就是这个模式的标准实践。
- **自定义 category 四步走**:① 定义 enum(0 留成功);② 特化 `is_error_code_enum<E>` 为 `true`(打开隐式转换开关);③ 写 category 单例(`name`/`message`/可选 `default_error_condition`);④ 写 `make_error_code(E)` 重载(ADL 被找到)。配齐后自定义错误码和 POSIX 错误在类型上平权。
- **配 `expected<T, error_code>`**:把"值或错误"的类型化处理和标准化错误码焊在一起,16 字节的 `error_code` 当 `E` 几乎零开销。注意 `unexpected(errc)` 编不过、`unexpected(MyErrc)` 可以——又是 code 枚举和 condition 枚举的区别。

下一篇我们换个角度,看 `<system_error>` 之外的另一种错误处理范式——感兴趣的话可以回头读第 64 篇 `expected` 的完整机制,或者去 `<filesystem>` 那篇看这套"双 API"模式在真实标准库组件里是怎么落地的。

## 参考资源

- [cppreference: std::error_code](https://en.cppreference.com/w/cpp/error/error_code) —— value + category 结构、`operator bool`、比较语义
- [cppreference: std::error_category](https://en.cppreference.com/w/cpp/error/error_category) —— 抽象基类与 `name`/`message`/`default_error_condition`
- [cppreference: std::errc](https://en.cppreference.com/w/cpp/error/errc) —— POSIX 通用错误条件枚举(注意它是 `error_condition` 枚举)
- [cppreference: std::system_error](https://en.cppreference.com/w/cpp/error/system_error) —— 包 `error_code` 的异常类,`filesystem_error` 继承自它
- [cppreference: std::is_error_code_enum](https://en.cppreference.com/w/cpp/error/error_code/is_error_code_enum) —— 启用 enum 到 `error_code` 隐式构造的 trait(自定义 category 第二步)
