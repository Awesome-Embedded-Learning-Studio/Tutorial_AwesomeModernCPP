---
title: "Learning Boost.Asio (1): Basic Client-Server"
description: "Starting from the smallest working synchronous echo server, this article explains the four core components of Boost.Asio — io_context, endpoint, socket, acceptor — and the usage of synchronous APIs."
chapter: 1
order: 1
tags:
  - Network Programming
  - Asynchronous Programming
  - Practical Examples
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
    name: Hanling You
    roles: [author]
translation:
  source: documents/community/incoming/boost-asio-01-basic-client-server.md
  source_hash: 7d29c0f940f0a668fcd8a118920b5685f298a7a04488e7f18f9c7df7c3f420a4
  translated_at: '2026-11-22T13:18:00+00:00'
  engine: qwen3:14b
  token_count: 8347
---
# Learning Boost.Asio (1): Basic Client-Server

> **Original Declaration**: This article is an original learning note summary, and all example codes were written by the author and compiled and run locally (MinGW g++ 13.1 + Boost 1.91, C++20) for verification.
> **Main References**: [Boost.Asio Official Documentation](https://think-async.com/Asio/), [cppreference](https://en.cppreference.com/), [llfc blog](https://llfc.club/)
> **Target Readers**: Readers who have basic knowledge of sockets and want to get started with Boost.Asio in modern C++.
> **License Statement**: The author agrees to allow maintainers to adjust the title, format, placement, and some wording; this article is released with the MIT license along with the repository.

This article assumes that the reader is familiar with the general functions of Linux native APIs such as `socket`, `bind`, `listen`, `accept`, and `connect`. We will then delve into how Boost.Asio encapsulates these processes by building a minimal working synchronous echo server and client. This article uses synchronous (blocking) APIs; asynchronous read/write operations will be covered in Part 3.

## Complete Example: Minimal Synchronous Echo

### Server

Listen on port 8080, accept a connection, read a message, and echo it back:

> Note: Here we assume that a single `read_some` call will read the complete message, which is only suitable for local demonstration. TCP is a byte stream and does not have message boundaries; production code must split messages based on fixed length, delimiters, or length prefixes. Part 4 will redo this with a length prefix.
>
> Both examples use the same error handling strategy: during the startup phase (`acceptor` construction, `socket.connect`), exceptions are used to directly fail and exit. During the sending and receiving phase (`read_some` / `write`), `error_code` is used to retrieve the reason.

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

### Client

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

## How It Runs

Open two terminals, first start `server.exe` (which prints `Server is listening on port 8080...`), then run `client.exe`:

```text
server:
Client connected from 127.0.0.1:xxxxx
Received 11 bytes: hello, asio

client:
Sent 11 bytes
Echo: hello, asio
```

The server serves only one connection and exits after processing it. (The synchronous model serves only one connection at a time; handling multiple connections will be covered in Part 4.)

## Comparison with Native Socket APIs

| **Native API**        | **Boost.Asio Equivalent**                                       |
| ------------------- | ------------------------------------------------------------- |
| `socket()`          | `tcp::socket socket(io_context)`                              |
| `bind()` + `listen()` | `tcp::acceptor acceptor(io_context, endpoint)`                |
| `accept()`          | `acceptor.accept(socket)`                                     |
| `connect()`         | `socket.connect(endpoint)` / `asio::connect(socket, results)` |
| `send()` / `recv()` | `write_some` / `read_some` (global `write` / `read` in Part 2) |
| `close(fd)`         | Automatically closed on destruction, or `socket.close()`      |

After encapsulation into classes, closing is handled by destructors (RAII), and error handling is moved from `errno` to `error_code` or exceptions.

***

## Breakdown of Components

The several types used in the example are explained below one by one.

### io_context: Execution Context

`io_context` is the execution context in Asio. In the synchronous model, it mainly provides an **executor**: all I/O objects such as `socket` and `acceptor` must be associated with an executor when they are constructed, which usually comes from `io_context` — hence, all objects in this article share the same `io_context`. In the asynchronous mode (starting from Part 3), it will evolve into an event loop that drives all callbacks, and its true value will become apparent then.

All I/O objects in this article share the same `io_context`, and it is recorded in each object as an executor during construction. Modern Asio also allows other executors such as `any_io_executor` and `strand`.

### endpoint: Address and Port

An endpoint is equivalent to `sockaddr_in` in native programming, representing a combination of "IP address + port":

```cpp
tcp::endpoint ep1(asio::ip::make_address("127.0.0.1"), 8080);  // 指定 IP
tcp::endpoint ep2(tcp::v4(), 8080);                            // 任意 IPv4 地址（INADDR_ANY）
```

Common members:

| **Member**           | **Description**                          |
| ------------------ | -------------------------------------- |
| address() / port() | Read IP address / port number              |
| address(a) / port(p) | Set IP address / port number              |
| protocol()         | Returns the protocol type (e.g., `tcp::v4()`) |
| size() / data()    | Byte length and pointer for interoperability with native `sockaddr` |

`make_address` is responsible for parsing a string into an address, but it only accepts IP literals: `"127.0.0.1"` is valid, while `"example.com"` will fail to parse directly. Resolving domain names is the responsibility of `tcp::resolver`.

### socket: Connection Object and Native fd Mapping

Boost.Asio abstracts the socket as a class. For TCP, it uses `asio::ip::tcp::socket`, and for UDP, it uses `asio::ip::udp::socket`. It corresponds to the native file descriptor (fd), and must be associated with an executor (usually `io_context`) during construction:

```cpp
tcp::socket socket(io_context);  // 关联 io_context 的执行器；此时尚未连接
```

Members used in this article:

| **Member Function**     | **Description**                         |
| -------------------- | ------------------------------------- |
| connect(endpoint)     | Synchronously connects to a specified endpoint |
| read_some / write_some | Single read / write, not guaranteed to be fully read or written |
| shutdown(what, ec)    | Half-close: close only the read or write end |
| close() / destructor  | Close; `close` also cancels pending asynchronous operations |
| remote_endpoint()    | Query the peer's address and port         |

The full semantics of the six send/receive functions (`send` / `write_some` / `write`, `receive` / `read_some` / `read`) and `asio::buffer` will be covered in Part 2.

### acceptor: Server Listener

`tcp::acceptor` is a class specific to servers, corresponding to the native process of `socket + bind + listen`:

```cpp
// 等价于 socket() + bind() + listen()
tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 8080));
```

Once constructed, it is in a listening state, and then uses `accept` to receive connections:

```cpp
tcp::socket socket(io_context);
acceptor.accept(socket);  // 同步阻塞；连接建立后 socket 可用于收发
```

Common members:

| **Member Function** | **Description**             |
| ----------------- | ------------------------- |
| accept(socket)    | Synchronously blocks and accepts one connection |
| is_open() / close() | Checks open status / closes the listener |
| local_endpoint()  | Gets the locally bound address and port |

`async_accept`, `cancel`, `native_handle()`, and explicit `listen()` / `bind()` will be covered in the asynchronous section and more complete examples later.

### connect: Member Function and Global Function

There are two `connect` functions in TCP that are easy to confuse:

```cpp
// ① socket 的成员函数：只连接一个确定的 endpoint
socket.connect(endpoint);

// ② 全局函数 boost::asio::connect：配合 resolver，依次尝试多个地址直到成功
tcp::resolver resolver(io_context);
auto results = resolver.resolve("example.com", "80");  // 域名 → 多个 endpoint
boost::asio::connect(socket, results);
```

In real clients, the first step is to use a resolver to convert the domain name into a list of endpoints. Changing the address in `client.cpp` to `"localhost"` only requires two lines of code:

```cpp
tcp::resolver resolver(io_context);
auto results = resolver.resolve("localhost", "8080");
boost::asio::connect(socket, results);   // 对解析出的多个候选地址逐个尝试
```

`socket.connect(ep)` tries only one address, while `asio::connect(socket, results)` tries all the candidate endpoints parsed from DNS one by one.

***

## Reference Resources

- Boost.Asio Official Documentation: [https://think-async.com/Asio/](https://think-async.com/Asio/)
- cppreference (synchronous I/O objects and error_code semantics): [https://en.cppreference.com/](https://en.cppreference.com/)
- llfc blog (C++ backend tutorial): [https://llfc.club/](https://llfc.club/)
