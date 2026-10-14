---
title: "Boost.Asio 学习(1)：基础的客户端服务器"
description: "从最小可运行的同步 echo 服务器入手，讲清 Boost.Asio 的 io_context、endpoint、socket、acceptor 四件套与同步 API 的用法"
chapter: 1
order: 1
tags:
  - 网络编程
  - 异步编程
  - 实战
  - host
  - cpp-modern
  - beginner
difficulty: beginner
platform: host
cpp_standard: "20"
reading_time_minutes: 10
community_category: learning-notes
contributors:
  - github: Karl-Heinrich-Marx-520
    name: 游翰灵
    roles: [author]
---

# Boost.Asio 学习(1)：基础的客户端服务器

> **原创声明**：本文为本人原创学习笔记整理，示例代码均为本人编写并在本地（MinGW g++ 13.1 + Boost 1.91，C++20）编译运行验证
> **主要参考**：[Boost.Asio 官方文档](https://think-async.com/Asio/)、[cppreference](https://en.cppreference.com/)、[llfc 博客](https://llfc.club/)\
> **目标读者**：有 socket 基础概念、想入门 Boost.Asio 的现代 C++ 学习者
> **授权说明**：同意维护者调整标题、格式、放置位置与部分措辞；随仓库以 MIT 许可发布

本篇假设读者已经了解 socket、bind、listen、accept、connect 这些Linux原生API大致在做什么，接下来通过一个最小可运行的同步 echo 服务器与客户端，深入了解 Boost.Asio 把这些流程封装成了什么形状。全文使用同步（阻塞）API，异步读写在第 3 篇展开。

## 完整示例：最小同步 echo

### 服务器

监听 8080 端口、接受一个连接、读一条消息并原样回显：

> 注意：这里假设一次 `read_some` 就能读到完整消息，仅适合本地演示。TCP 是字节流、没有消息边界，生产代码必须按固定长度、分隔符或长度前缀切分；第 4 篇会按长度前缀重做一遍。
>
> 两个示例用同一套错误处理策略：启动阶段（`acceptor` 构造、`socket.connect`）用异常直接失败退出，收发阶段（`read_some` / `write`）用 `error_code` 取原因。

```cpp
// server.cpp
#include <array>
#include <iostream>
#include <string_view>

#include <boost/asio.hpp>

int main() {
    try {
        namespace asio = boost::asio;
        using tcp = asio::ip::tcp;

        asio::io_context io_context;

        // 创建监听器并绑定 0.0.0.0:8080
        // tcp::v4() 配 endpoint 时默认地址即 INADDR_ANY
        // 若端口已被占用，这个构造函数直接抛 system_error
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 8080));
        std::cout << "Server is listening on port 8080...\n";

        tcp::socket socket(io_context);
        acceptor.accept(socket);  // 同步阻塞，直到一个客户端连入
        std::cout << "Client connected from "
                  << socket.remote_endpoint() << "\n";

        // read_some：有多少读多少，不保证读满（详细语义见第 2 篇）
        std::array<char, 1024> recv_buf{};
        boost::system::error_code ec;
        const std::size_t len = socket.read_some(asio::buffer(recv_buf), ec);
        if (ec) {
            std::cerr << "Receive failed: " << ec.message() << "\n";
            return 1;
        }
        std::cout << "Received " << len << " bytes: "
                  << std::string_view(recv_buf.data(), len) << "\n";

        // 原样回显给客户端
        boost::asio::write(socket, asio::buffer(recv_buf.data(), len), ec);
        if (ec) {
            std::cerr << "Send failed: " << ec.message() << "\n";
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

### 客户端

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
        tcp::endpoint endpoint(asio::ip::make_address("127.0.0.1"), 8080);
        socket.connect(endpoint);  // 同步阻塞连接

        const std::string msg = "hello, asio";
        boost::system::error_code ec;
        const std::size_t len = boost::asio::write(socket, asio::buffer(msg), ec);
        if (ec) {
            std::cerr << "Send failed: " << ec.message() << "\n";
            return 1;
        }
        std::cout << "Sent " << len << " bytes\n";

        std::array<char, 1024> recv_buf{};
        const std::size_t rlen = socket.read_some(asio::buffer(recv_buf), ec);
        if (ec) {
            std::cerr << "Receive failed: " << ec.message() << "\n";
            return 1;
        }
        std::cout << "Echo: " << std::string_view(recv_buf.data(), rlen) << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

## 跑起来是什么样

开两个终端，先启动 server.exe（打印 `Server is listening on port 8080...`），再运行 client.exe：

```text
server:
Client connected from 127.0.0.1:xxxxx
Received 11 bytes: hello, asio

client:
Sent 11 bytes
Echo: hello, asio
```

服务器只服务一个连接，处理完就退出。（同步模型一次只服务一个连接，多连接留到第 4 篇）

## 与原生 socket API 的对比

| **原生 API**            | **Boost.Asio 对应**                                             |
| --------------------- | ------------------------------------------------------------- |
| `socket()`            | `tcp::socket socket(io_context)`                              |
| `bind()` + `listen()` | `tcp::acceptor acceptor(io_context, endpoint)`                |
| `accept()`            | `acceptor.accept(socket)`                                     |
| `connect()`           | `socket.connect(endpoint)` / `asio::connect(socket, results)` |
| `send()` / `recv()`   | `write_some` / `read_some`（全局 `write` / `read` 见第 2 篇）        |
| `close(fd)`           | 析构自动关闭，或 `socket.close()`                                     |

封装成类之后，关闭交给析构（RAII），错误处理从 `errno` 换成 `error_code` 或异常。

***

## 逐个拆解

示例里出现的几个类型，下面逐个拆开讲。

### io\_context：执行上下文

`io_context` 是 Asio 的执行上下文。同步模型下它主要提供**执行器（executor）：**`socket`、`acceptor` 等所有 I/O 对象在构造时都要关联一个 executor，它通常就来自 `io_context`——所以本篇所有对象共用同一个 `io_context`。到异步模式（第 3 篇起）它会升级为驱动所有回调的事件循环，真正的价值那时才显现。

本篇所有 I/O 对象共用这一个 `io_context`，构造时它会以 executor 的形式被记进每个对象里。现代 Asio 还允许 `any_io_executor`、`strand` 等其他执行器。

​

### endpoint：地址与端口

endpoint 相当于原生编程里的 `sockaddr_in`，是"IP 地址 + 端口"的组合：

```cpp
tcp::endpoint ep1(asio::ip::make_address("127.0.0.1"), 8080);  // 指定 IP
tcp::endpoint ep2(tcp::v4(), 8080);                            // 任意 IPv4 地址（INADDR_ANY）
```

常用成员：

| **成员**               | **说明**                      |
| -------------------- | --------------------------- |
| address() / port()   | 读取 IP 地址 / 端口号              |
| address(a) / port(p) | 设置 IP 地址 / 端口号              |
| protocol()           | 返回协议类型（如 tcp::v4()）         |
| size() / data()      | 与原生 `sockaddr` 互操作时的字节长度与指针 |

`make_address` 负责把字符串解析成地址，但它只接受 IP 字面量：`"127.0.0.1"` 合法，`"example.com"` 会直接解析失败。解析域名是 `tcp::resolver` 的职责。

​

### socket：连接对象与原生 fd 的对应关系

Boost.Asio 把 socket 抽象为类，TCP 用 `asio::ip::tcp::socket`、UDP 用 `asio::ip::udp::socket`。它对应原生编程里的 fd，构造时必须关联一个执行器（通常是 `io_context`）：

```cpp
tcp::socket socket(io_context);  // 关联 io_context 的执行器；此时尚未连接
```

本篇用到的成员：

| **成员函数**                 | **说明**                  |
| ------------------------ | ----------------------- |
| connect(endpoint)        | 同步连接到指定的 endpoint       |
| read\_some / write\_some | 单次读 / 写，不保证读满或写满        |
| shutdown(what, ec)       | 半关闭：只关读端或只关写端           |
| close() / 析构             | 关闭；`close` 会顺带取消挂起的异步操作 |
| remote\_endpoint()       | 查询对端地址和端口               |

收发的六个函数（send / write\_some / write、receive / read\_some / read）与 `asio::buffer` 的完整语义留到第 2 篇。

​

### acceptor：服务器的监听器

`tcp::acceptor` 是服务器专用类，对应原生流程里的 socket + bind + listen：

```cpp
// 等价于 socket() + bind() + listen()
tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 8080));
```

构造完成即处于监听状态，之后用 accept 接收连接：

```cpp
tcp::socket socket(io_context);
acceptor.accept(socket);  // 同步阻塞；连接建立后 socket 可用于收发
```

常用成员：

| **成员函数**             | **说明**         |
| -------------------- | -------------- |
| accept(socket)       | 同步阻塞，接收一个连接    |
| is\_open() / close() | 判断打开状态 / 关闭监听器 |
| local\_endpoint()    | 获取本地绑定的地址与端口   |

`async_accept`、`cancel`、`native_handle()`、显式 `listen()` / `bind()` 等在后面的异步篇与更完整的示例里再展开。

​

### connect：成员函数与全局函数

TCP 连接有两个容易混淆的 `connect`：

```cpp
// ① socket 的成员函数：只连接一个确定的 endpoint
socket.connect(endpoint);

// ② 全局函数 boost::asio::connect：配合 resolver，依次尝试多个地址直到成功
tcp::resolver resolver(io_context);
auto results = resolver.resolve("example.com", "80");  // 域名 → 多个 endpoint
boost::asio::connect(socket, results);
```

真实客户端拿到的是域名，第一步就是用 resolver 换成 endpoint 列表，把 client.cpp 里的地址换成 `"localhost"` 只要两行：

```cpp
tcp::resolver resolver(io_context);
auto results = resolver.resolve("localhost", "8080");
boost::asio::connect(socket, results);   // 对解析出的多个候选地址逐个尝试
```

`socket.connect(ep)` 只试一个地址，`asio::connect(socket, results)` 则会把 DNS 解析出的多个候选依次试一遍。

***

## 参考资源

- Boost.Asio 官方文档：[https://think-async.com/Asio/](https://think-async.com/Asio/)
- cppreference（同步 I/O 对象与 error\_code 语义）：[https://en.cppreference.com/](https://en.cppreference.com/)
- llfc 博客（C++ 后端教程）：[https://llfc.club/](https://llfc.club/)
