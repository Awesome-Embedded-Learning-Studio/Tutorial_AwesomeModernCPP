---
title: "Boost.Asio 学习(5)：用协程改写异步服务器"
description: "把第 4 篇的回调式异步服务器用 C++20 协程重写：co_await 挂起点、Asio 的 awaitable / use_awaitable / co_spawn，以及 strand 与取消语义"
chapter: 1
order: 5
tags:
  - 网络编程
  - 异步编程
  - coroutine
  - 实战
  - host
  - cpp-modern
  - advanced
difficulty: advanced
platform: host
cpp_standard: [20]
reading_time_minutes: 20
community_category: learning-notes
---

# Boost.Asio 学习(5)：用协程改写异步服务器

> **作者**：游翰灵（Karl-Heinrich-Marx-520） · **原创声明**：本文为本人原创学习笔记整理，示例代码均为本人编写并在本地（MinGW g++ 13.1 + Boost 1.91，C++20）编译运行验证
> **主要参考**：[Boost.Asio 官方文档](https://think-async.com/Asio/)、[cppreference](https://en.cppreference.com/)\
> **目标读者**：读完第 4 篇、想知道回调之外还有哪种写法的现代 C++ 学习者
> **授权说明**：同意维护者调整标题、格式、放置位置与部分措辞；随仓库以 MIT 许可发布

第 4 篇末尾埋了个尾巴：回调嵌套把读、写、业务拆散到好几个成员函数里，一个会话的生命周期要靠 `shared_from_this()` 撑着，发送队列还得配一把锁加一个“正在写”标志位。本篇把同一台服务器用 C++20 协程重写一遍。你会学会怎么把"读头—校验—读体—处理—回包"的状态机缩回一个 `for` 循环，怎么削减锁和队列，以及这么做的代价是什么。

在开始之前，我们先熟悉下三个关键字：

`co_await` 是挂起点，协程停在这一行、把线程还给 `io_context`，等操作完成再从这一行往下走；`co_return` 是协程的返回；`co_yield` 本篇用不到。

协程的局部变量存在堆上的协程帧里，挂起期间不会丢，这正是它能替你省掉"把状态塞进成员变量"的原因。至于 `promise_type`、awaiter 三件套这些语言机制，本仓库第 5 卷第 6 章《异步 I/O 与协程》已经讲透，本篇只讲 Asio 怎么把它们接进自己的异步模型。

本篇的路线是：先跑通一个协程版 echo 服务器，再逐个认识 Asio 为协程准备的四个零件，然后回答两个第 4 篇留下的问题——strand 还要不要用、怎么让一个协程停下来，最后把第 4 篇的 id-length-value 会话整个改写成协程版。示例端口用 14515、14516、14517，与前四篇的 8080、8081、8082、14514 错开。

​

## 完整示例：协程版 echo 服务器

### 服务器：一个协程管一条连接，读写全用 co\_await

三个线程共跑一个 `io_context`，每接受一个连接就给它开一个协程：

```cpp
#include <boost/asio.hpp>
#include <exception>
#include <iostream>
#include <thread>
#include <utility>
#include <vector>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

asio::awaitable<void> echo_session(tcp::socket socket) {
    try {
        char data[1024];
        for (;;) {
            std::size_t n = co_await socket.async_read_some(asio::buffer(data), asio::use_awaitable);
            co_await asio::async_write(socket, asio::buffer(data, n), asio::use_awaitable);
            std::cout << "  [thread " << std::this_thread::get_id() << "] echo " << n
                      << " bytes\n";
        }
    } catch (const std::exception& e) {
        std::cout << "Session ended: " << e.what() << "\n";
    }
}

asio::awaitable<void> listener(unsigned short port) {
    auto executor = co_await asio::this_coro::executor;
    tcp::acceptor acceptor(executor, {tcp::v4(), port});
    std::cout << "Server running on port " << port << " (Ctrl+C to stop)\n";
    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable);
        std::cout << "New connection accepted\n";
        asio::co_spawn(executor, echo_session(std::move(socket)), asio::detached);
    }
}

int main() {
    asio::io_context io;
    asio::co_spawn(io, listener(14515), asio::detached);

    std::vector<std::thread> workers;
    for (int i = 0; i < 3; ++i) {
        workers.emplace_back([&io] { io.run(); });
    }
    for (auto& t : workers) {
        t.join();
    }
}
```

对比第 4 篇，这里少了三样东西：没有 `Session` 类，没有 `shared_from_this()`，没有成员变量缓存半包。socket 是按值传进协程的，它活在协程帧里，协程结束它就析构，连接也就关了。

### 客户端：连上后发一条、收一条

客户端同样是一个协程，四步交互写成四行：

```cpp
#include <boost/asio.hpp>
#include <iostream>
#include <string>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

asio::awaitable<void> echo_client(unsigned short port) {
    auto executor = co_await asio::this_coro::executor;
    tcp::socket socket(executor);
    co_await socket.async_connect({asio::ip::make_address("127.0.0.1"), port},
                                  asio::use_awaitable);
    std::cout << "已连接服务器\n";

    std::string msg = "hello echo";
    co_await asio::async_write(socket, asio::buffer(msg), asio::use_awaitable);

    char data[1024];
    std::size_t n = co_await socket.async_read_some(asio::buffer(data), asio::use_awaitable);
    std::cout << "收到回显: " << std::string(data, n) << "\n";

    socket.shutdown(tcp::socket::shutdown_both);
    std::cout << "客户端退出\n";
}

int main() {
    asio::io_context io;
    asio::co_spawn(io, echo_client(14515), asio::detached);
    io.run();
}
```

注意 `msg` 和 `data` 都是普通局部变量，直接传给 `asio::buffer` 就行。它们所在的协程帧在 `co_await` 挂起期间一直活着，不存在"异步操作还拿着引用、变量已经出栈"的问题。

## 跑起来是什么样

先起服务器再跑客户端，服务端是这样（Boost 1.91 的 `e.what()` 会附带源码位置，这里省略了；线程号每次运行也不固定）：

```text
Server running on port 14515 (Ctrl+C to stop)
New connection accepted
  [thread 2] echo 10 bytes
Session ended: End of file
```

客户端是这样：

```text
已连接服务器
收到回显: hello echo
客户端退出
```

客户端 `shutdown` 之后，服务端 `async_read_some` 读到 eof，Asio 把它变成 `boost::system::system_error` 抛出来，被 `catch` 接住，会话协程正常结束。**协程里的错误是异常，不是 `error_code` 参数**——这是协程写法和回调写法最直观的一处差异。

​

***

## 逐个拆解

### awaitable：协程函数的返回类型

`asio::awaitable<T>` 就是本篇所有协程函数的返回类型，`T` 是协程最终 `co_return` 的值的类型。它还有第二个模板参数叫执行器类型，默认 `any_io_executor`。

写 `asio::awaitable<T>` 就等于告诉编译器"这是个协程"，`promise_type`、协程帧、挂起恢复那套东西 Asio 已经实现好了，我们不用管。

第二个参数别写死。留空就是 `any_io_executor`，意思是“什么执行器都行”，所以同一个 `awaitable<void>` 既能 spawn 到 `io_context`，也能 spawn 到 strand，本篇全程这么干。一旦把它写死成 `awaitable<void, strand<...>>`，再想 spawn 到裸 `io_context` 就编译不过了。

​

### co\_spawn：把协程挂上执行器

`co_spawn` 有三个参数：执行器、协程函数、完成令牌。

它不会立刻开始跑，而是把协程的启动排到执行器上。所以你在 `io.run()` 之前 `co_spawn` 是安全的，真正执行发生在 `io.run()` 里。

第三个参数别省，它有默认值 `deferred`，而 `deferred` 是惰性的：不主动去执行它的话协程根本不会跑。

```cpp
asio::co_spawn(io, listener(14515), asio::detached);
```

`listener(14515)` 这次调用只是把协程帧建出来、挂在 `initial_suspend` 上，真正的函数体要等 `io_context` 调度到它才开始。

​

### use\_awaitable：让 async\_\* 可以被 co\_await

`use_awaitable` 是本篇出现频率最高的一个词，它是 Asio 异步 API 和协程之间的桥。不写它，`async_read_some` 只会去找回调参数；写了它，这个异步操作的返回值就变成 `awaitable<T>`，可以被 `co_await`：

```cpp
std::size_t n = co_await socket.async_read_some(asio::buffer(data), asio::use_awaitable);
```

回调写法里那个 `(error_code ec, size_t length)` 的参数对，在协程写法里被拆成了两路：正常的返回值变成 `co_await` 的值，错误变成抛出的 `boost::system::system_error`。

想要 `error_code` 而不是异常，就套一层 `redirect_error`：

```cpp
boost::system::error_code ec;
std::size_t n = co_await socket.async_read_some(
    asio::buffer(data), asio::redirect_error(asio::use_awaitable, ec));
if (ec) { /* 自己处理 */ }
```

`redirect_error` 只接管异步操作本身的失败。**协程里 `throw` 出来的普通异常不会进 `ec`**，它照常往外抛。

​

### completion token：协程结束之后结果交给谁

`co_spawn` 的第三个参数决定协程跑完之后发生什么：

| **token**          | **协程跑完之后**                                                                                |
| ------------------ | ----------------------------------------------------------------------------------------- |
| `asio::detached`   | 结果和异常一起丢掉。出错不会崩                                                                           |
| 回调函数               | 结果和异常一起交给你，签名是 `[](std::exception_ptr e, T result)`；`T` 是 `void` 时只剩 `std::exception_ptr` |
| `asio::use_future` | 结果进 `std::future`。`.get()` 会阻塞，别在跑 `io_context` 的线程上调用                                    |
|  `deferred` ( 默认 ） | 不点火不跑                                                                                     |
|  `use_awaitable`   | 协程里启动子协程并等它                                        |

`detached` 最常用，代价是静默。实测过：一个协程 `throw std::runtime_error` 而不接，`detached` 下 `io.run()` 照常返回、进程不崩，控制台什么都不会打印。所以本篇每个会话协程都自己包了 `try/catch`——别指望 `detached` 替你报错误。

​

### this\_coro::executor：协程自己所在的执行器

`co_await asio::this_coro::executor` 是一个内置 awaiter，返回当前协程所绑定的执行器。`listener` 里用它构造 `acceptor`，`echo_client` 里用它构造 socket，这样这些对象就继承协程的执行器，而不是你再手动传一遍 `io_context`。

它在 strand 场景下尤其关键：协程被 spawn 到 strand 上时，这里拿到的是 strand 本身，用它造出来的 timer 和 socket 也都归这条 strand 管。

​

### 协程与 strand：什么时候还需要串行化

结论先行：一条连接一个协程时，你不需要 strand。

原因是协程内部的 `co_await` 天然是串行的，任何时刻只有一个挂起点能被恢复。第 4 篇的 `send_mutex_`、`writing_`、`send_queue_` 三件套，本质都是为了让并发的回调串行化，而协程本身就是串行的。

但有一件事没变：多线程 `io_context` 下，**同一个协程会在不同线程上被恢复**。实测过：某条连接的前三次回显落在线程 2，后三次落在线程 4。不过协程帧里的局部变量存在堆上、只有自己能访问，所以这么跳是安全的。**跨协程共享的东西（会话表、全局统计）该加锁还是得加**，这一点和没变。

那什么时候需要 strand？**一条连接上开了两个及以上协程的时候**。比如给会话加一个每秒一次的心跳协程，它和读协程共用同一个 socket，就必须绑到同一条 strand 上：

```cpp
using namespace std::chrono_literals;
static const char kPing[] = "[ping]";

asio::awaitable<void> heartbeat(std::shared_ptr<tcp::socket> socket) {
    try {
        for (;;) {
            asio::steady_timer timer(co_await asio::this_coro::executor);
            timer.expires_after(1s);
            co_await timer.async_wait(asio::use_awaitable);
            co_await asio::async_write(*socket, asio::buffer(kPing, sizeof(kPing) - 1),
                                      asio::use_awaitable);
        }
    } catch (const boost::system::system_error& e) {
        std::cout << "heartbeat 结束: " << e.code().message() << "\n";
    }
}

// listener 里：同一条连接的两个协程共用一条 strand
auto strand = asio::make_strand(executor);
asio::co_spawn(strand, echo_loop(socket), asio::detached);
asio::co_spawn(strand, heartbeat(socket), asio::detached);
```

这里 `heartbeat` 里的 timer 是用 `this_coro::executor` 造的，所以它是 strand 上的 timer，恢复时也会回到 strand——两个协程的任何一步都不会并发。跑起来客户端会交替收到回显和心跳：

```text
已连接服务器
收到: hello
收到: [ping]
收到: [ping]
收到: [ping]
```

最后一个容易记错的点：**strand 保证的是"handler 不并发、且按投递顺序执行"，而不是"这一整段逻辑是原子的"**。协程每次 `co_await` 挂起时它当前的 handler 就返回了，同 strand 上排队的另一个协程会立刻插进来。所以"读共享变量—挂起—写回"这种临界区照样会被打断，要么把这段写成不含 `co_await` 的一整段，要么用不含协程真正的锁。

​

### 协程取消：怎么让一个协程停下来

第 4 篇的 `Session::Stop()` 是"关 socket + 置标志位"，协程版的对应物是取消。两种常见触发方式：

第一种是外部主动取消。`cancellation_signal` 是个信号源，用 `bind_cancellation_slot` 把它绑到协程上，之后在你想停的时候 `emit` 一下：

```cpp
asio::awaitable<void> wait_long(const char* name, std::chrono::seconds d) {
    try {
        asio::steady_timer timer(co_await asio::this_coro::executor);
        timer.expires_after(d);
        co_await timer.async_wait(asio::use_awaitable);
        std::cout << name << " 正常等到\n";
    } catch (const boost::system::system_error& e) {
        if (e.code() == asio::error::operation_aborted) {
            std::cout << name << " 被取消（operation_aborted）\n";
        }
    }
}

void demo_signal() {
    asio::io_context io;
    asio::cancellation_signal sig;  // 取消信号发送器

    asio::co_spawn(io, wait_long("waiter", 5s),
                   asio::bind_cancellation_slot(sig.slot(), asio::detached));

    asio::steady_timer kick(io, 1s);   // 1 秒后发出取消
    kick.async_wait([&sig](const boost::system::error_code&) {
        sig.emit(asio::cancellation_type::terminal);
    });

    io.run();
}
```

注意 `emit` 不在启动处调用——它通常待在 `Stop()`、定时器回调或者信号处理里，上面的 `kick` 只是为了让例子能跑起来。

第二种是超时自动取消，给操作套一层 `cancel_after`，到点没完成就自己取消：

```cpp
asio::steady_timer timer(co_await asio::this_coro::executor);
// timer 设置为 5 秒后才完成
timer.expires_after(5s);
co_await timer.async_wait(asio::cancel_after(1s, asio::use_awaitable));
```

取消之后正在 await 的那个操作以 `operation_aborted` 结束，并且 `co_await` 会直接抛出 `system_error(operation_aborted)` 。

总结：

| **场景** | **做法**                           |
| ------ | -------------------------------- |
| 主动取消   | `cancellation_signal` + `emit()` |
| 超时取消   | `cancel_after()`                 |
| 取消结果   | `operation_aborted`              |
| 处理方式   | `try/catch`                      |

***

## 收官：把第 4 篇的 id-length-value 会话缩成一个函数

第 4 篇的会话是个三段式状态机：读 4 字节头、校验 id 与长度、再读消息体，中间靠 `head_parsed_` 记住“上一轮读到哪了”。协程版把它压回一个 `for` 循环——三个 `co_await` 就是那三步，函数里的局部变量替掉了成员变量。下面是完整可编译的服务器，端口 14517：

```cpp
#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/system/system_error.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace msg {
inline constexpr std::size_t kHeadTotalLength = 4;
inline constexpr std::size_t kHeadIdLength = 2;
inline constexpr std::size_t kHeadDataLength = 2;
inline constexpr std::size_t kMsgLengthLimit = 1024;
inline constexpr unsigned short kMsgIdMax = 1024;
enum class MsgId : unsigned short { kHelloWorld = 1001 };
}  // namespace msg

// 业务处理：与第 4 篇 LogicSystem 里的回调体一致，只是不再经过队列和线程
std::string HandleMessage(unsigned short id, const std::string& body) {
    if (id != static_cast<unsigned short>(msg::MsgId::kHelloWorld)) {
        return nlohmann::json{{"error", 1003}, {"msg", "unknown id"}}.dump();
    }

    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(body);
    } catch (const nlohmann::json::parse_error&) {
        return nlohmann::json{{"error", 1001}, {"msg", "invalid json"}}.dump();
    }

    if (!parsed.is_object() || !parsed.contains("id") ||
        !parsed["id"].is_number_integer() || !parsed.contains("data") ||
        !parsed["data"].is_string()) {
        return nlohmann::json{{"error", 1002}, {"msg", "bad fields"}}.dump();
    }

    const int req_id = parsed["id"].get<int>();
    const std::string req_data = parsed["data"].get<std::string>();
    std::cout << "receive msg id: " << req_id << " data: " << req_data << "\n";

    return nlohmann::json{{"id", req_id},
                          {"data", "server has received msg: " + req_data}}
        .dump();
}

std::string PackFrame(unsigned short id, const std::string& payload) {
    std::string frame(msg::kHeadTotalLength + payload.size(), '\0');
    const unsigned short id_net = boost::endian::native_to_big(id);
    const unsigned short len_net =
        boost::endian::native_to_big(static_cast<unsigned short>(payload.size()));
    std::memcpy(frame.data(), &id_net, msg::kHeadIdLength);
    std::memcpy(frame.data() + msg::kHeadIdLength, &len_net, msg::kHeadDataLength);
    std::memcpy(frame.data() + msg::kHeadTotalLength, payload.data(), payload.size());
    return frame;
}

asio::awaitable<void> session(tcp::socket socket) {
    try {
        for (;;) {
            std::array<unsigned char, msg::kHeadTotalLength> head{};
            co_await asio::async_read(socket, asio::buffer(head), asio::use_awaitable);

            unsigned short msg_id = 0;
            unsigned short msg_len = 0;
            std::memcpy(&msg_id, head.data(), msg::kHeadIdLength);
            std::memcpy(&msg_len, head.data() + msg::kHeadIdLength, msg::kHeadDataLength);
            msg_id = boost::endian::big_to_native(msg_id);
            msg_len = boost::endian::big_to_native(msg_len);

            if (msg_id == 0 || msg_id > msg::kMsgIdMax) {
                std::cout << "invalid msg id: " << msg_id << "，断开\n";
                co_return;
            }
            if (msg_len == 0 || msg_len > msg::kMsgLengthLimit) {
                std::cout << "invalid msg len: " << msg_len << "，断开\n";
                co_return;
            }

            std::string body(msg_len, '\0');
            co_await asio::async_read(socket, asio::buffer(body), asio::use_awaitable);

            const std::string payload = HandleMessage(msg_id, body);
            // frame 必须是具名变量：async_write 只持有 buffer 引用，
            // 临时对象在 co_await 挂起期间就销毁了
            const std::string frame = PackFrame(msg_id, payload);
            co_await asio::async_write(socket, asio::buffer(frame), asio::use_awaitable);
        }
    } catch (const boost::system::system_error& e) {
        std::cout << "session 结束: " << e.code().message() << "\n";
    }
}

asio::awaitable<void> listener(unsigned short port) {
    auto executor = co_await asio::this_coro::executor;
    tcp::acceptor acceptor(executor, {tcp::v4(), port});
    std::cout << "Server running on port " << port << " (Ctrl+C to stop)\n";

    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable);
        std::cout << "New connection accepted\n";
        asio::co_spawn(executor, session(std::move(socket)), asio::detached);
    }
}

int main() {
    asio::io_context io;
    asio::co_spawn(io, listener(14517), asio::detached);

    std::vector<std::thread> workers;
    for (int i = 0; i < 3; ++i) {
        workers.emplace_back([&io] { io.run(); });
    }
    for (auto& t : workers) {
        t.join();
    }
}
```

客户端同样是“组帧—发—读头—读体”四步：

```cpp
#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cstring>
#include <iostream>
#include <string>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace msg {
inline constexpr std::size_t kHeadTotalLength = 4;
inline constexpr std::size_t kHeadIdLength = 2;
inline constexpr std::size_t kHeadDataLength = 2;
enum class MsgId : unsigned short { kHelloWorld = 1001 };
}  // namespace msg

std::string PackFrame(unsigned short id, const std::string& payload) {
    std::string frame(msg::kHeadTotalLength + payload.size(), '\0');
    const unsigned short id_net = boost::endian::native_to_big(id);
    const unsigned short len_net =
        boost::endian::native_to_big(static_cast<unsigned short>(payload.size()));
    std::memcpy(frame.data(), &id_net, msg::kHeadIdLength);
    std::memcpy(frame.data() + msg::kHeadIdLength, &len_net, msg::kHeadDataLength);
    std::memcpy(frame.data() + msg::kHeadTotalLength, payload.data(), payload.size());
    return frame;
}

asio::awaitable<void> talk(unsigned short port, const std::string& text) {
    auto executor = co_await asio::this_coro::executor;
    tcp::socket socket(executor);
    co_await socket.async_connect({asio::ip::make_address("127.0.0.1"), port},
                                  asio::use_awaitable);

    const std::string body = nlohmann::json{{"id", 1}, {"data", text}}.dump();
    const std::string frame =
        PackFrame(static_cast<unsigned short>(msg::MsgId::kHelloWorld), body);
    co_await asio::async_write(socket, asio::buffer(frame), asio::use_awaitable);

    std::array<unsigned char, msg::kHeadTotalLength> head{};
    co_await asio::async_read(socket, asio::buffer(head), asio::use_awaitable);

    unsigned short msg_id = 0;
    unsigned short msg_len = 0;
    std::memcpy(&msg_id, head.data(), msg::kHeadIdLength);
    std::memcpy(&msg_len, head.data() + msg::kHeadIdLength, msg::kHeadDataLength);
    msg_id = boost::endian::big_to_native(msg_id);
    msg_len = boost::endian::big_to_native(msg_len);

    std::string payload(msg_len, '\0');
    co_await asio::async_read(socket, asio::buffer(payload), asio::use_awaitable);

    std::cout << "收到 id=" << msg_id << " 响应: " << payload << "\n";
    socket.shutdown(tcp::socket::shutdown_both);
}

int main(int argc, char* argv[]) {
    const std::string text = argc > 1 ? argv[1] : "hello coroutine";

    asio::io_context io;
    asio::co_spawn(io, talk(14517, text), asio::detached);
    io.run();
}
```

服务端是这样（`HandleMessage` 里的业务代码与第 4 篇 `LogicSystem` 一致，只是不再经过队列和线程）：

```text
Server running on port 14517 (Ctrl+C to stop)
New connection accepted
receive msg id: 1 data: hello coroutine
session 结束: End of file
```

客户端是这样：

```text
收到 id=1001 响应: {"data":"server has received msg: hello coroutine","id":1}
```

和第 4 篇逐项对照：

| **项**   | **第 4 篇（回调）**                              | **本篇（协程）**      |
| ------- | ------------------------------------------ | --------------- |
| 半包状态    | 成员变量 `head_parsed_`、`recv_head_node_`      | 无，局部变量          |
| 会话生命周期  | `shared_from_this()` 撑着                    | 无，socket 活在协程帧里 |
| 发送队列    | `send_queue_` + `send_mutex_` + `writing_` | 无，同一时刻只有一个写     |
| 业务线程解耦  | `LogicSystem` + 队列 + 条件变量                  | 无，业务直接在协程里跑     |
| 粘包处理代码量 | 三个回调函数 + 一个状态机                             | 一个 `for` 循环     |

代价也要说清楚：业务处理现在跑在 IO 线程上。第 4 篇把 `LogicSystem` 单独拆出去，是为了让慢业务不把 IO 线程堵住；协程版想保留这个好处，得自己把计算丢给线程池或另一个执行器。协程省掉的是代码结构，不是并发模型。

***

## 从状态机回到一个函数

本篇可以概括为三个要点：`use_awaitable` 把异步操作的返回值搬到 `co_await` 左边、错误搬到异常里；单协程天然串行，所以第 4 篇的 strand 和发送队列可以省掉；取消是协程版的 `Session::Stop()`，`cancellation_signal` 判整个协程死刑、`cancel_after` 只给单次操作设上限。

回调和协程不是替代关系。回调更贴近 Asio 的底层模型，写库、做精细控制时绕不开它，而协程胜在可读性，长流程的业务用它写出来的代码最接近人脑的顺序习惯。第 4 篇的骨架在真实项目里依然站得住，本篇只是把同一个问题的答案换了一种说法。

留个练习：给上面这台服务器加一个"读超时"——任何一条连接 60 秒没数据就踢掉。提示是 `cancel_after`，但要注意超时之后 `co_await` 会抛异常，`catch` 里要分清是超时还是对方断开。

## 参考资源

- Boost.Asio 官方文档：[https://think-async.com/Asio/](https://think-async.com/Asio/)
- cppreference（C++20 协程）：[https://en.cppreference.com/w/cpp/language/coroutines](https://en.cppreference.com/w/cpp/language/coroutines)
- 本仓库第 5 卷第 6 章《异步 I/O 与协程》：协程语言机制的完整讲解
- llfc 博客（C++ 后端教程）：[https://llfc.club/](https://llfc.club/)

第 6 篇会用 Boost.Beast 给服务器套一层 HTTP，到时候你会发现 HTTP 报文的解析也有它自己的状态机，以及 Beast 是怎么把这套状态机藏起来的。
