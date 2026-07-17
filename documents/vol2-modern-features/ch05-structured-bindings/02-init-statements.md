---
chapter: 5
cpp_standard:
- 17
description: C++17 的 if 和 switch 初始化器，让变量生命周期恰到好处
difficulty: intermediate
order: 2
platform: host
prerequisites:
- 'Chapter 5: 结构化绑定'
reading_time_minutes: 9
related:
- RAII 深入理解
tags:
- host
- cpp-modern
- intermediate
title: if/switch 初始化器：缩小变量作用域
---
# if/switch 初始化器：缩小变量作用域

笔者审代码时经常撞见这种写法：先声明一个变量，拿它做判断，可这个变量在 `if` 之后整个函数都还能看见，哪怕它只在 `if` 分支里有用。变量就这么漏到了外层作用域。C++17 给了个干净办法：if 和 switch 的初始化语句，把变量的生存范围卡在它真正有用的那几行里。

------

## 起因：变量漏到外层作用域

先看一个熟悉的场景。在 map 里查一个 key，再按查找结果走不同分支：

```cpp
{
    auto it = cache.find(key);
    if (it != cache.end()) {
        use(it->second);
    } else {
        cache[key] = compute_value(key);
    }
    // it 在这里仍然可见，但它已经没用了
}
```

有人可能会说，这不就多了一行声明，有什么大不了。问题在于，`it` 这个迭代器在 `if/else` 结束后还活着。后面要是再声明个同名变量，就遮蔽（shadowing）了；后面要是不小心又用到 `it`，可能拿到一个没意义的状态。函数一长，这种泄漏越攒越多，最后就是维护债。

更典型的是锁的保护范围。要是只想在条件判断期间持锁：

```cpp
std::unique_lock<std::mutex> lock(mtx);
if (condition) {
    do_something();
}
// lock 在这里才析构，可您只需要它在 if 期间有效
```

C++17 的 if 初始化器把这些场景都收拾干净了。

------

## if 初始化器的语法

语法很直白：在 `if` 的括号里，用分号把初始化语句和条件隔开。

```cpp
if (init-statement; condition) {
    // ...
}
```

`init-statement` 可以是任何声明语句或表达式语句，最常见的是变量声明。分号后面的 `condition` 就拿分号前面声明的那个变量来做判断。

### map 查找的经典用法

这是 if 初始化器最实用的场景之一：查 map，判断是否找到，再处理结果。

```cpp
std::map<std::string, int> cache;

if (auto it = cache.find(key); it != cache.end()) {
    std::cout << "Found: " << it->second << '\n';
} else {
    cache[key] = compute_value(key);
}
// it 在这里不可见，作用域被限制在 if/else 内部
```

对比一下没初始化器的写法，差别很明显：以前的 `it` 会漏到 `if` 之后，现在它的生命周期被精确卡在 `if/else` 块内。

### 结合结构化绑定

上一篇讲过结构化绑定，它跟 if 初始化器搭在一起更顺手。`std::map::insert` 返回 `pair<iterator, bool>`，那个 `bool` 表示是否插入成功。一行就能搞定：

```cpp
if (auto [it, ok] = cache.insert({key, compute_value(key)}); ok) {
    std::cout << "Inserted: " << it->second << '\n';
} else {
    std::cout << "Already exists: " << it->second << '\n';
}
```

`it` 和 `ok` 都被关在 `if/else` 内部。意图很清楚：试着插，成了打印 "Inserted"，否则打印 "Already exists"。

------

## switch 初始化器

switch 也有一样的初始化语法，分号隔开初始化和条件：

```cpp
switch (init-statement; condition) {
    case ...:
        break;
}
```

一个常见用途是在 switch 之前把数据准备好。比如按从输入流读到的命令类型做分发：

```cpp
switch (auto cmd = read_command(); cmd.type) {
    case CommandType::Start:
        start_process(cmd.arg);
        break;
    case CommandType::Stop:
        stop_process(cmd.id);
        break;
    case CommandType::Status:
        report_status();
        break;
    default:
        handle_unknown(cmd);
        break;
}
// cmd 在这里不可见
```

还有一种取巧写法：算字符串的哈希，拿哈希值做 switch（C++ 的 `switch` 不能直接匹配字符串）。完整能跑的版本长这样：

```cpp
#include <string_view>
#include <cstddef>

// 编译期哈希（用户定义字面量），让 case 标签能用 "start"_hash 这种写法
constexpr std::size_t operator""_hash(const char* s, std::size_t n) {
    std::size_t h = 0;
    for (std::size_t i = 0; i < n; ++i) h = h * 31 + std::size_t(s[i]);
    return h;
}
constexpr std::size_t hash_string(std::string_view s) {
    std::size_t h = 0;
    for (char c : s) h = h * 31 + std::size_t(c);
    return h;
}

int dispatch(std::string_view input) {
    switch (auto hash = hash_string(input); hash) {
        case "start"_hash:  return 1;
        case "stop"_hash:   return 2;
        case "status"_hash: return 3;
        default:            return 0;
    }
}
```

`"start"_hash` 是编译期常量，能当 case 标签；运行时拿输入算 `hash_string`，再分发。GCC 16.1.1 实测：

```text
dispatch("start")  = 1
dispatch("status") = 3
dispatch("reboot") = 0
```

得提醒一句：哈希把无限种输入压进有限范围，理论上必然碰撞，两个不同的串算出同一个哈希，就会落进错误的 case。要的是精确匹配，命中之后还得拿原串再比一次。

------

## 锁守卫模式：RAII 与初始化器的配合

if 初始化器天生配 RAII 风格的资源管理，锁是最典型的例子。要在持锁状态下检查某个条件：

```cpp
std::mutex mtx;
bool ready = false;

// 在持锁期间检查条件
if (std::lock_guard lock(mtx); ready) {
    // 持锁状态下执行
    process();
    ready = false;
}
// lock 在 if/else 结束时析构，自动释放锁
```

这里 `std::lock_guard lock(mtx)` 用了 C++17 的 CTAD（类模板参数推导），不用写 `std::lock_guard<std::mutex> lock(mtx)`。`lock` 对象在整个 `if/else` 块结束时析构，自动 `mtx.unlock()`。

要留意：锁的析构发生在整个 `if/else` 块结束时，`else` 分支也在持锁状态下跑。口说无凭，写个会打印获取/释放时机的 RAII 追踪器跑一下（GCC 16.1.1）：

```cpp
struct LockTracker {
    LockTracker()  { std::puts("  >> 锁获取"); }
    ~LockTracker() { std::puts("  << 锁释放"); }
};

std::puts("进入 if/else 块");
if (LockTracker lock; false) {
    // if 分支，不执行
} else {
    std::puts("else 分支执行中（此时锁仍被持有）");
}
std::puts("已离开 if/else 块");
```

```text
进入 if/else 块
  >> 锁获取
else 分支执行中（此时锁仍被持有）
  << 锁释放
已离开 if/else 块
```

`<< 锁释放` 排在 `else 分支执行中` 之后、`已离开 if/else 块` 之前，说明锁覆盖了整个 `if/else`，else 跑的时候锁还没放。要是只想在 if 里持锁、else 用不着锁，这种写法就把锁的范围带大了，得换更细的写法。

### 文件或资源检查

同样的模式也适合文件、网络连接这类场景：

```cpp
// 检查文件能否打开，能就读
if (auto f = std::ifstream("config.txt"); f.is_open()) {
    std::string line;
    while (std::getline(f, line)) {
        parse_config(line);
    }
} else {
    use_default_config();
}
// f 在这里析构，文件自动关闭
```

### 锁和查找能不能塞进同一个 if

多线程里"先持锁、再查条件"很常见。有人想把锁、查找、判断一行塞进 if：

```cpp
// 想当然的写法，编不过
if (std::lock_guard lock(mtx); auto it = data_store.find(id); it != data_store.end()) {
    process(it->second);
}
```

编不过。if 的括号里只容得下一个 init-statement，一个分号把 init 和 condition 分开，塞不下两个。正确的路子有几条：

```cpp
// 方法1：锁做 init，查找结果直接当条件
if (std::lock_guard lock(mtx); data_store.count(id) > 0) {
    process(data_store.at(id));
}

// 方法2：锁做 init，内层再套一个带 init 的 if
if (std::lock_guard lock(mtx); true) {
    if (auto it = data_store.find(id); it != data_store.end()) {
        process(it->second);
    }
}

// 方法3：回到朴素代码块，最直白
{
    std::lock_guard lock(mtx);
    if (auto it = data_store.find(id); it != data_store.end()) {
        process(it->second);
    }
}
```

方法 2 里 `if (std::lock_guard lock(mtx); true)` 看着别扭，但合法，锁的析构覆盖整个外层 if/else，内层 if 仍在持锁状态下跑。

------

## 作用域限制的妙用

if 初始化器真正的好处，是让变量的作用域精确贴合它的实际用途，少写一行只是顺便。这对可读性和可维护性都有帮助。

### 避免变量遮蔽

没有 if 初始化器时，同一函数里多次查找得换不同的变量名，或者用花括号限作用域：

```cpp
// 不用初始化器：变量名冲突
auto it1 = m1.find(key1);
if (it1 != m1.end()) { use1(it1->second); }

auto it2 = m2.find(key2);  // 不能也叫 it
if (it2 != m2.end()) { use2(it2->second); }
```

有了 if 初始化器，每个 `it` 都关在自己的 `if/else` 作用域里，不用换名字：

```cpp
if (auto it = m1.find(key1); it != m1.end()) { use1(it->second); }
if (auto it = m2.find(key2); it != m2.end()) { use2(it->second); }
```

### 提高代码局部性

变量声明和使用挨在一起，读者一眼就看出它的用途。要是声明在函数顶上、用在几十行之后，读者就得上下翻找。if 初始化器把声明和使用钉在一起。

```cpp
// 变量声明和使用分离，读者得在大段代码里找关联
auto status = check_system();
// ... 30 行其他代码 ...
if (status == Status::Ok) {
    // ...
}

// 用初始化器，声明和使用紧挨着
if (auto status = check_system(); status == Status::Ok) {
    // ...
}
```

------

## 常见的坑

### init 变量在 else 里也能用

if 初始化器声明的变量，在 `if` 和 `else` 两个分支里都能用，这点常被忽略。跑一下：

```cpp
std::map<int, std::string> m{{1, "one"}, {2, "two"}};
// 第一次插新 key
if (auto [it, ok] = m.insert({3, "three"}); ok) {
    std::cout << "if   分支: Inserted " << it->second << '\n';
} else {
    std::cout << "else 分支: Existing " << it->second << '\n';
}
// 第二次插已存在的 key
if (auto [it, ok] = m.insert({1, "ONE"}); ok) {
    std::cout << "if   分支: Inserted " << it->second << '\n';
} else {
    std::cout << "else 分支: Existing " << it->second << " (新值 ONE 未覆盖)\n";
}
```

```text
if   分支: Inserted three
else 分支: Existing one (新值 ONE 未覆盖)
```

第一次插新 key 走 if，第二次插已存在的 key 走 else，`it` 在两个分支里都拿得到，还顺带看出 insert 失败时新值不会覆盖旧值。

### 不能用于三元运算符

if 初始化器只适用于 `if` 和 `switch`，用不进三元运算符 `?:`。要在三元表达式里做初始化，只能退回先声明后使用的老办法。

### 调试时的注意

初始化器声明的变量作用域很短，有些调试器里一旦出了 `if/else` 块，变量就观察不到了。调试时要持续盯着某个变量，可能得暂时把声明挪到 `if` 外面。

------

## 在线运行

在线运行 if/switch 初始化器示例，体会变量作用域被精确卡在 if/switch 块内的效果：

<OnlineCompilerDemo
  title="if/switch 初始化器：缩小变量作用域"
  source-path="code/examples/vol2/13_init_statements.cpp"
  description="在线运行并观察 map 查找、insert + 结构化绑定、锁守卫、switch 初始化器如何把变量作用域限制在 if/switch 块内。"
  allow-run
/>

## 参考资源

- [cppreference: if statement](https://en.cppreference.com/w/cpp/language/if)
- [cppreference: switch statement](https://en.cppreference.com/w/cpp/language/switch)
- [C++17 if/switch init statement - C++ Stories](https://www.cppstories.com/2021/if-switch-init/)
