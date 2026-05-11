---
title: Understanding the Revolutionary Features of C++20 — Coroutines Reference —
  Building a Mini Echo Server Using Coroutines
description: ''
tags:
- cpp-modern
- host
- intermediate
difficulty: intermediate
platform: host
chapter: 10
order: 5
---
# Understanding the Revolutionary Features of C++20 — Coroutines Reference — Building a Mini Echo Server with Coroutines

## Preface

We have successfully built a mini scheduler and its corresponding Task abstraction. Now let's increase the difficulty of our project — using this to build the simplest possible Echo Server.

Of course, expecting to immediately complete a coroutine-based Echo Server is a bit much — after all, jumping straight from a demo to a real-world application is quite a leap. Therefore, we had better take it step by step:

- Boost ASIO has its own coroutine abstraction, so let's look at how Boost ASIO does it.
- We replace it with our own coroutine framework to complete an Echo Server.

## How Boost ASIO Does It

Boost ASIO has its own simple coroutine abstraction. As for the TCP/IP abstraction in Boost ASIO itself, coincidentally, the author wrote a blog post explaining and introducing it a few months ago. Here it is:

> - [Boost ASIO Library In-Depth Study (1)](https://blog.csdn.net/charlie114514191/article/details/148514347)
> - [Boost ASIO Library In-Depth Study (2)](https://blog.csdn.net/charlie114514191/article/details/148517197)
> - [Boost ASIO Library In-Depth Study (3)](https://blog.csdn.net/charlie114514191/article/details/148518447)

So, we won't repeat that content here. If you have any questions about Boost ASIO, just check the blog posts directly.

#### Example Code

```cpp
#include <boost/asio.hpp>
#include <iostream>

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;

awaitable<void> echo(tcp::socket socket) {
    try {
        char data[1024];
        for (;;) {
            // 异步读取数据（协程中以 co_await 方式等待）
            std::size_t n = co_await socket.async_read_some(boost::asio::buffer(data), use_awaitable);
            // 异步写回数据
            co_await boost::asio::async_write(socket, boost::asio::buffer(data, n), use_awaitable);
        }
    } catch (std::exception& e) {
        std::cout << "Client disconnected: " << e.what() << "\n";
    }
}

// 接受客户端连接并为每个新连接 spawn 一个 echo 协程
awaitable<void> listener(tcp::acceptor acceptor) {
    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
        std::cout << "New client connected\n";
        // 将 socket 移动进 echo 协程实例，使用 acceptor 的 executor 启动协程
        co_spawn(acceptor.get_executor(), echo(std::move(socket)), detached);
    }
}

int main() {
    try {
        boost::asio::io_context io;

        tcp::endpoint endpoint(tcp::v4(), 12345);
        tcp::acceptor acceptor(io, endpoint);

        // 启动监听协程（在 io_context 的上下文中）
        co_spawn(io, listener(std::move(acceptor)), detached);

        std::cout << "C++20 Boost.Asio TCP Echo Server running on port 12345...\n";
        io.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
}

```

Interestingly, because Boost ASIO is a library rather than standard syntax, it uses functions for generation and invocation.

#### `awaitable<T>`

`awaitable<T>` is the coroutine return type provided by Boost.Asio (inside a coroutine, you can `co_await` asynchronous operations and ultimately return `T`). In the example, we use `awaitable<void>` to indicate that the coroutine has no explicit return value.

#### `use_awaitable`

This is a completion token that tells Asio: please adapt this asynchronous operation into an `co_await` form. A common usage is:

```cpp
co_await socket.async_read_some(buffer, use_awaitable);

```

This line of code will suspend the coroutine until the asynchronous read completes, then return the number of bytes read (or throw an exception).

#### `co_spawn`

Launches a coroutine task. A common signature is:

```cpp
co_spawn(executor_or_context, coroutine(), completion_token);

```

- The first parameter specifies which executor / io_context the coroutine runs on (related to thread affinity, strand, etc.).
- The last parameter is the completion strategy; `detached` means we don't care about the coroutine's completion result, suitable for long-lived background tasks. You can also pass in a callback to collect errors or results.

#### `detached`

Means "detached" coroutine launch — start it without waiting for its return value (ignoring the result). This is commonly used on the server side when accepting connections and spawning a handler coroutine for each one.

#### `co_await` and Exceptions

When an error occurs, `co_await` inside the coroutine will throw the corresponding exception. Therefore, it's common to wrap the coroutine body in a `try/catch` so that cleanup or logging can be performed when a connection exception occurs (such as the peer disconnecting).

#### Move Semantics and Executor

Note that `listener` accepts a `tcp::acceptor acceptor` (moved by value). When we `co_await acceptor.async_accept(...)` to get a new socket, we use:

```cpp
co_spawn(acceptor.get_executor(), echo(std::move(socket)), detached);

```

to move the socket into the echo coroutine, and we use the acceptor's executor to ensure the coroutine runs in the same execution context (avoiding resource contention across executors).

This code can be compiled quickly —

```bash
g++ -std=c++20 -O2 -pthread main.cpp -lboost_system -o echo_server
./echo_server

```

Test it (using `nc` in another terminal):

```bash
$ nc localhost 12345
hello
hello          # 服务器会回显你发送的内容

```

## Let's Use Our Own?

#### Native Socket Programming

The content above is a Boost demo, and it still looks a bit unfamiliar. Why don't we try building an Echo Server using our own coroutine framework?

Of course, the author needs to emphasize one thing here: if you want to follow along, you must first copy the Socket code from the blog post below — it mainly creates non-blocking I/O. For backup purposes, the author has also placed it in Appendix 1 for your reference.

> - [Asynchronous I/O Programming: Socket RAII Wrapper - charliechen114514's Blog](https://www.charliechen114514.tech/archives/li-jie-c-c-yi-bu-iobian-cheng----zuo-yi-ge-raiide-socketchou-xiang)
> - [Asynchronous I/O Programming: Socket RAII Wrapper - CSDN](https://blog.csdn.net/charlie114514191/article/details/152599584)

So, the author doesn't want to repeat a single word about Socket programming APIs.

#### Using Epoll

To build a coroutine-based Echo Server, we essentially need to wrap Epoll to do our work.

> As for Epoll, coincidentally, the author also has blog posts:
>
> - [Asynchronous I/O Programming: Epoll - charliechen114514's Blog](https://www.charliechen114514.tech/archives/li-jie-c-c-yi-bu-iobian-cheng-ioduo-lu-fu-yong-ji-shu-yu-epollru-men)
> - [Asynchronous I/O Programming: Epoll - CSDN](https://blog.csdn.net/charlie114514191/article/details/152597436)

So we won't cover Epoll itself again. If you find that you're unfamiliar with the content above, you can brush up on it first, and then we'll continue with our code.

## Before We Start

We now need to think about this — we plan to integrate Socket I/O into our coroutines. In previous blog posts, we already built the Task and Scheduler abstractions. In reality, our remaining work is to use I/O readiness as an event notification source — when I/O is ready, we schedule read and write operations.

But we now have a serious problem — the native scheduler doesn't accept event sources to trigger scheduling! So we need to slightly modify the code to convert Epoll events into triggerable coroutine scheduling. For this, an IOManager is needed — we need to modify the Scheduler so that its run loop can embed checks against the epoll singleton. Our work from here on is to:

- Wrap Epoll as a schedulable trigger source, IOManager, that triggers the scheduler's work.
- Modify the scheduler code to introduce IOManager to collect a portion of I/O-ready routines and push them into the ready queue.

## Revising Our Scheduler: Introducing IOManager to Schedule I/O-Mapped Coroutines

Let's not overcomplicate things — we've already arranged a singleton in one thread (in fact, after further discussion with other colleagues yesterday, you can also relax the singleton requirement somewhat, changing from a strict singleton to `thread_local`, which represents an independent scheduler per thread). We'll also make IOManager a singleton here to keep our work simpler.

```cpp
class IOManager : public SingleInstance<IOManager> {
public:
 friend class SingleInstance<IOManager>;
 // register a waiter for (fd, events). If already registered, expand/replace.
 void add_waiter(int fd, uint32_t events, std::coroutine_handle<> h);

 // remove a specific watcher
 void remove_waiter(int fd);

 // poll events, timeout in ms (-1 block)
 void poll(int timeout_ms, std::vector<std::coroutine_handle<>>& out_handles);

 // whether there are any watchers
 bool has_watchers() const;

private:
 IOManager();
 ~IOManager();

 int epoll_fd { -1 };
 struct Waiter {
  uint32_t events;
  std::coroutine_handle<> handle;
 };
 std::unordered_map<int, Waiter> table;
};

```

Waiter is our Epoll event handle. We manipulate IOManager to know exactly which I/O-mapped handles we want to add and remove. This way, we connect events to their corresponding handles through this mechanism.

The work of IOManager is rather tedious. I think you'll understand the following three pieces of code at a glance, so they'll serve as a warm-up:

```cpp
IOManager::IOManager() {
 epoll_fd = epoll_create1(0);
 if (epoll_fd < 0) {
  throw std::runtime_error(std::string("epoll_create1 failed: ") + strerror(errno));
 }
}

IOManager::~IOManager() {
 if (epoll_fd >= 0)
  close(epoll_fd);
}

void IOManager::remove_waiter(int fd) {
 auto it = table.find(fd);
 if (it == table.end())
  return;
 epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
 table.erase(it);
}

```

`add_waiter` is a bit more complex. We need to note that our fd might have already been added to our epoll, but perhaps it hasn't been set to the properties we're interested in.

```cpp
void IOManager::add_waiter(int fd, uint32_t events, std::coroutine_handle<> h) {
 // ensure ET
 uint32_t new_events = events | EPOLLET;
 auto it = table.find(fd);
 if (it == table.end()) {
        // 添加一个映射
  epoll_event ev;
  ev.events = new_events;
  ev.data.fd = fd;
        // 当然，下面的代码是防御性质的，可写可不写，笔者参考别人的代码的
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) != 0) {
   if (errno == EEXIST) {
    // try mod
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) != 0) {
     std::cerr << "epoll_ctl MOD failed in add_waiter: " << strerror(errno) << std::endl;
    }
   } else {
    std::cerr << "epoll_ctl ADD failed in add_waiter: " << strerror(errno) << std::endl;
   }
  }
  table.emplace(fd, Waiter { new_events, h });
 } else { // 我们添加过了，咱们就做新映射而不是从头创建
  // merge events & replace handle
  new_events |= it->second.events;
        // 注册进咱们的epoll
  epoll_event ev;
  ev.events = new_events;
  ev.data.fd = fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) != 0) {
   std::cerr << "epoll_ctl MOD failed in add_waiter: " << strerror(errno) << std::endl;
  }
        // 建立映射句柄
  it->second.events = new_events;
  it->second.handle = h;
 }
}

```

After doing all this, let's not forget that our job is to provide the coroutine interface for I/O preparation in the coroutine scheduler.

```cpp
 // poll events, timeout in ms (-1 block)
 void poll(int timeout_ms, std::vector<std::coroutine_handle<>>& out_handles);

```

In the interface above, the first parameter waits for `timeout_ms` milliseconds to collect the specified I/O tasks, and the second parameter fills `out_handles`. This parameter holds our `std::coroutine_handle<>` ready handle set. Once we clarify this, things become easy.

```cpp
void IOManager::poll(int timeout_ms, std::vector<std::coroutine_handle<>>& out_handles) {
 const int MAX_EVENTS = 64;
 epoll_event events[MAX_EVENTS];
 int n = epoll_wait(epoll_fd, events, MAX_EVENTS, timeout_ms);
 if (n < 0) {
  if (errno == EINTR)
   return;
  // std::cerr << "epoll_wait failed: " << strerror(errno) << std::endl;
  return;
 }
    // OK，收集成功，找出来这些内容，他们也许不再被需要了，不要放在这里，所以我们直接移走
    // 让协程重新掌握所有的对IO的控制权
 for (int i = 0; i < n; ++i) {
  int fd = events[i].data.fd;
  auto it = table.find(fd);
  if (it != table.end()) {
   auto handle = it->second.handle;
   // ET semantics: remove registration and
             // let coroutine re-register if needed
   table.erase(it);
   epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
   if (handle)
    out_handles.push_back(handle);
  }
 }
}

```

With this, we only need to modify a little bit of code:

```cpp
void Schedular::run() {
  while (!ready_coroutines.empty() || !sleepys.empty() || IOManager::instance().has_watchers()) {
   // resume all ready ones first
   while (!ready_coroutines.empty()) {
    auto front_one = ready_coroutines.front();
    ready_coroutines.pop();
    front_one.resume();
   }

   // move expired sleepers to ready queue
   auto now = current();
   while (!sleepys.empty() && sleepys.top().sleep <= now) {
    ready_coroutines.push(sleepys.top().coro_handle);
    sleepys.pop();
   }

   // compute timeout for epoll (ms)
   int timeout_ms = -1;
   if (!ready_coroutines.empty()) {
    timeout_ms = 0; // 大家都在忙，所以不等待有活就览没活干正事
   } else if (!sleepys.empty()) {
    auto next = sleepys.top().sleep;
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(next - now).count();
    timeout_ms = (int)std::max<long long>(0, diff); // 都在睡觉，处理睡觉的
   } else {
    timeout_ms = -1; // block until IO，连睡觉的都没有了，立马处理IO等待
   }

   // POLL IO and collect handles that should be resumed (ET: coroutine will re-register)
   std::vector<std::coroutine_handle<>> ready_from_io;
   IOManager::instance().poll(timeout_ms, ready_from_io);

   // push io-ready handles into ready_coroutines
   for (auto h : ready_from_io) {
    ready_coroutines.push(h);
   }

   // If still nothing ready and there are sleepers, sleep until next sleeper time
   if (ready_coroutines.empty() && !sleepys.empty()) {
    std::this_thread::sleep_until(sleepys.top().sleep);
   }
  }
 }

```

Done.

## Wrapping async_read, async_write, async_accept, and run_server

Let me tell you our interfaces first.

```cpp
using client_comming_callback_t = Task<void> (*)(std::shared_ptr<PassiveClientSocket> socket); // 协程函数接口

void run_server(
    std::shared_ptr<ServerSocket> server_socket,
    client_comming_callback_t callback);

Task<ssize_t> async_read(
    std::shared_ptr<PassiveClientSocket> socket, void* buffer, size_t buffer_size);

Task<ssize_t> async_write(
    std::shared_ptr<PassiveClientSocket> socket, const void* buffer, size_t buffer_size);

```

To effectively reduce confusion, please read the Socket code in Appendix 1 first.

Let's start with the simple parts — wrapping `async_read` and `async_write`. IOManager has already done all the operations for us; what we need to do is wrap an Awaiter semantic for all I/O events. This way, we can let the coroutine know when an I/O operation needs to be suspended because it still has more data to read (placing it into epoll).

Think about it — as the author discussed in the first blog post, when thinking about Awaiter semantics, follow these steps:

- `await_ready`: Do we need to take over the suspension of the coroutine? Yes — we need to do some operations ourselves.
- `await_suspend`: Once we request suspension, we need to add a new event to IOManager. This is our agreement — IOManager removes the event once it gets it, and when the coroutine cares about the event again, it's added back in the Awaiter.
- `await_resume`: When resuming, we do nothing; we just execute the continuation of the previously suspended code.

```cpp
struct WaitForEvent {
 int fd;
 uint32_t events;
 WaitForEvent(int f, uint32_t e)
     : fd(f)
     , events(e) { }
 bool await_ready() { return false; }
 void await_suspend(std::coroutine_handle<> h) {
  IOManager::instance().add_waiter(fd, events, h);
 }
 void await_resume() { }
};

WaitForEvent await_io_event(std::shared_ptr<Socket> socket, uint32_t events) {
 return { socket->internal(), events };
}

WaitForEvent await_io_event(socket_raw_t socket_fd, uint32_t events) {
 return { socket_fd, events };
}

```

`await_io_event` consists of two convenient functions. You'll understand them at a glance — I believe in your skill level.

## Immediately Wrapping `async_read` (and Clarifying the Semantics)

Think about it — if we read all the content *at once*, then obviously there's **no need to suspend**: the coroutine can immediately complete the current asynchronous I/O task, directly `co_return` the number of bytes read, maintaining POSIX-style return semantics (>= 0 means bytes read, 0 means the peer has gracefully closed, -1 means an error occurred and errno is set).

But if `read()` is not ready (returns -1 and `errno == EAGAIN` / `EWOULDBLOCK`), we need to put the corresponding FD into the I/O manager's (`IOManager` / `epoll`) monitoring queue, **suspend the coroutine**, until the scheduler (`Schedular`) wakes it up when it detects that I/O is readable. The wake-up chain is roughly: `epoll_wait()` -> `IOManager` marks the event -> resumes the suspended `coroutine_handle` -> the coroutine continues executing and `read()` retries.

```cpp
Task<ssize_t> async_io::async_read(
    std::shared_ptr<PassiveClientSocket> socket,
    void* buffer, size_t buffer_size) {
 while (true) {
  ssize_t n = socket->read(buffer, buffer_size);
  if (n >= 0) {
   co_return n;
  } else {
   if (errno == EAGAIN || errno == EWOULDBLOCK) {
    co_await await_io_event(socket, EPOLLIN);
    continue;
   } else {
    co_return -1;
   }
  }
 }
}

```

------

## `async_write`: Beyond Simplicity, Watch Out for Partial Writes and Errors

`write()` / `send()` on a non-blocking socket might write only partial data (returning bytes written < requested length), or it might return `EAGAIN` (buffer full). Therefore, `async_write` loops to send until the entire request is written or an unrecoverable error is encountered.

```cpp
Task<ssize_t> async_io::async_write(
    std::shared_ptr<PassiveClientSocket> socket,
    const void* buffer, size_t buffer_size) {
 size_t sent = 0;
 while (sent < buffer_size) {
  ssize_t n = socket->write((const char*)buffer + sent, buffer_size - sent);

  if (n > 0) {
   sent += (size_t)n;
   continue;
  }
  if (n <= 0) {
   if (errno == EAGAIN || errno == EWOULDBLOCK) {
    co_await await_io_event(socket, EPOLLOUT);
    continue;
   } else {
    co_return -1; // 其他错误直接退出
   }
  }
 }
 co_return buffer_size;
}

```

------

## `async_accept` and `run_server`

The complexity of listening / accepting manifests in two parts:

1. `accept()` might return multiple connections at once (there are many completed connections in the kernel queue), or it might return none at all (returning EAGAIN).
2. We want the **accept loop to keep running**, and **each connection's handler to execute concurrently**. We can't let the first connection's handler block the accept loop.

Therefore, our design approach is:

- Provide an `co_accept` coroutine interface: it loops trying to `accept()`, and if none is ready, it `co_await` to wait for `EPOLLIN`.
- In the accept loop (`__accept_loop`), we **`spawn`** the callback coroutine (a new scheduler task) and immediately continue accepting. This way, each handler runs independently and won't block the accept loop.

```cpp
Task<std::shared_ptr<async_io::AsyncPassiveSocket>>
async_io::AsyncServerSocket::__async_accept() {
    while (true) {
        auto client_socket = this->accept();
        if (client_socket) {
            co_return client_socket;
        }
        co_await await_io_event(socket_fd, EPOLLIN);
    }
}

Task<void> __accept_loop(
    std::shared_ptr<ServerSocket> server_socket,
    client_comming_callback_t callback) {

    while (true) {
        auto client_socket = co_await __async_accept(server_socket);
        // 把 client 的处理逻辑并发扔给调度器，不阻塞 accept loop
        Schedular::instance().spawn(callback(client_socket));
    }
}

void async_io::run_server(
    std::shared_ptr<ServerSocket> server_socket,
    client_comming_callback_t callback) {
    Schedular::instance().spawn(
        std::move(__accept_loop(server_socket, callback)));
    Schedular::run(); // 启动事件循环（阻塞当前线程）
}

```

`co_await callback` will "wait for the handler to complete" — this would cause the accept loop to stop at the first connection, preventing subsequent connections from being accepted. `spawn` turns the handler into an independent task, returns immediately, and the accept loop keeps running, supporting concurrent clients.

Let's see the result:

```cpp
#include "async_socket.hpp"
#include "helpers.h"
#include "task.hpp"
#include <format>
#include <memory>
#include <print>

static constexpr const size_t BUFEFR_SIZE = 4096;
Task<void> handle_client(std::shared_ptr<PassiveClientSocket> socket) {
 simple_log("Accepted new client");
 char buf[4096];
 while (true) {
  ssize_t n = co_await async_io::async_read(socket, buf, sizeof(buf));
  simple_log(std::format("Get something special to write: {}", buf));
  if (n > 0) {
   co_await async_io::async_write(socket, buf, n);
  } else {
   simple_log("Client remote shutdown!");
   socket->close(); // no matter close or error, shutdown direct
   co_return;
  }
 }
}

int main(int argc, char** argv) {
 int port = 7000;
 if (argc >= 2)
  port = std::stoi(argv[1]);

 auto server = std::make_shared<ServerSocket>(port);
 int listen_fd = 0;

 try {
  listen_fd = server->listen();
  if (!server->is_valid()) {
   throw "Oh, server is invalid due to non internal sets";
  }
 } catch (const std::exception& e) {
  std::println("Error Occurs: {}", e.what());
  return -1;
 }

 std::println("Echo server listening on port {} (edge-triggered, single-threaded)", port);
 async_io::run_server(server, handle_client);

 server->close();
 return 0;
}

```

#### Leaving a Small Task — Wrap It Into a Class?

# Appendix 1: Asynchronous Non-Coroutine Socket Wrapper

> socket.hpp

```cpp
#pragma once

#include <cstddef>
#include <memory>
#include <sys/types.h>
#include <unistd.h>
using socket_raw_t = int;
static constexpr const socket_raw_t INVALID_SOCK_INTERNAL = -1;

class Socket {
public:
 socket_raw_t internal() const { return socket_fd; }
 virtual ~Socket() {
  close();
 };
 Socket(const socket_raw_t fd);
 Socket(Socket&& other) noexcept
     : socket_fd(other.socket_fd) {
  other.socket_fd = INVALID_SOCK_INTERNAL;
 }
 bool is_valid() const {
  return is_valid(socket_fd);
 }

 static bool is_valid(socket_raw_t fd) {
  return fd != INVALID_SOCK_INTERNAL;
 }

 void close();

protected:
 socket_raw_t socket_fd;
 Socket() = delete;
 Socket(const Socket&) = delete;
 Socket& operator=(Socket&&) = delete;
 Socket& operator=(const Socket&) = delete;
};

class PassiveClientSocket : public Socket {
public:
 PassiveClientSocket(const socket_raw_t fd);

 ssize_t read(void* buffer_ptr, size_t size);
 ssize_t write(const void* buffer_ptr, size_t size);
};

class ServerSocket : public Socket {
public:
 ServerSocket(ServerSocket&&) = default;

 ServerSocket(const int port)
     : Socket(INVALID_SOCK_INTERNAL)
     , open_port(port) {
 }

 socket_raw_t listen();
 int port() const { return open_port; }
 std::shared_ptr<PassiveClientSocket> accept();

private:
 const int open_port;

private:
 ServerSocket() = delete;
 ServerSocket(const ServerSocket&) = delete;
 ServerSocket& operator=(ServerSocket&&) = delete;
 ServerSocket& operator=(const ServerSocket&) = delete;
};

```

> socket.cpp

```cpp
#include "socket.hpp"
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

Socket::Socket(const socket_raw_t fd)
    : socket_fd(fd) { }

void Socket::close() {
 if (!is_valid())
  return;
 ::close(socket_fd);
 socket_fd = INVALID_SOCK_INTERNAL;
}

PassiveClientSocket::PassiveClientSocket(const socket_raw_t fd)
    : Socket(fd) {
}

ssize_t PassiveClientSocket::read(void* buffer_ptr, size_t size) {
 if (!is_valid())
  throw "invalid socket!";
 return ::recv(socket_fd, buffer_ptr, size, 0);
}
ssize_t PassiveClientSocket::write(const void* buffer_ptr, size_t size) {
 if (!is_valid())
  throw "invalid socket!";
 return ::send(socket_fd, buffer_ptr, size, 0);
}

socket_raw_t ServerSocket::listen() {
 int listen_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
 if (listen_fd < 0) {
  throw "Socket Creation Error";
 }

 int opt = 1;
 setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

 sockaddr_in addr {};
 addr.sin_family = AF_INET;
 addr.sin_port = htons(open_port);
 addr.sin_addr.s_addr = INADDR_ANY;

 if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
  throw "Bind Error!";
 }

 if (::listen(listen_fd, SOMAXCONN) != 0) {
  throw "Listen Error!";
 }
 socket_fd = listen_fd;
 return listen_fd;
}

std::shared_ptr<PassiveClientSocket> ServerSocket::accept() {
 sockaddr_in cli {};
 socklen_t cli_len = sizeof(cli);

 int fd = ::accept4(socket_fd, (sockaddr*)&cli, &cli_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
 if (fd < 0) {
  return nullptr;
 }
 return std::make_shared<PassiveClientSocket>(fd);
}

```

# Appendix 2: Core Code

> async_socket.cpp/.hpp

```cpp
#pragma once
#include "socket.hpp"
#include "task.hpp"
#include <cstddef>
#include <memory>
#include <sys/types.h>
using client_comming_callback_t = Task<void> (*)(std::shared_ptr<PassiveClientSocket> socket);

namespace async_io {

class AsyncPassiveSocket : public Socket {
public:
 AsyncPassiveSocket(const socket_raw_t fd);
 ~AsyncPassiveSocket();
 Task<ssize_t> async_read(void* buffer, size_t buffer_size);
 Task<ssize_t> async_write(const void* buffer, size_t buffer_size);

private:
 ssize_t read(void* buffer_ptr, size_t size);
 ssize_t write(const void* buffer_ptr, size_t size);
};

class AsyncServerSocket : public Socket {
public:
 using async_client_comming_callback_t = Task<void> (*)(std::shared_ptr<async_io::AsyncPassiveSocket> socket);
 AsyncServerSocket(AsyncServerSocket&&) = default;

 AsyncServerSocket(const int port)
     : Socket(INVALID_SOCK_INTERNAL)
     , open_port(port) {
 }

 socket_raw_t listen();
 void run_server(async_client_comming_callback_t callback);
 int port() const { return open_port; }

private:
 const int open_port;

private:
 std::shared_ptr<async_io::AsyncPassiveSocket> accept();
 Task<std::shared_ptr<async_io::AsyncPassiveSocket>> __async_accept();
 Task<void> __accept_loop(
     async_io::AsyncServerSocket::async_client_comming_callback_t callback);
 AsyncServerSocket() = delete;
 AsyncServerSocket(const AsyncServerSocket&) = delete;
 AsyncServerSocket& operator=(AsyncServerSocket&&) = delete;
 AsyncServerSocket& operator=(const AsyncServerSocket&) = delete;
};

void run_server(
    std::shared_ptr<ServerSocket> server_socket,
    client_comming_callback_t callback);

Task<ssize_t> async_read(
    std::shared_ptr<PassiveClientSocket> socket, void* buffer, size_t buffer_size);

Task<ssize_t> async_write(
    std::shared_ptr<PassiveClientSocket> socket, const void* buffer, size_t buffer_size);

}

```

```cpp
#include "async_socket.hpp"
#include "helpers.h"
#include "schedular.hpp"
#include "socket.hpp"
#include "task.hpp"
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
// using async_io::WaitForEvent;

namespace {

struct WaitForEvent {
 int fd;
 uint32_t events;
 WaitForEvent(int f, uint32_t e)
     : fd(f)
     , events(e) { }
 bool await_ready() { return false; }
 void await_suspend(std::coroutine_handle<> h) {
  IOManager::instance().add_waiter(fd, events, h);
 }
 void await_resume() { }
};

WaitForEvent await_io_event(std::shared_ptr<Socket> socket, uint32_t events) {
 return { socket->internal(), events };
}

WaitForEvent await_io_event(socket_raw_t socket_fd, uint32_t events) {
 return { socket_fd, events };
}

};

async_io::AsyncPassiveSocket::AsyncPassiveSocket(socket_raw_t fd)
    : Socket(fd) {
}

async_io::AsyncPassiveSocket::~AsyncPassiveSocket() {
 IOManager::instance().remove_waiter(socket_fd);
 this->close();
}

ssize_t async_io::AsyncPassiveSocket::read(void* buffer, size_t buffer_size) {
 if (!is_valid())
  throw "invalid socket!";
 ssize_t n;
 do {
  n = ::recv(socket_fd, buffer, buffer_size, 0);
 } while (n < 0 && errno == EINTR);
 return n;
}

ssize_t async_io::AsyncPassiveSocket::write(const void* buffer, size_t buffer_size) {
 if (!is_valid())
  throw "invalid socket!";
 return ::send(socket_fd, buffer, buffer_size, 0);
}

Task<ssize_t> async_io::AsyncPassiveSocket::async_read(void* buffer, size_t buffer_size) {
 while (true) {
  ssize_t n = read(buffer, buffer_size);
  if (n >= 0) {
   co_return n;
  } else {
   if (errno == EAGAIN || errno == EWOULDBLOCK) {
    co_await await_io_event(socket_fd, EPOLLIN);
    continue;
   } else {
    co_return -1;
   }
  }
 }
}

Task<ssize_t> async_io::AsyncPassiveSocket::async_write(const void* buffer, size_t buffer_size) {
 size_t sent = 0;
 while (sent < buffer_size) {
  ssize_t n = write((const char*)buffer + sent, buffer_size - sent);

  if (n > 0) {
   sent += (size_t)n;
   continue;
  }
  if (n <= 0) {
   if (errno == EAGAIN || errno == EWOULDBLOCK) {
    co_await await_io_event(socket_fd, EPOLLOUT);
    continue;
   } else {
    co_return -1; // quit
   }
  }
 }
 co_return buffer_size;
}

socket_raw_t async_io::AsyncServerSocket::listen() {
 int listen_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
 if (listen_fd < 0) {
  throw "Socket Creation Error";
 }

 int opt = 1;
 setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

 sockaddr_in addr {};
 addr.sin_family = AF_INET;
 addr.sin_port = htons(open_port);
 addr.sin_addr.s_addr = INADDR_ANY;

 if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) != 0) {
  throw "Bind Error!";
 }

 if (::listen(listen_fd, SOMAXCONN) != 0) {
  throw "Listen Error!";
 }
 socket_fd = listen_fd;
 return listen_fd;
}

Task<void> async_io::AsyncServerSocket::__accept_loop(
    async_io::AsyncServerSocket::async_client_comming_callback_t callback) {
 while (true) {
  auto client_socket = co_await __async_accept();
  Schedular::instance().spawn(callback(client_socket));
 }
}

void async_io::AsyncServerSocket::run_server(async_client_comming_callback_t callback) {
 try {
  listen();
 } catch (...) {
  // log errors
  simple_log("Error happens when listen!");
  return;
 }

 Schedular::instance().spawn(
     std::move(__accept_loop(callback)));
 Schedular::run();
}

std::shared_ptr<async_io::AsyncPassiveSocket> async_io::AsyncServerSocket::accept() {
 sockaddr_in cli {};
 socklen_t cli_len = sizeof(cli);

 int fd = ::accept4(socket_fd, (sockaddr*)&cli, &cli_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
 if (fd < 0) {
  return nullptr;
 }
 return std::make_shared<async_io::AsyncPassiveSocket>(fd);
}

Task<std::shared_ptr<async_io::AsyncPassiveSocket>>
async_io::AsyncServerSocket::__async_accept() {
 while (true) {
  auto client_socket = this->accept();
  if (client_socket) {
   co_return client_socket;
  }
  co_await await_io_event(socket_fd, EPOLLIN);
 }
}

/*-------- Helpers if you use Sync Sockets ----------*/
namespace {
Task<std::shared_ptr<PassiveClientSocket>>
__async_accept(std::shared_ptr<ServerSocket> server_socket) {
 while (true) {
  auto client_socket = server_socket->accept();
  if (client_socket) {
   co_return client_socket;
  }
  co_await await_io_event(server_socket, EPOLLIN);
 }
}

Task<void> __accept_loop(
    std::shared_ptr<ServerSocket> server_socket,
    client_comming_callback_t callback) {
 while (true) {
  auto client_socket = co_await __async_accept(server_socket);
  Schedular::instance().spawn(callback(client_socket));
 }
}
}

void async_io::run_server(
    std::shared_ptr<ServerSocket> server_socket,
    client_comming_callback_t callback) {
 Schedular::instance().spawn(
     std::move(__accept_loop(server_socket, callback)));
 Schedular::run();
}

Task<ssize_t> async_io::async_read(
    std::shared_ptr<PassiveClientSocket> socket,
    void* buffer, size_t buffer_size) {
 while (true) {
  ssize_t n = socket->read(buffer, buffer_size);
  if (n >= 0) {
   co_return n;
  } else {
   if (errno == EAGAIN || errno == EWOULDBLOCK) {
    co_await await_io_event(socket, EPOLLIN);
    continue;
   } else {
    co_return -1;
   }
  }
 }
}

Task<ssize_t> async_io::async_write(
    std::shared_ptr<PassiveClientSocket> socket,
    const void* buffer, size_t buffer_size) {
 size_t sent = 0;
 while (sent < buffer_size) {
  ssize_t n = socket->write((const char*)buffer + sent, buffer_size - sent);

  if (n > 0) {
   sent += (size_t)n;
   continue;
  }
  if (n <= 0) {
   if (errno == EAGAIN || errno == EWOULDBLOCK) {
    co_await await_io_event(socket, EPOLLOUT);
    continue;
   } else {
    co_return -1; // 其他错误直接退出
   }
  }
 }
 co_return buffer_size;
}

```

> IOManager.hpp

```cpp
#pragma once

#include "single_instance.hpp"
#include <coroutine>
#include <cstdint>
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>

class IOManager : public SingleInstance<IOManager> {
public:
 friend class SingleInstance<IOManager>;
 // register a waiter for (fd, events). If already registered, expand/replace.
 void add_waiter(int fd, uint32_t events, std::coroutine_handle<> h);

 // remove a specific watcher
 void remove_waiter(int fd);

 // poll events, timeout in ms (-1 block)
 void poll(int timeout_ms, std::vector<std::coroutine_handle<>>& out_handles);

 // whether there are any watchers
 bool has_watchers() const;

private:
 IOManager();
 ~IOManager();

 int epoll_fd { -1 };

 struct Waiter {
  uint32_t events;
  std::coroutine_handle<> handle;
 };

 std::unordered_map<int, Waiter> table;
};

```

> IOManager.cpp

```cpp
#include "io_manager.hpp"
#include <cstring>
#include <errno.h>
#include <iostream>
#include <stdexcept>
#include <sys/epoll.h>
#include <unistd.h>

IOManager::IOManager() {
 epoll_fd = epoll_create1(0);
 if (epoll_fd < 0) {
  throw std::runtime_error(std::string("epoll_create1 failed: ") + strerror(errno));
 }
}

IOManager::~IOManager() {
 if (epoll_fd >= 0)
  close(epoll_fd);
}

void IOManager::add_waiter(int fd, uint32_t events, std::coroutine_handle<> h) {
 // ensure ET
 uint32_t new_events = events | EPOLLET;
 auto it = table.find(fd);
 if (it == table.end()) {
  epoll_event ev;
  ev.events = new_events;
  ev.data.fd = fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) != 0) {
   if (errno == EEXIST) {
    // try mod
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) != 0) {
     std::cerr << "epoll_ctl MOD failed in add_waiter: " << strerror(errno) << std::endl;
    }
   } else {
    std::cerr << "epoll_ctl ADD failed in add_waiter: " << strerror(errno) << std::endl;
   }
  }
  table.emplace(fd, Waiter { new_events, h });
 } else {
  // merge events & replace handle
  new_events |= it->second.events;
  epoll_event ev;
  ev.events = new_events;
  ev.data.fd = fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) != 0) {
   std::cerr << "epoll_ctl MOD failed in add_waiter: " << strerror(errno) << std::endl;
  }
  it->second.events = new_events;
  it->second.handle = h;
 }
}

void IOManager::remove_waiter(int fd) {
 auto it = table.find(fd);
 if (it == table.end())
  return;
 epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
 table.erase(it);
}

void IOManager::poll(int timeout_ms, std::vector<std::coroutine_handle<>>& out_handles) {
 const int MAX_EVENTS = 64;
 epoll_event events[MAX_EVENTS];
 int n = epoll_wait(epoll_fd, events, MAX_EVENTS, timeout_ms);
 if (n < 0) {
  if (errno == EINTR)
   return;
  // std::cerr << "epoll_wait failed: " << strerror(errno) << std::endl;
  return;
 }
 for (int i = 0; i < n; ++i) {
  int fd = events[i].data.fd;
  auto it = table.find(fd);
  if (it != table.end()) {
   auto handle = it->second.handle;
   // ET semantics: remove registration and let coroutine re-register if needed
   table.erase(it);
   epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
   if (handle)
    out_handles.push_back(handle);
  }
 }
}

bool IOManager::has_watchers() const {
 return !table.empty();
}

```
