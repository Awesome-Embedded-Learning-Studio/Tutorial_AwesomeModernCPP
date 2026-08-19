---
title: "现代 socket 封装:RAII、std::expected,以及每连接一线程扛不住的 C10K"
description: 把 00 的传统 C 风格 echo 现代化——RAII 的 unique_fd 让"漏 close"变成不可能、std::expected 把错误和值装进类型,再加上"每连接一线程"让它能扛并发;结尾实测 2000 个空闲连接把虚拟内存从 83MB 吃到 24GB,亲手摸到 C10K 的痛点,引出 epoll
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20, 23]
reading_time_minutes: 14
prerequisites:
  - "传统 socket 编程:服务器五步与 TCP 建链"
related:
  - "传统 socket 编程:服务器五步与 TCP 建链"
  - "epoll:Linux I/O 多路复用"
tags:
  - host
  - cpp-modern
  - intermediate
  - 网络编程
  - RAII守卫
---

# 现代 socket 封装:RAII、std::expected,以及每连接一线程扛不住的 C10K

上一篇(00)我们用最朴素的 C 风格 socket 走通了一个 echo server,把五步和 TCP 建链讲透了。但说实话,那套代码写完,笔者心里是有点别扭的——它能跑,可是**脏**。裸 `int fd` 在函数里到处传,中间任何一条 `if (...) return 1;` 都可能跳过末尾的 `close(cfd)`,fd 就悄悄漏掉了;错误处理是散落的 `errno` 加 `perror`,每一步都"返回值看一眼、errno 再看一眼",到底哪步炸的得你自己记。这套写法是 1983 年的 C 风格,它不管这些脏活——那是后面语言的事。

这一篇我们就用 Modern C++ 把它收掉。具体做两件事:第一,用 RAII 把 fd 的生命周期焊死,让"漏 close"在类型层面就不可能发生;第二,用 `std::expected` 把"成功值"和"带上下文的错误"装进同一个返回类型,替代散落的 errno。收完之后,我们再给这个 server 加上"每连接一线程",让它真正能同时伺候多个客户端——然后实测一个会让笔者血压拉满的数字,你会亲眼看到为什么"每连接一线程"这个看似天经地义的做法,在并发上去之后就崩。那个数字,正是下一篇 `epoll` 的入场券。

本篇的代码是 C++23(`std::expected`、`std::print` 都是 C++23),本机 GCC 16.1.1 编译运行,贴的终端输出都是真的。

## 先看传统版的"脏"到底脏在哪

在动手收之前,我们先把 00 那段代码的脏点指出来,这样后面每一处改造才有靶子。回想 00 的核心循环:

```c
for (;;) {
    int cfd = accept(lfd, NULL, NULL);
    if (cfd < 0) { if (errno == EINTR) continue; perror("accept"); continue; }

    char buf[4096];
    for (;;) {
        ssize_t n = read(cfd, buf, sizeof(buf));
        if (n <= 0) break;
        write(cfd, buf, n);
    }
    close(cfd);   /* ← 手动 close */
}
```

这里的 `cfd` 是个裸 `int`。看起来没问题——但你想,如果在 `read`/`write` 那段循环里,你将来要加一个错误处理分支、提前 `return` 或者抛个异常,`close(cfd)` 那行就被跳过了。fd 是进程级有限资源(默认上限 1048576 个,听起来很多,但一个长跑的服务器漏一点积少成多就耗尽),漏一个少一个,还不报错——这种 bug 叫**资源泄漏**,它在测试里毫发无损,跑上几天才因为"fd 耗尽、`socket()` 返回 -1"而炸,排查起来极其痛苦。

错误处理那边也好不到哪去:每个系统调用失败都返回 `-1` + 设 `errno`,你得在每一步之后判返回值、读 `errno`、再 `perror` 打印。错误信息和出错位置是割裂的——`perror("bind")` 里的 "bind" 是你手写字符串硬塞的,跟实际代码没有绑定关系,改名了一忘改就误导自己。

这两个脏点——**资源靠人记着释放、错误靠人拼字符串**——就是 C 风格的宿命。Modern C++ 给了对应的、类型层面的收口。

## RAII:让"漏 close"变成不可能

Modern C++ 管资源的核心思想叫 **RAII**(Resource Acquisition Is Initialization):把资源的所有权绑到一个栈对象上,**对象构造时获取资源、析构时释放资源**。栈对象的作用域一结束,析构函数必然被调用——不管是正常走到末尾、提前 `return`、还是抛了异常,都跑不掉。换句话说,把"记得释放"这件事从程序员的脑子里挪到类型系统里。

对应到 fd,我们写一个 `UniqueFd`:构造时接管一个裸 fd,析构时 `close`,而且**禁拷贝、只移动**——因为 fd 是独占资源,不能两个对象都以为自己拥有同一个 fd(那会 double close)。

```cpp
class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) : fd_{fd} {}
    ~UniqueFd() { reset(); }                              // 析构即 close

    UniqueFd(const UniqueFd&) = delete;                   // 独占,禁拷贝
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_{other.fd_} { other.fd_ = -1; }
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) { reset(); fd_ = other.fd_; other.fd_ = -1; }
        return *this;
    }

    void reset() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }
    int  get() const { return fd_; }
    explicit operator bool() const { return fd_ >= 0; }
private:
    int fd_{-1};                                          // -1 = 空,析构不动
};
```

几个设计点值得讲一下。`fd_{-1}` 是"空状态"(合法 fd 不会是 -1),这样"一个空的 UniqueFd 析构"是安全的——`reset()` 看到 `fd_ < 0` 就什么都不做,不会去 `close(-1)`。移动构造把对方的 fd 偷过来、把对方置成空(-1),保证任意时刻只有一个 `UniqueFd` 持有这个 fd。有了它,00 里那段循环就变成:

```cpp
for (;;) {
    int raw = ::accept(listener->get(), nullptr, nullptr);
    if (raw < 0) {
        if (errno == EINTR)
            continue;
        /* ... */
            continue;
    }
    UniqueFd conn{raw};        // ← 接管,从此 conn 活着 fd 就有效
    // ... 在 conn 上 read/write ...
}   // ← 循环体结束,conn 析构,自动 close——不可能漏
```

`conn` 是栈对象,无论这个循环体里将来加多少个 `return`、抛多少异常,它析构时一定 `close`。**"漏 close"从一个需要人记的事,变成了一个不可能发生的事**。这就是 RAII 的力量,也是 Modern C++ 区别于 C 最根本的一条。

## std::expected:把错误和值装进一个类型

资源搞定了,再看错误处理。C 风格的错误是"返回值 + errno"两件套,Modern C++ 的答案是 `std::expected<T, E>`——它要么装一个**成功值 T**,要么装一个**错误 E**,两者在同一个返回类型里,用类型系统强制你处理错误,不能假装没看见。

我们给错误定义一个带上下文的小结构,把"哪一步炸的"和 errno 一起带上:

```cpp
struct SysError {
    int errno_value;
    std::string context;     // "socket" / "bind" / "listen",失败发生在哪步
};
```

然后 `socket + bind + listen` 三步封装成一个返回 `std::expected<UniqueFd, SysError>` 的函数:

```cpp
std::expected<UniqueFd, SysError> make_listener(std::uint16_t port) {
    int raw = ::socket(AF_INET, SOCK_STREAM, 0);
    if (raw < 0) return std::unexpected(SysError{errno, "socket"});
    UniqueFd fd{raw};

    int yes = 1;
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
        return std::unexpected(SysError{errno, "setsockopt(SO_REUSEADDR)"});

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);
    if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        return std::unexpected(SysError{errno, "bind"});

    if (::listen(fd.get(), 64) < 0)
        return std::unexpected(SysError{errno, "listen"});

    return fd;   // 成功:直接返回 UniqueFd
}
```

调用方拿到的是一个"可能成功也可能失败"的盒子,用 `if (!listener)` 一眼判断,失败时 `listener.error().context` 直接告诉你"是 bind 阶段、errno 98"——上下文和错误绑在一起,不用你再手写 `perror("bind")` 那种割裂的字符串。对比 00 的 `perror`,这里最大的区别是:**错误信息不再是散落的、靠人维护的字符串,而是类型里的一等公民,跟着错误对象走**。

这是 Modern C++ 给 I/O 代码的范式升级——和 RAII 一脉相承,都是"把程序员脑子里的约定,挪到类型系统里强制"。

## 加并发:每连接一线程

00 的传统 server 是**单线程**的——`accept` 一个连接、echo 完、`close`、再 `accept` 下一个。这有个要命的缺点:如果一个客户端连上来不发包(就挂着),整个 server 就卡在 `read` 上等它,后面的连接全进不来。能跑,但同一时刻只能伺候一个客人。

最直觉的升级是**每来一个连接,spawn 一个线程专门伺候它**。`accept` 拿到新 fd 之后,把它交给一个独立线程,主循环立刻回去 `accept` 下一个——这样多个客户端就被多个线程并行处理,互不卡。配合刚写的 RAII,fd 用 `std::move` 交给线程,所有权转移得干干净净:

```cpp
void handle_session(UniqueFd conn) {       // 按值接收:线程独占这条 fd
    std::array<char, 4096> buf;
    for (;;) {
        ssize_t n = ::read(conn.get(), buf.data(), buf.size());
        if (n == 0) break;                 // 对端关闭
        if (n < 0) { if (errno == EINTR) continue; break; }
        /* ... 回写 ... */
    }
}   // conn 析构,自动 close

int main() {
    /* ... make_listener ... */
    for (;;) {
        int raw = ::accept(listener->get(), nullptr, nullptr);
        if (raw < 0) { if (errno == EINTR) continue; /* ... */ continue; }
        UniqueFd conn{raw};
        std::thread{handle_session, std::move(conn)}.detach();   // 每连接一线程
    }
}
```

`std::thread{handle_session, std::move(conn)}.detach()`——`detach` 让线程在后台独立跑,主循环不等它。`conn` 通过 `std::move` 转交所有权,转完主循环里的 `conn` 就是空的了,fd 彻底归线程所有,线程函数返回时 `conn` 析构自动 `close`。没有 double close,也没有漏 close。

跑起来,开三个客户端同时连,三个都立刻得到回显——并发问题了,看起来皆大欢喜。但事情到这里还没完,真正的坑在后面。

## 但是,它扛得住多少并发?

"每连接一线程"看起来天经地义——一个连接一个线程,多直观。可我们换个问题问它:**如果并发量上去,比如几千个、一万个连接,它还扛得住吗?** 我们真跑一次,别凭感觉。

趁 server 跑着,我们开一个客户端连 2000 条**空闲连接**(连上但不发包,就挂着),期间读 server 的 `/proc/<pid>/status`,看它的虚拟内存、常驻内存、线程数怎么变:

```text
[idle]       VmSize:  85352 kB    VmRSS:  4212 kB    Threads: 1
[2000 连接]   VmSize: 25081508 kB  VmRSS: 28888 kB    Threads: 2001
```

你看那个 `VmSize`:**从 83MB 飙到了将近 24GB**。2000 个连接,虚拟内存吃了 24GB。算到每条连接上,`(25081508 - 85352) / 2000 ≈ 12.5 MB/连接`。这 12.5MB 是哪来的?——**每个线程默认 8MB 栈**(glibc 默认值),再加上 glibc 的 per-thread 内部映射、TLS、guard page,堆起来每个线程约 12MB 的虚拟地址空间。`Threads` 那列更是直接:`1 → 2001`,每来一个连接就多一个线程。

有意思的是 `VmRSS`(常驻物理内存)只涨了 `(28888 - 4212) / 2000 ≈ 12 KB/连接`——因为内核只给"真正摸到的栈页"分配物理内存(惰性分配),空闲阻塞的线程几乎不碰栈。所以**看着 RSS 不多,但虚拟地址空间已经被栈预留吃光了**。这就是 C10K 问题(Dan Kegel 1999 年提的经典命题:一台机器怎么扛一万个并发连接)的命门:每连接一线程 → 1 万连接 = 8MB × 10000 = **80GB 虚拟地址空间**;而且每个线程内核还要维护一份 `task_struct` + 内核栈,1 万个 mostly-idle 的线程(它们大部分时间都阻塞在 `read` 上干等数据)给调度器和内存子系统徒增负担——**用"线程"这个重实体去对应一个可能长期空闲的连接,资源利用率极低**。

说白了,"每连接一线程"不是错在"跑不动",而是错在**它用最重的资源(一个线程)去伺候最轻的工作(一个大部分时间在等 I/O 的连接)**。连接少的时候你感觉不到,一上量就炸。

那怎么办?方向很清楚:**用少量线程,服务大量连接**——让一两个线程同时盯着成千上万个 fd,谁的 fd 有数据了再去处理谁,而不是给每个连接配一个专职线程蹲守。这就是下一篇 **epoll / I/O 多路复用** 要解决的问题,也是我们从"同步阻塞、每连接一线程"跨进"事件驱动"的门槛。

## 小结

这一篇我们用 Modern C++ 把 00 的传统 server 改造了一遍,几个关键的东西收一下:

- **RAII `UniqueFd`**:fd 的生命周期焊到栈对象上,析构即 `close`,禁拷贝只移动。"漏 close"从靠人记变成不可能。这是 Modern C++ 区别于 C 最根本的一条。
- **`std::expected<T, E>`**:成功值和带上下文的错误装进同一个返回类型,`if (!x)` 一眼判断,错误信息跟着错误对象走,替代散落的 errno + 手写 `perror` 字符串。
- **每连接一线程**:让 server 能扛并发客户端,fd 用 `std::move` 转交线程,所有权干净。
- **C10K 实测**:2000 空闲连接把虚拟内存从 83MB 吃到 24GB(每连接 ~12MB,主要是 8MB 线程栈),Threads 涨到 2001。根本问题是"用重实体(线程)伺候轻工作(等 I/O 的连接)",连接一上量就炸。
- **出路**:少量线程服务大量连接——下一篇 epoll。

到这一篇,Linux socket 这块我们从"传统 C 风格"走到了"现代 C++ + 并发",也亲手摸到了同步模型的天花板。下一篇就该翻过这道墙了:epoll 怎么让一个线程盯住成千上万个 fd。

## 参考资源

- [cppreference: std::expected](https://en.cppreference.com/w/cpp/utility/expected) —— C++23 的错误处理(`std::unexpected` 构造错误值)
- [cppreference: std::unique_ptr / RAII](https://en.cppreference.com/w/cpp/memory/unique_ptr) —— RAII 范式,`UniqueFd` 是同一思想在 fd 上的应用
- [The C10K problem (Dan Kegel)](https://kea.dev/notes/the-c10k-problem) —— "一台机器如何扛一万并发连接",本篇实测正是它的动机
- [传统 socket 编程:服务器五步与 TCP 建链(本系列 00)](./00-traditional-socket-basics.md) —— 本篇的现代化对象
- [epoll:Linux I/O 多路复用(本系列下一篇)](./02-epoll-io-multiplexing.md) —— 用少量线程服务大量 fd,解决本篇结尾的 C10K 痛点
