---
title: "Boost.Asio 学习(3)：异步读写"
description: "从同步切到异步：回调在哪个线程执行、回调链上的对象如何保活、io_context::run() 如何驱动事件循环，写一个可同时服务多个连接的异步回声服务器"
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
---

# Boost.Asio 学习(3)：异步读写

> **作者**：游翰灵（Karl-Heinrich-Marx-520） · **原创声明**：本文为本人原创学习笔记整理，示例代码均为本人编写并在本地（MinGW g++ 13.1 + Boost 1.91，C++20）编译运行验证
> **主要参考**：[Boost.Asio 官方文档](https://think-async.com/Asio/)、[cppreference](https://en.cppreference.com/)、[llfc 博客](https://llfc.club/)\
> **目标读者**：读完第 1、2 篇、想从同步转到异步的现代 C++ 学习者
> **授权说明**：同意维护者调整标题、格式、放置位置与部分措辞；随仓库以 MIT 许可发布

第 1、2 篇用的都是同步（阻塞）API：`accept`、`read_some`、`write` 都阻塞当前线程直到完成，一个线程同时只能伺候一个连接。本篇把收发换成异步版本——发起操作后立即返回，完成时由回调通知。伴随这个切换，有三件同步模型里原本不存在的事需要讲清楚：回调在哪个线程执行、回调链上的对象怎么保活、`io_context::run()` 如何驱动一切。读完你能写出一个常驻、可同时服务多个连接的异步回声服务器。全文用单线程异步 API，客户端复用第 1 篇。

## 完整示例：异步回声服务器

服务器由四个文件组成：`Session` 管一条连接的读与回显，`Server` 负责监听并把连接交给会话，`main` 只负责驱动事件循环。先看Session会话类：

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

会话的读、写是两个交替的异步操作，`Session` 只干这一件事。管理所有会话的是 `Server`：

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

最后是入口，把 `io_context` 跑起来：

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

### 客户端

客户端复用第 1 篇的同步写法，只发一条、收一条回显，然后半关闭退出：

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

## 跑起来是什么样

先启动服务器，再运行客户端。服务器常驻，按 Ctrl+C 结束。会话的 uuid 每次随机，下面用省略号占位：

```text
server:
Server running on port 8082. Ctrl+C to stop.
session f31f93e4-... received hello, async
session f31f93e4-...: peer closed gracefully

client:
Echo: hello, async
```

客户端收到的 `hello, async` 正是它发出去的那条；服务器打印的 `peer closed gracefully` 对应客户端的 `shutdown`。

## 线程、保活、驱动

从同步切到异步，看起来只是把 `read_some` 换成 `async_read_some`。但底层多了三件同步模型里根本不会遇到的问题，这也是本篇要逐个拆开讲的三条主线：

| **问题**       | **它到底在解决什么**                 |
| ------------ | ---------------------------- |
| 回调在哪个线程执行    | 谁调用 `run()`，回调就在谁的线程上执行      |
| 对象怎么保活       | 回调真正触发时，它引用的对象必须还活着          |
| `run()` 怎么驱动 | `run()` 靠一份工作计数撑着，计到零自动肯收工返回 |

***

## 逐个拆解

### 异步读写 API：回调签名变了

第 2 篇的同步六函数（`send` / `write_some` / `write`、`receive` / `read_some` / `read`）都有异步版，名字在前面加 `async_`，同样分成「一次调用」和「循环到满」两类：

| **同步**                  | **异步**                              | **归类**        |
| ----------------------- | ----------------------------------- | ------------- |
| `write_some` / `send`   | `async_write_some` / `async_send`   | 一次写，不保证发完     |
| `write`                 | `async_write`                       | 循环写完整个 buffer |
| `read_some` / `receive` | `async_read_some` / `async_receive` | 一次读，不保证读满     |
| `read`                  | `async_read`                        | 循环读满整个 buffer |

它们最大的不同是**调用方式**：同步版返回值就是结果，异步版没有返回值，它需要你在发起时给一个回调，结果通过回调参数送回来

```cpp
// 同步：返回值即结果，当前线程阻塞
std::size_t n = socket.read_some(asio::buffer(buf), ec);

// 异步：立即返回，结果通过回调参数 (ec, n) 送达
socket.async_read_some(asio::buffer(buf),
    [](const boost::system::error_code& ec, std::size_t n) {
        // 完成时才会走到这里
    });
```

读写类回调的签名统一是 `(error_code, bytes_transferred)`。

​

### async\_accept：两版回调

服务器接受连接用 `async_accept`，它有两种回调形态：

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

本篇用版本 A：先建会话、把 accept 落到会话的 socket 成员上。accept 永远是被动等待，对端地址可随时用`socket.remote_endpoint()` 查询。

​

### 回调在哪个线程执行：run() 的线程

一条铁律：**回调执行在调用 `io_context::run()` 的线程里**。

单线程 `run()` 时，所有回调在这个唯一线程里**串行**执行，天然线程安全，所以示例里不加任何锁。多个线程 `run()` 同一个 `io_context` 时，回调可能在不同线程**并发**执行，共享状态就要显式串行化（`strand` 或 `mutex`）。

串行化的专用工具是 `boost::asio::strand`：它把一组回调排队，即使多个线程在 `run()`，同一 strand 里的回调也一个一个执行。单线程模型用不上它，第 4 篇的多线程模型里它是保护会话内部状态的首选。

​

### 对象怎么保活：shared\_from\_this

异步世界里最大的坑是**回调触发时对象已经死了**——异步代码里 `async_read_some` 返回后，我们手上的指针可能早就悬空了。

示例的保活链条是：`Server::sessions_` 用 `shared_ptr` 强持有每个 `Session`，同时每个异步回调都捕获 `self = shared_from_this()`。读写链不断，会话就活着：

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

还要注意同一时刻每个会话只有一个挂起操作。`Start → DoRead → HandleRead → DoWrite → DoRead → …` 是一条读写交替的串行链，这保证了 `buf_` 不会被读和写同时触碰。

​

### run() 靠什么续命：工作计数

`run()` 阻塞当前线程驱动事件循环，**直到 `io_context` 内部的工作计数归零**。计数由两部分构成：每个挂起的异步操作（挂起的 `async_accept`、进行中的读写）各占 1；`make_work_guard(io_context)` 创建的 work guard 也占 1，重置它才释放。

本篇的服务器靠前者续命：accept 回调末尾立刻再挂下一个 `async_accept`，任何时刻都恰好有一个挂起的 accept，计数永不归零，`run()` 因此不返回。这也是一个陷阱——如果哪条代码路径忘记重新挂 accept（比如某个错误分支提前 return），计数归零，`run()` 返回，服务器**静默退出**、没有任何报错。

后面会引入 work guard：把 `run()` 显式「钉住」，直到 `Stop()` 释放。本篇停止服务器的方式是 Ctrl+C 强杀，**优雅退出（信号 + 资源清理）留到第 4 篇**。

​

### eof：异步读的对端关闭

第 2 篇讲的 `eof` 判断在异步里原样成立：`async_read_some` 收到对端关闭时 `ec == eof` 且 `bytes_transferred == 0`。示例里正是用 `if (ec == eof)` 结束会话、从服务器注销。

`Server&` 裸引用为什么安全，这里一并说清：`server` 是 `main` 的栈对象，活到 `main` 结束；所有回调都执行在 `ioc.run()` 期间，`run()` 返回后不会再有回调碰它。后面引入多线程后这个前提要重新审视，那时传递裸引用比较危险，需要升级为传递 `weak_ptr`。

***

## 单线程的边界

本篇的服务器已经是常驻、多连接、按 uuid 注销的异步服务器，不过目前还是**单线程**的：`ioc.run()` 只有一个线程，所有回调串行，一旦某个会话的回调里做了耗时操作，所有连接都被卡住。想要真正榨干多核，得让多个线程同时 `run()`，那时 strand、会话表锁、逻辑线程解耦、粘包协议、优雅退出都要补上。

记住三件事就够了：**回调在 `run()` 的线程里跑、对象要 `shared_from_this()` 保活、`run()` 靠工作计数决定何时返回**。第 4 篇将补齐：多线程 `io_context`、发送队列、粘包协议、逻辑线程与优雅退出。

## 参考资源

- Boost.Asio 官方文档：[https://think-async.com/Asio/](https://think-async.com/Asio/)
- cppreference（异步操作与 error\_code 语义）：[https://en.cppreference.com/](https://en.cppreference.com/)
- llfc 博客（C++ 后端教程）：[https://llfc.club/](https://llfc.club/)
