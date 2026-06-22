---
title: "单例模式:从注释约束到 Meyer's Singleton"
description: "从最原始的「写注释提醒」开始,一步步逼出线程安全的 Meyer's Singleton,顺手拆穿 DCLP 的老黄历,最后用依赖注入收尾"
chapter: 11
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 单例模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 18
related:
  - "工厂方法与抽象工厂"
prerequisites:
  - "Chapter 6: 类与对象"
---

# 单例模式:从注释约束到 Meyer's Singleton

## 我们到底在解决什么问题

我们先不急着上定义。想一个最常见的场景:程序里要读一份全局配置(`host`、`port`、`username`……),这份配置从启动到结束不变,而且全程序任何角落都可能想读它。你当然可以把配置塞进一个对象,然后把这个对象满世界传——但很快你就会烦:每个函数签名都要多挂一个 `Config&` 参数,层层透传,仅仅是为了让最底层的某个工具函数能读到一个 `port`。

单例模式要解决的就是这类需求:**保证一个对象在程序运行期间只有一个实例,并且提供一个全局访问点**。日志器、配置管理器、数据库连接池、设备驱动接口——它们都有「全局唯一」的天然诉求。

但「全局唯一」这四个字,在 C++ 里**并不是你声明了就成立的**。C++ 是一门特别爱做隐式操作的语言:只要你没明确封死,拷贝构造、赋值、移动,甚至一个不经意的按值传参,都能在背后偷偷造出第二个实例。所以我们真正要回答的问题是——**怎么用语言机制,而不是用人脑约定,去把「只能有一个」这件事钉死**。

接下来我们就一步步来,从最蠢的写法开始,看看每一步为什么不够,最后逼出一个现代 C++ 的标准答案。

## 第一步:最原始的写法——写注释(错误示范)

很多朋友第一次遇到「只能创建一次」的需求,下意识反应是这样的:

```cpp
struct GlobalOled {
    // You should only invoke the creation for once!!!
    GlobalOled();
};
```

打一堆感叹号,把约束写进注释里。说实话,这真的不是夸张,我确实在生产代码里见过这种写法。问题在于,这种约束是写给人看的,而**编译器根本不参与检查**。

我们假设人人在开发时都会认真读注释——但 C++ 会在你看不见的地方动手脚。比如有人在某个函数里为了方便,随手写了 `GlobalOled another = oled;`,这是一次再正常不过的拷贝构造,然而就在这一行,你的「全局唯一」就破功了。又或者有人把它按值塞进了一个容器、一个 `std::function` 的捕获,RAII 的初始化路径上随时可能触发拷贝或移动。注释挡不住任何一条。

所以这条路走不通。我们得让编译器替我们把守。

## 第二步:封死一切拷贝路径——`= delete`

既然坑在于「会偷偷被拷贝」,那最直接的办法就是把所有破坏唯一性的拷贝、移动路径**全部禁用掉**:

```cpp
struct GlobalOled {
public:
    // ...

private:
    GlobalOled();
    GlobalOled(const GlobalOled&) = delete;
    GlobalOled& operator=(const GlobalOled&) = delete;
    GlobalOled(GlobalOled&&) = delete;
    GlobalOled& operator=(GlobalOled&&) = delete;
};
```

`=`delete` 是 C++11 给我们的利器:被 delete 的函数仍然参与重载决议,一旦有人试图调用,编译器直接报错。这一步比注释强太多了——现在「不能拷贝」是编译期硬约束。

但事情到这里出现了新的矛盾:我们把构造函数也放进 `private` 了,目的是不让外部随手构造;可这一来,**谁都没法创建它了**,连那个「唯一实例」也造不出来。我们缺的是一个受控的入口:既不让外部随便 `new`,又得给外部一个拿到实例的办法。

## 第三步:私有构造 + 静态访问点——Meyer's Singleton

把问题收敛一下:构造的唯一性交给一个我们可控的入口函数,外部想用实例,只能走这个入口。而「入口内部如何保证只构造一次」,C++11 给了一个干净到近乎免费的答案——**函数内的 `static` 局部变量**:

```cpp
class GlobalOled {
public:
    static GlobalOled& get_instance() {
        static GlobalOled oled;  // 只在首次经过时初始化一次
        return oled;
    }

private:
    GlobalOled();
    GlobalOled(const GlobalOled&) = delete;
    GlobalOled& operator=(const GlobalOled&) = delete;
    GlobalOled(GlobalOled&&) = delete;
    GlobalOled& operator=(GlobalOled&&) = delete;
};
```

这段代码有个名字,叫 **Meyer's Singleton**(以 Scott Meyers 命名)。它的核心就一行 `static GlobalOled oled;`,但这一行背后的保证非常硬:从 C++11 起,标准明确规定——**如果多个线程同时首次进入这条声明,恰好只有一个线程会执行初始化,其余线程会被阻塞等待,直到初始化完成**([stmt.dcl],俗称 *magic statics*)。

这意味着什么?**单例的线程安全初始化,语言已经替我们保证了,我们一行锁都不用写。** 先别急着信,这里我们先验证一下。

## 这里先验证一下:magic statics 真的线程安全吗

口说无凭,我们写个小程序,让 500 个线程同时去抢这个 `get_instance()`,在构造函数里挂一个原子计数器,看看它到底被构造了几次:

```cpp
#include <atomic>
#include <thread>
#include <vector>
#include <iostream>

class MeyersSingleton {
public:
    static MeyersSingleton& instance() {
        static MeyersSingleton s;  // C++11 [stmt.dcl]: 线程安全地初始化一次
        return s;
    }
    static inline std::atomic<int> construct_count{0};

private:
    MeyersSingleton() { ++construct_count; }
    MeyersSingleton(const MeyersSingleton&) = delete;
    MeyersSingleton& operator=(const MeyersSingleton&) = delete;
};

int main() {
    constexpr int kThreadCount = 500;
    std::vector<std::thread> ts;
    ts.reserve(kThreadCount);
    for (int i = 0; i < kThreadCount; ++i) {
        ts.emplace_back([] { auto& s = MeyersSingleton::instance(); (void)s; });
    }
    for (auto& t : ts) t.join();
    std::cout << "construct_count = " << MeyersSingleton::construct_count
              << " (expect 1)\n";
}
```

编译跑一下(`-O2` 优化也开着,故意让竞争更激烈):

```sh
$ g++ -std=c++23 -O2 -pthread singleton_verify.cpp -o singleton_verify
$ for i in 1 2 3 4 5; do ./singleton_verify; done
construct_count = 1 (expect 1)
construct_count = 1 (expect 1)
construct_count = 1 (expect 1)
construct_count = 1 (expect 1)
construct_count = 1 (expect 1)
```

连跑五次,500 个线程并发抢,`construct_count` 稳稳地是 1。这就是 magic statics 的承诺——你不用写锁,不用 `call_once`,不用提心吊胆,语言层面已经把「只初始化一次」这件事承包了。**在现代 C++ 里,写单例的第一选择就是 Meyer's Singleton,没有任何理由去手写更复杂的东西。**

## 踩坑预警:DCLP 的老黄历

::: warning 踩坑预警
如果你在翻一些 C++11 之前的老资料,大概率会看到一个叫 **DCLP(Double-Checked Locking Pattern,双重检查锁)** 的写法。网上很多博客还在转它,甚至有些会写成下面这样——**这个写法有问题,别照抄**:

```cpp
// ⚠️ 反面教材:memory_order_consume 在这里不靠谱
static GlobalOled& get_instance() {
    GlobalOled* p = oled.load(std::memory_order_consume);
    if (p) return *p;
    std::lock_guard<std::mutex> _(instance_lock);
    p = oled.load(std::memory_order_consume);
    if (!p) {
        p = new GlobalOled;
        oled.store(p, std::memory_order_release);
    }
    return *p;
}
```

问题出在 `memory_order_consume` 上。consume 的本意是「只保护有依赖关系的访问」,听起来对 DCLP 够用了,但它在 C++17 之后被标准大幅弱化、实际上几乎所有主流编译器都把它**直接降级成 acquire** 处理——也就是说,你以为写的是 consume 的弱保证,运行时拿到的是 acquire 的强保证,语义和你写的对不上,可移植性稀烂。手写 consume 至今仍是个雷区。
:::

如果非要手写 DCLP(我再强调一遍,**现代 C++ 用 magic statics 就够了,不需要手写**),正确的内存序是 **acquire / release**:

```cpp
class DclpSingleton {
public:
    static DclpSingleton* instance() {
        auto* p = ptr_.load(std::memory_order_acquire);  // 第一次检查(无锁)
        if (!p) {
            std::lock_guard<std::mutex> lk(mtx_);
            p = ptr_.load(std::memory_order_relaxed);    // 第二次检查(持锁)
            if (!p) {
                p = new DclpSingleton();
                ptr_.store(p, std::memory_order_release);  // 发布
            }
        }
        return p;
    }

private:
    DclpSingleton() = default;
    DclpSingleton(const DclpSingleton&) = delete;
    DclpSingleton& operator=(const DclpSingleton&) = delete;
    static inline std::mutex mtx_;
    static inline std::atomic<DclpSingleton*> ptr_{nullptr};
};
```

acquire 保证读到非空指针时,它指向的对象已经完整构造好;release 保证写回指针时,对象的构造对其他线程可见。同样验证一下,两个线程并发抢,拿到的是不是同一个实例:

```sh
$ ./singleton_verify
Meyers:  construct_count = 1 (expect 1)
DCLP:    same instance = true (expect true)
```

结论是好的。但我还是要再唠叨一遍:**这段 DCLP 代码只是给你看清「老办法正确长什么样」,它不是让你去用的**。Meyer's Singleton 一行 `static` 就把 DCLP 那一坨全替掉了,而且没有堆分配(`new`)、没有裸指针、没有销毁顺序问题。DCLP 是 C++11 之前的遗产,现在留着只是为了读老代码时能认出它。

## 实战:一个能跑的全局配置读取器

光讲 GlobalOled 太虚,我们来个真能用的。下面这个 `ConfigManager` 是一个典型的单例配置读取器:它读一个 `key=value` 格式的配置文件,提供按 key 查询的接口,返回 `std::optional` 表示「可能没有这个 key」:

```cpp
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

class ConfigManager {
public:
    static ConfigManager& instance() {
        static ConfigManager config;  // Meyer's Singleton
        return config;
    }

    void read_from_file(const std::filesystem::path& path);
    std::optional<std::string> get_value(const std::string& key);

private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager(ConfigManager&&) = delete;
    ConfigManager& operator=(ConfigManager&&) = delete;

    void parse_line(const std::string& line);
    std::unordered_map<std::string, std::string> maps_;
};
```

你看,套路和前面一模一样:`instance()` 里一个 `static` 局部变量,四个特殊成员函数全部 delete,构造私有。`std::optional` 的返回值让调用方不得不处理「key 不存在」的情况,比返回空字符串或者抛异常都更体面。`std::filesystem::path` 则天然支持跨平台路径。

```cpp
void ConfigManager::parse_line(const std::string& line) {
    if (line.empty() || line[0] == '#') return;  // 空行 / 注释跳过

    const auto eq = line.find_first_of('=');
    if (eq == std::string::npos) return;          // 不是合法 kv,跳过

    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);
    key.erase(key.find_last_not_of(" \t") + 1);   // rtrim key
    val.erase(0, val.find_first_not_of(" \t"));   // ltrim val
    maps_.insert({key, val});
}
```

用起来就是这样,任何地方都能拿到那个唯一实例:

```cpp
auto& config = ConfigManager::instance();
config.read_from_file("app.conf");
if (auto host = config.get_value("host")) {
    connect(*host);  // 只有真的拿到值才进入
}
```

::: tip 配套可编译工程
这一节的完整代码(含 `#` 注释、空白行处理、500 线程并发读取测试、一个最简 `Logger` 单例)在本仓库里,自己 clone 下来 cmake 一把就能跑:[Singleton](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Singleton)。小提醒:其中的 `GlobalConfig` 要读同目录的 `test_config.txt`(CMake 会自动拷到 `build/` 下),所以跑它得 `cd build && ./ConfigManager`;`Logger` 直接 `./build/Logger` 即可。
:::

## 单例为什么不讨喜

到这里我们已经有了一个正确、线程安全、写起来还特别简单的单例。但事情到这里还没完——我得诚实地告诉你,**单例模式在软件工程里名声其实不太好**,不少工程师把它视作应当谨慎使用的模式,甚至是反模式。为什么?

**第一,它压倒了单一职责。** 一个 `ConfigManager` 既是「配置管理」又兼任「全局访问」,你只要在任何地方写下 `ConfigManager::instance().get_value(...)`,这个全局对象就穿透了你模块的接口边界——本来你的函数只该依赖「能拿到某个配置值」这个抽象,现在它直接耦合到了一个具体的全局实现上。

**第二,它让单元测试变得非常痛苦。**

```cpp
void do_work() {
    ConfigManager::instance().get_value("timeout");  // 写死成全局
}

void test_do_work() {
    // 想换个假的 ConfigManager 来测边界条件?抱歉,改不了。
    do_work();
}
```

因为实例是全局唯一的、是在 `instance()` 里硬编码的,你测试时根本没法替换成一个 mock。单例强迫每个用到它的模块都得连着真实单例一起被测,单元测试「单元独立」的前提就被破坏了。

**第三,它违背开闭原则(OCP)。** 哪天你想从「全局一个配置」改成「每个租户一份配置」,你会发现 `ConfigManager` 被设计成单例的那一刻起,扩展成多实例几乎要重构一大片代码。

**第四,static 单例有自己的生命周期坑。** 如果单例持有重量级资源(大缓存、文件句柄),即使你只用了一小会儿,它也会常驻到程序结束。更隐蔽的是,**静态对象的销毁顺序是不可控的**——如果单例 A 依赖另一个静态对象 B,而 B 比 A 先析构了,A 在析构时去碰 B 就是未定义行为(著名的 *static deinitialization order* 问题)。

## 改进:把单例关进子系统——依赖注入

上面这些毛病的根源是同一个:**单例「穿透」了接口,把全局状态强塞给了每一个调用方**。一个更健康的做法是把依赖关系翻转过来——**别让调用方去抓全局,而是由上层把需要的对象主动注入进来**:

```cpp
class OledUpdater {
public:
    explicit OledUpdater(GlobalOled& oled) : oled_(oled) {}

    void do_work() {
        oled_.process_buffer_update();
    }

private:
    GlobalOled& oled_;
};

// 使用时手动注入
int main() {
    auto& oled = GlobalOled::get_instance();   // 单例被关在 main 这一层
    OledUpdater updater(oled);                 // updater 只依赖引用,不知道全局
    updater.do_work();
}
```

这一翻转带来三个直接好处:`OledUpdater` 不再依赖全局状态,它只依赖一个 `GlobalOled&`,测试时你可以随手传一个假的实现进去;单例的作用域被压缩到了 `main` 这一层子系统里,而不是满程序都是 `::get_instance()`;想扩展成多实例时,改 `main` 的装配代码就够了,`OledUpdater` 一行都不用动。

这就是**依赖注入(DI)**的思路。真正的单例——那种「程序里就一个、躲不掉」的——其实非常少。多数时候你以为需要单例,其实需要的是「在某个子系统内唯一」,而那种需求,注入一个引用就解决了。

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 注释约束 | 写 `// only once` | 编译器不检查,挡不住隐式拷贝 |
| `= delete` | 禁用拷贝/移动 | 没法构造实例了 |
| Meyer's Singleton | 私有构造 + `static` 局部变量 | **够用了**(C++11 magic statics 保证线程安全) |
| 手写 DCLP | 双重检查锁 + acquire/release | 历史遗产,现代 C++ 不需要 |
| 依赖注入 | 把单例关进子系统,注入引用 | 解决全局状态污染与可测试性 |

记下这几条关键结论:

- **现代 C++ 写单例,第一选择是 Meyer's Singleton**(私有构造 + `static` 局部变量 + delete 拷贝移动),一行锁都不用写。
- **不要手写 DCLP**,尤其不要用 `memory_order_consume`——magic statics 已经包办了初始化的线程安全。
- 单例的真正代价不在实现,而在**全局状态污染、难测试、违背 OCP、static 销毁顺序**。
- 多数「我需要单例」的场景,真正需要的是**依赖注入**——把唯一性约束在一个子系统内,而不是放任全局。

## 参考资源

- [cppreference:Static local variables](https://en.cppreference.com/w/cpp/language/storage_duration#Static_local_variables)(magic statics,C++11 起)
- [cppreference:`std::memory_order`](https://en.cppreference.com/w/cpp/atomic/memory_order)(acquire/release/consume 的语义)
- Scott Meyers,《Effective C++》Item 4 / Andréi Alexandrescu,《Modern C++ Design》第 6 章(单例与多线程)
- 配套可编译工程:[Singleton](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns/Singleton)
