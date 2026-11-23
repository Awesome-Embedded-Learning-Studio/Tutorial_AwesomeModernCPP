---
title: "Learning Boost.Asio (5): Rewriting an Asynchronous Server with Coroutines"
description: "Rewriting the callback-based asynchronous server from Part 4 using C++20 coroutines: co_await suspension points, Asio's awaitable / use_awaitable / co_spawn, and strand and cancellation semantics"
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
translation:
  source: documents/community/incoming/boost-asio-05-coroutine-rewrite.md
  source_hash: 2e881db0b2040f05b8c3351b02dc657e050c019274f2043b79106678730be3a0
  translated_at: '2026-11-22T13:53:34+00:00'
  engine: qwen3:14b
  token_count: 19985
---
# Learning Boost.Asio (5): Rewriting an Asynchronous Server with Coroutines

> **Author**: You Hanling (Karl-Heinrich-Marx-520) · **Original Declaration**: This article is an original learning note summary, with all example code written by the author and compiled and run locally (MinGW g++ 13.1 + Boost 1.91, C++20)
> **Main References**: [Boost.Asio Official Documentation](https://think-async.com/Asio/), [cppreference](https://en.cppreference.com/)
> **Target Readers**: Readers who have completed Part 4 and want to know what other writing styles are available besides callbacks for modern C++ learners
> **License Statement**: Agree to allow maintainers to adjust the title, format, placement, and some wording; released with MIT license along with the repository

At the end of Part 4, a small issue was left unresolved: callback nesting splits read, write, and business logic into several member functions, the lifecycle of a session relies on `shared_from_this()`, and the send queue needs a lock and a "writing" flag. In this article, we rewrite the same server using C++20 coroutines. You will learn how to shrink the "read header-validate-read body-process-return" state machine into a single `for` loop, how to reduce locks and queues, and what the cost of doing so is.

Before we begin, let's get familiar with three keywords:

`co_await` is the suspension point, the coroutine pauses at this line, returns the thread to the `io_context`, and continues from this line after the operation is completed; `co_return` is the return of the coroutine; `co_yield` is not used in this article.

Local variables in coroutines are stored in the coroutine frame on the heap, and they are not lost during suspension. This is why it can save you from having to "store state in member variables." As for the language mechanisms such as `promise_type` and `awaiter`, they have been thoroughly explained in Chapter 6 of Volume 5 of this repository, "Asynchronous I/O and Coroutines." This article only discusses how Asio integrates them into its own asynchronous model.

The route of this article is: first, run through a coroutine-based echo server, then gradually understand the four components Asio has prepared for coroutines, then answer two questions left from Part 4—whether to still use strand, and how to make a coroutine stop, and finally rewrite the id-length-value session from Part 4 into a coroutine version. The example ports are 14515, 14516, and 14517, which are different from the previous four parts (8080, 8081, 8082, 14514).

​

## Complete Example: Coroutine-based Echo Server

### Server: One coroutine manages one connection, read and write use `co_await`

Three threads run a single `io_context`, and a coroutine is started for each connection accepted:

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

Compared to Part 4, this version is missing three things: no `Session` class, no `shared_from_this()`, and no member variables to cache partial packets. The socket is passed by value into the coroutine, and it lives in the coroutine frame. When the coroutine ends, it is destructed, and the connection is closed.

### Client: Send one message and receive one message after connecting

The client is also a coroutine, and the four-step interaction is written in four lines:

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

Note that both `msg` and `data` are ordinary local variables, and they can be directly passed to `asio::buffer`. The coroutine frame they are in remains alive during `co_await` suspension, so there is no problem of "asynchronous operations still holding references, and variables have already been popped off the stack."

## What It Looks Like When Running

Start the server first, then run the client. The server looks like this (Boost 1.91's `e.what()` includes the source code location, which is omitted here; the thread number is not fixed each run):

```text
Server running on port 14515 (Ctrl+C to stop)
New connection accepted
  [thread 2] echo 10 bytes
Session ended: End of file
```

The client looks like this:

```text
已连接服务器
收到回显: hello echo
客户端退出
```

After the client `shutdown`, the server `async_read_some` reads the `eof`, Asio turns it into a `boost::system::system_error` and throws it, which is caught by the `catch` block, and the session coroutine ends normally. **Errors in coroutines are exceptions, not `error_code` parameters**—this is the most intuitive difference between coroutine and callback writing styles.

***

## Dissecting Each Component

### awaitable: The return type of coroutine functions

`asio::awaitable<T>` is the return type of all coroutine functions in this article, where `T` is the type of the value that the coroutine ultimately `co_returns`. It has a second template parameter called the executor type, with a default of `any_io_executor`.

Writing `asio::awaitable<T>` tells the compiler "this is a coroutine," and Asio has already implemented the `promise_type`, coroutine frame, and suspension/resumption mechanisms, so we don't need to worry about them.

Don't hardcode the second parameter. Leaving it empty means "any executor is acceptable," so the same `awaitable<void>` can be spawned on both `io_context` and `strand`. This article does this throughout. Once you hardcode it as `awaitable<void, strand<...>>`, you won't be able to spawn it on a plain `io_context` anymore, and it will fail to compile.

​

### co_spawn: Spawning a coroutine on an executor

`co_spawn` has three parameters: the executor, the coroutine function, and the completion token.

It does not start immediately, but schedules the coroutine's launch on the executor. So it is safe to call `co_spawn` before `io.run()`, and the actual execution occurs inside `io.run()`.

Don't skip the third parameter. It has a default value of `deferred`, and `deferred` is lazy: if you don't actively execute it, the coroutine won't run at all.

```cpp
asio::co_spawn(io, listener(14515), asio::detached);
```

The call to `listener(14515)` this time only creates the coroutine frame and suspends it at `initial_suspend`. The actual function body starts only when the `io_context` schedules it.

​

### use_awaitable: Making async_* awaitable

`use_awaitable` is the most frequently used term in this article. It is the bridge between Asio's asynchronous API and coroutines. Without it, `async_read_some` only looks for callback parameters; with it, the return value of this asynchronous operation becomes `awaitable<T>`, which can be `co_await`ed:

```cpp
std::size_t n = co_await socket.async_read_some(asio::buffer(data), asio::use_awaitable);
```

In the callback style, the `(error_code ec, size_t length)` parameter pair is split into two paths in the coroutine style: the normal return value becomes the value of `co_await`, and the error becomes a thrown `boost::system::system_error`.

If you want `error_code` instead of exceptions, wrap it with `redirect_error`:

```cpp
boost::system::error_code ec;
std::size_t n = co_await socket.async_read_some(
    asio::buffer(data), asio::redirect_error(asio::use_awaitable, ec));
if (ec) { /* 自己处理 */ }
```

`redirect_error` only takes over the failure of the asynchronous operation itself. **Ordinary exceptions thrown in the coroutine will not go into `ec`**; they are thrown normally.

​

### Completion token: Who gets the result after the coroutine finishes

The third parameter of `co_spawn` determines what happens after the coroutine finishes:

| **token**          | **After the coroutine finishes**                                                                                |
| ------------------ | ----------------------------------------------------------------------------------------- |
| `asio::detached`   | The result and exception are discarded. No crash if an error occurs                                                                           |
| Callback function  | The result and exception are passed to you, with the signature `[](std::exception_ptr e, T result)`; when `T` is `void`, only `std::exception_ptr` remains |
| `asio::use_future` | The result goes into `std::future`. `.get()` blocks, so don't call it on the thread running `io_context`                                    |
| `deferred` (default) | Doesn't start unless triggered                                                                                     |
| `use_awaitable`    | The coroutine starts a child coroutine and waits for it                                        |

`detached` is the most commonly used, but it is silent. In practice, if a coroutine throws a `std::runtime_error` and is not caught, `detached` will not cause `io.run()` to return or the process to crash, and nothing will be printed to the console. So each session coroutine in this article is wrapped in its own `try/catch`—don't rely on `detached` to report errors for you.

​

### this_coro::executor: The executor the coroutine is on

`co_await asio::this_coro::executor` is an inbuilt awaiter that returns the executor the current coroutine is bound to. In `listener`, it is used to construct the `acceptor`, and in `echo_client`, it is used to construct the `socket`, so these objects inherit the coroutine's executor instead of manually passing `io_context` again.

It is especially crucial in the `strand` scenario: when a coroutine is spawned on a `strand`, this returns the `strand` itself, and the `timer` and `socket` created with it are also under the same `strand`.

​

### Coroutines and strand: When is serialization still needed

Conclusion first: when one connection is managed by one coroutine, you don't need `strand`.

The reason is that the `co_await` inside the coroutine is naturally serial, and only one suspension point can be resumed at any time. In Part 4, the `send_mutex_`, `writing_`, and `send_queue_` trio were essentially to make concurrent callbacks serial, but coroutines are inherently serial.

However, one thing hasn't changed: under a multi-threaded `io_context`, **the same coroutine can be resumed on different threads**. In practice, the first three echoes of a connection fall on thread 2, and the next three fall on thread 4. However, the local variables in the coroutine frame are on the heap and only accessible by the coroutine itself, so this is safe. **Shared things across coroutines (session table, global statistics) still need to be locked**—this hasn't changed.

When do you need `strand`? **When two or more coroutines are running on the same connection**. For example, adding a heartbeat coroutine that runs once per second to a session, which shares the same socket with the read coroutine, must be bound to the same `strand`:

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

Here, the `timer` in `heartbeat` is created using `this_coro::executor`, so it is a `strand`-based `timer`, and when it resumes, it also returns to the `strand`—any step of either coroutine will not be concurrent. When running, the client will receive alternating echoes and heartbeats:

```text
已连接服务器
收到: hello
收到: [ping]
收到: [ping]
收到: [ping]
```

The last point that is easy to forget: **`strand` guarantees that "handlers are not concurrent and executed in the order they are submitted," not that "this entire logic is atomic."** When a coroutine `co_await`s and suspends, its current handler returns, and another coroutine queued on the same `strand` will immediately interleave. So critical sections like "read shared variable—suspend—write back" can still be interrupted. Either write the entire section without `co_await`, or use a real lock that is not coroutine-based.

​

### Canceling a coroutine: How to make a coroutine stop

In Part 4, `Session::Stop()` was "close socket + set flag," and the corresponding version in the coroutine is cancellation. Two common triggering methods:

The first is external cancellation. `cancellation_signal` is a signal source, and `bind_cancellation_slot` binds it to the coroutine. Then, when you want to stop, call `emit`:

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

Note that `emit` is not called at the start—it is usually placed in `Stop()`, a timer callback, or a signal handler. The above `kick` is just to make the example run.

The second is automatic cancellation on timeout, wrapping the operation with `cancel_after`, which cancels itself if it hasn't completed by the deadline:

```cpp
asio::steady_timer timer(co_await asio::this_coro::executor);
// timer 设置为 5 秒后才完成
timer.expires_after(5s);
co_await timer.async_wait(asio::cancel_after(1s, asio::use_awaitable));
```

After cancellation, the operation currently `co_await`ing ends with `operation_aborted`, and `co_await` directly throws a `system_error(operation_aborted)`.

Summary:

| **Scenario** | **Action**                           |
| ------ | -------------------------------- |
| Active cancellation | `cancellation_signal` + `emit()` |
| Timeout cancellation | `cancel_after()`                 |
| Cancellation result | `operation_aborted`              |
| Handling method | `try/catch`                      |

***

## Conclusion: Shrinking the id-length-value session from Part 4 into a single function

The session in Part 4 is a three-part state machine: read 4 bytes of header, validate id and length, and then read the message body. It uses `head_parsed_` to remember "where the last read was." The coroutine version compresses it into a single `for` loop—three `co_await`s represent the three steps, and the function's local variables replace the member variables. Below is a fully compilable server, port 14517:

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

The client is also "frame—send—read header—read body" in four steps:

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

The server looks like this (`HandleMessage`'s business code is the same as `LogicSystem` in Part 4, but it no longer goes through the queue and thread):

```text
Server running on port 14517 (Ctrl+C to stop)
New connection accepted
receive msg id: 1 data: hello coroutine
session 结束: End of file
```

The client looks like this:

```text
收到 id=1001 响应: {"data":"server has received msg: hello coroutine","id":1}
```

Comparing item by item with Part 4:

| **Item**   | **Part 4 (Callback)**                              | **This Article (Coroutine)**      |
| ------- | ------------------------------------------ | --------------- |
| Partial packet state | Member variables `head_parsed_` and `recv_head_node_` | None, local variables          |
| Session lifecycle | `shared_from_this()` supports                    | None, socket lives in the coroutine frame |
| Send queue    | `send_queue_` + `send_mutex_` + `writing_` | None, only one write at the same time     |
| Decoupling business thread | `LogicSystem` + queue + condition variable                  | None, business runs directly in the coroutine     |
| Code size for handling sticky packets | Three callback functions + one state machine                             | One `for` loop     |

The cost must also be clear: the business processing now runs on the IO thread. In Part 4, `LogicSystem` was separated out to prevent slow business from blocking the IO thread. To retain this benefit in the coroutine version, you need to manually offload the computation to a thread pool or another executor. Coroutines save code structure, not concurrency model.

***

## Returning from a state machine to a single function

This article can be summarized in three points: `use_awaitable` moves the return value of asynchronous operations to the left of `co_await` and errors into exceptions; a single coroutine is inherently serial, so the `strand` and send queue from Part 4 can be omitted; cancellation is the coroutine version of `Session::Stop()`, `cancellation_signal` executes the entire coroutine, and `cancel_after` sets a limit on a single operation.

Callbacks and coroutines are not substitutes for each other. Callbacks are closer to Asio's underlying model and are unavoidable when writing libraries or doing fine-grained control. Coroutines win in readability, and for long business processes, the code written with coroutines is closest to the human brain's sequential habit. The skeleton from Part 4 still stands in real projects, and this article is just offering a different way to answer the same question.

Exercise: Add a "read timeout" to the above server—any connection that has no data for 60 seconds is kicked off. The hint is `cancel_after`, but be careful: after the timeout, `co_await` will throw an exception, and you need to distinguish between timeout and the other side disconnecting in the `catch`.

## Reference Resources

- Boost.Asio Official Documentation: [https://think-async.com/Asio/](https://think-async.com/Asio/)
- cppreference (C++20 Coroutines): [https://en.cppreference.com/w/cpp/language/coroutines](https://en.cppreference.com/w/cpp/language/coroutines)
- Chapter 6 of Volume 5 of this repository, "Asynchronous I/O and Coroutines": Complete explanation of coroutine language mechanisms
- llfc Blog (C++ Backend Tutorial): [https://llfc.club/](https://llfc.club/)

Part 6 will use Boost.Beast to add a layer of HTTP to the server. At that time, you will find that HTTP message parsing also has its own state machine, and Beast hides this state machine.
