---
title: "Learning Boost.Asio (3): Asynchronous Read and Write"
description: "Switching from synchronous to asynchronous: which thread executes the callback, how objects on the callback chain are kept alive, how `io_context::run()` drives the event loop, and writing an asynchronous echo server that can serve multiple connections simultaneously"
chapter: 1
order: 3
tags:
  - 网络编程
  - 异步编程
  - 实战
  - host
  - cpp-modern
  - intermediate
difficulty: intermediate
platform: host
cpp_standard: "20"
reading_time_minutes: 14
community_category: learning-notes
contributors:
  - github: Karl-Heinrich-Marx-520
    name: Huanling You
    roles: [author]
translation:
  source: documents/community/incoming/boost-asio-03-async-read-write.md
  source_hash: 2261dbcd6ac685449acd6fdbdd74e4756345fa437cfe22cc40f8fd5b47a4eb7a
  translated_at: '2026-11-22T13:19:04+00:00'
  engine: qwen3:14b
  token_count: 11400
---
# Learning Boost.Asio (3): Asynchronous Read and Write

> **Original Declaration**: This article is an original learning note summary. All example code was written by the author and compiled and run locally (MinGW g++ 13.1 + Boost 1.91, C++20) for verification.
> **Main References**: [Boost.Asio Official Documentation](https://think-async.com/Asio/), [cppreference](https://en.cppreference.com/), [llfc blog](https://llfc.club/)
> **Target Readers**: Readers who have completed the first two articles and want to transition from synchronous to asynchronous programming in modern C++.
> **License Statement**: The author agrees to allow maintainers to adjust the title, format, placement, and some wording; it is released with the MIT license along with the repository.

The first two articles used synchronous (blocking) APIs: `accept`, `read_some`, and `write` all block the current thread until completion, and a single thread can only serve one connection at a time. This article replaces the send and receive operations with asynchronous versions — the operations return immediately after being initiated, and the callback notifies when they complete. With this switch, there are three things that do not exist in the synchronous model that need to be explained clearly: which thread executes the callback, how objects on the callback chain are kept alive, and how `io_context::run()` drives everything. After reading this article, you will be able to write a persistent, asynchronous echo server that can serve multiple connections simultaneously. The entire article uses single-threaded asynchronous APIs, and the client reuses the code from the first article.

## Complete Example: Asynchronous Echo Server

The server consists of four files: `Session` manages the read and echo for a single connection, `Server` is responsible for listening and handing connections over to sessions, and `main` is only responsible for driving the event loop. Let's look at the `Session` class first:

```cpp
// Session.h
#pragma once

#include <array>
#include <memory>
#include <string>

#include <boost/asio.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

class Server;

// 一个连接对应一个会话：负责这一条连接的读、写、回显。
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(boost::asio::io_context& ioc, Server& server);

    boost::asio::ip::tcp::socket& Socket() { return socket_; }
    const std::string& GetUuid() const { return uuid_; }
    void Start();

private:
    void DoRead();
    void HandleRead(const boost::system::error_code& ec, std::size_t bytes_transferred);
    void DoWrite(std::size_t bytes_transferred);

    boost::asio::ip::tcp::socket socket_;
    std::string uuid_;
    Server& server_;
    std::array<char, 1024> buf_{};
};
```

```cpp
// Session.cpp
#include "Session.h"

#include <iostream>
#include <string_view>

#include "Server.h"

Session::Session(boost::asio::io_context& ioc, Server& server)
    : socket_(ioc), server_(server) {
    uuid_ = boost::uuids::to_string(boost::uuids::random_generator()());  // 随机生成id号
}

void Session::Start() {
    DoRead();
}

void Session::DoRead() {
    auto self = shared_from_this();  // 保活：回调触发时对象还在
    socket_.async_read_some(
        boost::asio::buffer(buf_),
        [self](const boost::system::error_code& ec, std::size_t bytes) {
            self->HandleRead(ec, bytes);
        });
}

void Session::HandleRead(const boost::system::error_code& ec,
                         std::size_t bytes_transferred) {
    if (ec == boost::asio::error::eof) {
        std::cout << "session " << uuid_ << ": peer closed gracefully\n";
        server_.RemoveSession(uuid_);  // 在单线程模型里直接调用是安全的
        return;                        // 不再发起读，回调链结束
    }
    else if (ec) {
        std::cout << "session " << uuid_ << ": read error " << ec.message() << "\n";
        server_.RemoveSession(uuid_);
        return;
    }

    std::cout << "session " << uuid_ << " received "
              << std::string_view(buf_.data(), bytes_transferred) << "\n";
    DoWrite(bytes_transferred);
}

void Session::DoWrite(std::size_t bytes_transferred) {
    auto self = shared_from_this();  // 保活
    boost::asio::async_write(
        socket_, boost::asio::buffer(buf_, bytes_transferred),
        [self](const boost::system::error_code& ec, std::size_t) {
            if (ec) {
                std::cout << "session " << self->uuid_ << ": write error "
                          << ec.message() << "\n";
                self->server_.RemoveSession(self->uuid_);
                return;
            }
            self->DoRead();  // 回显完成，进入下一轮读——读、写交替的串行链
        });
}
```

The read and write operations in the session are two alternating asynchronous operations, and the `Session` only does this one thing. The `Server` manages all sessions:

```cpp
// Server.h
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include <boost/asio.hpp>

class Session;

// 监听并管理所有会话：接受连接、登记会话、按 uuid 注销会话。
class Server {
public:
    Server(boost::asio::io_context& ioc, unsigned short port);
    void RemoveSession(const std::string& uuid);

private:
    void DoAccept();

    boost::asio::io_context& ioc_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;  // 只做插入删除
};
```

```cpp
// Server.cpp
#include "Server.h"

#include <iostream>

#include "Session.h"

Server::Server(boost::asio::io_context& ioc, unsigned short port)
    : ioc_(ioc), acceptor_(ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {
    DoAccept();
}

void Server::DoAccept() {
    auto new_session = std::make_shared<Session>(ioc_, *this);
    acceptor_.async_accept(
        new_session->Socket(),
        [this, new_session](const boost::system::error_code& ec) {
            if (!ec) {
                sessions_.emplace(new_session->GetUuid(), new_session);
                new_session->Start();
            } else {
                std::cout << "accept failed: " << ec.message() << "\n";
            }
            DoAccept();  // 继续等下一个连接——accept 回调链同样是「注册 → 完成 → 再注册」
        });
}

void Server::RemoveSession(const std::string& uuid) {
    sessions_.erase(uuid);
}
```

Finally, the entry point runs the `io_context`:

```cpp
// main.cpp
#include <iostream>

#include "Server.h"

int main() {
    try {
        boost::asio::io_context ioc;
        Server server(ioc, 8082);
        std::cout << "Server running on port 8082. Ctrl+C to stop.\n";
        ioc.run();  // 阻塞驱动事件循环：只要还有挂起的异步操作就不会返回
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

### Client

The client reuses the synchronous approach from the first article, sending one message and receiving one echo, then half-closing and exiting:

```cpp
// client.cpp
#include <array>
#include <iostream>
#include <string>
#include <string_view>

#include <boost/asio.hpp>

int main() {
    try {
        namespace asio = boost::asio;
        using tcp = asio::ip::tcp;

        asio::io_context io_context;
        tcp::socket socket(io_context);
        socket.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), 8082));

        const std::string msg = "hello, async";
        boost::system::error_code ec;
        boost::asio::write(socket, asio::buffer(msg), ec);
        if (ec) {
            std::cerr << "Send failed: " << ec.message() << "\n";
            return 1;
        }

        std::array<char, 1024> recv_buf{};
        const std::size_t rlen = socket.read_some(asio::buffer(recv_buf), ec);
        if (ec) {
            std::cerr << "Receive failed: " << ec.message() << "\n";
            return 1;
        }
        std::cout << "Echo: " << std::string_view(recv_buf.data(), rlen) << "\n";

        socket.shutdown(tcp::socket::shutdown_send);  // 告诉服务器「我发完了」
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

## What It Looks Like When Running

Start the server first, then run the client. The server runs continuously, and you can terminate it with Ctrl+C. The session's `uuid` is random each time, and the following uses ellipsis as a placeholder:

```text
server:
Server running on port 8082. Ctrl+C to stop.
session f31f93e4-... received hello, async
session f31f93e4-...: peer closed gracefully

client:
Echo: hello, async
```

The client receives `hello, async`, which is exactly the message it sent; the server's log of `peer closed gracefully` corresponds to the client's `shutdown`.

## Threads, Object Lifetime, and Driving the Event Loop

Switching from synchronous to asynchronous appears to be just replacing `read_some` with `async_read_some`. However, there are three underlying issues that are never encountered in the synchronous model, and these are the three main topics we will address in this article:

| **Problem**       | **What It Actually Solves**                 |
| ------------ | ---------------------------- |
| Which thread executes the callback | The callback is executed on the thread that calls `run()` |
| How objects are kept alive       | When the callback is actually triggered, the objects it references must still be alive |
| How `run()` is driven | `run()` is sustained by a work count, and it returns automatically when the count reaches zero |

***

## Breaking It Down

### Asynchronous Read/Write APIs: The Callback Signature Changes

The six synchronous functions from the second article (`send` / `write_some` / `write`, `receive` / `read_some` / `read`) all have asynchronous versions, with `async_` added to the front of the name, and they are also divided into two categories: "single call" and "loop until full":

| **Synchronous**                  | **Asynchronous**                              | **Classification**        |
| ----------------------- | ----------------------------------- | ------------- |
| `write_some` / `send`   | `async_write_some` / `async_send`   | One-time write, not guaranteed to complete     |
| `write`                 | `async_write`                       | Loop to write the entire buffer |
| `read_some` / `receive` | `async_read_some` / `async_receive` | One-time read, not guaranteed to be full     |
| `read`                  | `async_read`                        | Loop to read the entire buffer |

The biggest difference is the **calling method**: the synchronous version returns the result directly, while the asynchronous version has no return value. Instead, you must provide a callback when initiating the call, and the result is returned through the callback parameters.

```cpp
// 同步：返回值即结果，当前线程阻塞
std::size_t n = socket.read_some(asio::buffer(buf), ec);

// 异步：立即返回，结果通过回调参数 (ec, n) 送达
socket.async_read_some(asio::buffer(buf),
    [](const boost::system::error_code& ec, std::size_t n) {
        // 完成时才会走到这里
    });
```

The signature for read and write callback functions is uniformly `(error_code, bytes_transferred)`.

### `async_accept`: Two Versions of Callback

The server uses `async_accept` to accept connections, and it has two versions of the callback:

```cpp
// 版本 A：自己准备好 socket，accept 完成后它就是那条新连接
tcp::socket socket(io_context);
acceptor.async_accept(socket,
    [](const boost::system::error_code& ec) {
        if (!ec) { /* socket 可用 */ }
    });

// 版本 B：移动接受——不传 socket，成功后回调里才拿到新连接
acceptor.async_accept(
    [](const boost::system::error_code& ec, tcp::socket peer) {
        if (!ec) { /* 把 peer 交给会话对象 */ }
    });
```

This article uses version A: first create a session, then pass the accept operation to the session's socket member. Accept is always passive, and the peer address can be queried at any time using `socket.remote_endpoint()`.

### Which Thread Executes the Callback: The Thread That Calls `run()`

A rule of thumb: **callbacks are executed on the thread that calls `io_context::run()`**.

When `run()` is called on a single thread, all callbacks are executed **serially** on that single thread, which is naturally thread-safe, so no locks are added in the examples. When `run()` is called on multiple threads for the same `io_context`, callbacks may be executed **concurrently** on different threads, and shared state must be explicitly serialized (`strand` or `mutex`).

The specialized tool for serialization is `boost::asio::strand`: it queues a group of callbacks, ensuring that even if multiple threads are running `run()`, callbacks in the same strand are executed one at a time. Single-threaded models don't need it, but in the multi-threaded model from the fourth article, it is the preferred method for protecting session internal state.

### How Objects Are Kept Alive: `shared_from_this`

The biggest pitfall in the asynchronous world is that **the object is already dead when the callback is triggered** — in asynchronous code, after `async_read_some` returns, the pointer in hand may already be dangling.

The survival chain in the example is: `Server::sessions_` uses `shared_ptr` to strongly hold each `Session`, and each asynchronous callback captures `self = shared_from_this()`. As long as the read/write chain continues, the session stays alive:

```cpp
void Session::DoRead() {
    auto self = shared_from_this();  // 让回调持有一份 shared_ptr，对象不提前析构
    socket_.async_read_some(
        boost::asio::buffer(buf_),
        [self](const boost::system::error_code& ec, std::size_t bytes) {
            self->HandleRead(ec, bytes);
        });
}
```

Also, note that at any given time, each session has only one pending operation. The sequence `Start → DoRead → HandleRead → DoWrite → DoRead → …` is a serial read/write chain, ensuring that `buf_` is not accessed by both read and write at the same time.

### How `run()` Is Kept Alive: Work Count

`run()` blocks the current thread to drive the event loop, **until the internal work count of the `io_context` reaches zero**. The count consists of two parts: each pending asynchronous operation (pending `async_accept`, ongoing read/write) contributes 1; the `make_work_guard(io_context)` created work guard also contributes 1, and it is only released when it is reset.

The server in this article is kept alive by the former: at the end of the accept callback, the next `async_accept` is immediately initiated, ensuring that there is always exactly one pending accept at any time, keeping the count from ever reaching zero, and thus `run()` never returns. This is also a trap — if any code path forgets to re-initiate the accept (e.g., if an error branch returns early), the count will reach zero, `run()` will return, and the server will **exit silently** with no error messages.

We will introduce the work guard later: it explicitly "pins" `run()` until `Stop()` releases it. In this article, the server is stopped by Ctrl+C, and **graceful shutdown (signal + resource cleanup) is left for the fourth article**.

### EOF: Remote Closure in Asynchronous Read

The `eof` check from the second article still holds in the asynchronous version: when the remote side closes, `async_read_some` receives `ec == eof` and `bytes_transferred == 0`. In the example, this is used to end the session and deregister it from the server.

Why is the raw `Server&` reference safe? Let's clarify this here: `server` is a stack object in `main`, and it lives until `main` ends. All callbacks are executed during `ioc.run()`, and no callbacks will touch it after `run()` returns. When we introduce multi-threading later, this premise needs to be re-examined, and at that time, passing a raw reference becomes more dangerous and should be upgraded to passing a `weak_ptr`.

***

## The Limits of a Single Thread

The server in this article is already a persistent, multi-connection, asynchronous server that deregisters sessions by UUID. However, it is still **single-threaded**: `ioc.run()` runs on a single thread, all callbacks are executed serially, and if a callback for a session performs a time-consuming operation, all connections are blocked. To truly utilize multiple cores, multiple threads must run `run()` simultaneously, and at that point, `strand`, session table locks, logical thread decoupling, packet sticking protocols, and graceful shutdown must be added.

Remember three things: **callbacks are executed on the thread that calls `run()`, objects must be kept alive using `shared_from_this()`, and `run()` returns based on the work count**. The fourth article will complete the rest: multi-threaded `io_context`, send queues, packet sticking protocols, logical threads, and graceful shutdown.

## Reference Resources

- Boost.Asio Official Documentation: [https://think-async.com/Asio/](https://think-async.com/Asio/)
- cppreference (Asynchronous Operations and `error_code` Semantics): [https://en.cppreference.com/](https://en.cppreference.com/)
- llfc Blog (C++ Backend Tutorials): [https://llfc.club/](https://llfc.club/)
