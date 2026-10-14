---
title: "Boost.Asio 学习(2)：同步读写"
description: "讲全同步收发 API：send / write_some / write 与 receive / read_some / read 六个函数的区别，以及 asio::buffer、eof 判断与按分隔符收帧"
chapter: 1
order: 2
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
reading_time_minutes: 12
community_category: learning-notes
contributors:
  - github: Karl-Heinrich-Marx-520
    name: 游翰灵
    roles: [author]
---

# Boost.Asio 学习(2)：同步读写

> **原创声明**：本文为本人原创学习笔记整理，示例代码均为本人编写并在本地（MinGW g++ 13.1 + Boost 1.91，C++20）编译运行验证
> **主要参考**：[Boost.Asio 官方文档](https://think-async.com/Asio/)、[cppreference](https://en.cppreference.com/)、[llfc 博客](https://llfc.club/)\*\*\*\*
>
> **目标读者**：有 socket 基础概念、想入门 Boost.Asio 的现代 C++ 学习者；建议先读第 1 篇
> **授权说明**：同意维护者调整标题、格式、放置位置与部分措辞；随仓库以 MIT 许可发布

第 1 篇里客户端发一条、服务器回一条就结束了，那条消息能一次读完，纯属消息足够小。本篇把同步收发的 API 讲全：发送的 `send` / `write_some` / `write`，接收的 `receive` / `read_some` / `read`，以及它们共同依赖的 `asio::buffer`。读完你会知道六个函数各管什么、返回值怎么读、对端关闭怎么判断，以及怎么按分隔符收下一帧完整消息。本篇简化错误处理，关注核心逻辑。

## 完整示例：对比 read 与 read\_some

服务端用 256 字节的小缓冲循环 `read_some` 回显（有多少读多少），客户端发 1000 字节、用 `read` 一次读满 1000（循环直到读满），最后半关闭触发服务端的 `eof`。

### 服务器

```cpp
// sync_server.cpp —— 循环 read_some 回显；小缓冲会逼出"部分读"
#include <array>
#include <iostream>

#include <boost/asio.hpp>

int main() {
    try {
        namespace asio = boost::asio;
        using tcp = asio::ip::tcp;

        asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 8081));
        std::cout << "Echo server listening on port 8081...\n";

        tcp::socket socket(io_context);
        acceptor.accept(socket);
        std::cout << "Client connected from " << socket.remote_endpoint() << "\n";

        std::array<char, 256> buf{};  // 故意用小缓冲：一条消息要读好几轮
        std::size_t total = 0;
        for (;;) {
            boost::system::error_code ec;
            const std::size_t len = socket.read_some(asio::buffer(buf), ec);
            if (ec == asio::error::eof) {
                std::cout << "Peer closed connection (eof). Echoed "
                          << total << " bytes in total.\n";
                break;
            }
            if (ec) {
                std::cerr << "Receive failed: " << ec.message() << "\n";
                return 1;
            }
            total += len;
            boost::asio::write(socket, asio::buffer(buf.data(), len));  // 收多少回多少
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
// sync_client.cpp —— write 完整写、read 读满、shutdown_send 触发对端 eof
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
        socket.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), 8081));

        // write：内部循环 write_some，直到 1000 字节全部写完
        const std::string msg(1000, 'x');
        const std::size_t sent = boost::asio::write(socket, asio::buffer(msg));
        std::cout << "Sent " << sent << " bytes\n";

        // read：内部循环 read_some，直到读满 1000 字节（服务端分了 4 块回，read 替你拼齐）
        std::array<char, 1000> echo{};
        const std::size_t received = boost::asio::read(socket, asio::buffer(echo));
        std::cout << "Received " << received << " bytes, content "
                  << (std::string_view(echo.data(), received) == msg ? "matches" : "MISMATCH")
                  << "\n";

        // 半关闭：告诉服务器"我发完了"。服务器下一次 read_some 会看到 eof
        socket.shutdown(tcp::socket::shutdown_send);
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

## 跑起来是什么样

先启动 `sync_server.exe`，再运行 `sync_client.exe`：

```text
sync_server:
Echo server listening on port 8081...
Client connected from 127.0.0.1:xxxxx
Peer closed connection (eof). Echoed 1000 bytes in total.

sync_client:
Sent 1000 bytes
Received 1000 bytes, content matches
```

服务端的"Echoed 1000 bytes"证明 1000 字节被 256 的小缓冲分块读齐；客户端的 `matches` 证明 `read` 原样拼回了完整消息。

## 六个函数总览：一次调用 vs 循环到满

六个函数不是六个并列的东西，而是两类——**"给你多少算多少"的一次调用**，&#x548C;**"内部循环直到满"的全局函数**：

| **函数**                  | **类型** | **归类**        | **典型场景**  |
| ----------------------- | ------ | ------------- | --------- |
| `write_some` / `send`   | 成员函数   | 一次写，不保证发完     | 自己控制写节奏   |
| `read_some` / `receive` | 成员函数   | 一次读，不保证读满     | 配合自定义边界逻辑 |
| `write`                 | 全局函数   | 循环写完整个 buffer | 默认选择      |
| `read`                  | 全局函数   | 循环读满整个 buffer | 已知长度的完整消息 |

一条判断规则：**需要"凑齐 N 字节"就用全局 `write` / `read`；需要自己掌控节奏，用成员版的 `*_some`。** 这条划分也是后面理解异步 API 的地图——第 3 篇的 `async_write_some` / `async_write` 正是同样的一对。

***

## 逐个拆解

先从最基础的一步说起：往里传什么。

### asio::buffer

收发函数都不直接接受 `std::string` 或数组，而是接受 `asio::buffer(...)` 创建的缓冲对象——它只描述"从哪块内存开始、有多长"，不拥有内存：

```cpp
char raw[64];                              // 可写内存
auto wbuf = asio::buffer(raw);             // mutable_buffer：用于收（数据写进这里）

const char* data = "hello";
auto cbuf = asio::buffer(data, 5);         // const_buffer：用于发（只读）
```

收（read / receive）要传可写缓冲 mutable\_buffer，发（write / send）要传只读缓冲 const\_buffer。不过其实 `asio::buffer(...)` 根据实参的常量性自动选择，不需要手动区分。

不手动指定长度时，`buffer` 按实参类型推导：

| **实参**                            | **推导出的长度** | **说明**                     |
| --------------------------------- | ---------- | -------------------------- |
| `char[N]` / `std::array<char, N>` | N（整个数组）    | 注意是数组全长，不是字符串长度            |
| `std::vector<char>`               | `size()`   | ​                          |
| `std::string`                     | `size()`   | ​                          |
| 裸指针 `char*`                       | 无法推导       | 必须手动给长度：`buffer(ptr, len)` |

传裸指针 char\* 是个经典误区：

```cpp
char data[100] = "hello";
socket.send(asio::buffer(data));    // 发送 100 字节！"hello" 后面的 95 个 '\0' 会被原样发出去
socket.send(asio::buffer(data, 5)); // 只发 "hello" —— 需要精确长度时手动指定
```

`data` 的后 95 个字节是聚合初始化的 `'\0'`（不是垃圾值），第一种用法下它们仍会被发送，因为数组形式的 buffer 自动传数组全长，所以只发有效内容就需要手动给长度。

​

### 一次调用：有多少算多少

四个成员函数都只做一次系统调用，返回多少就是多少：

```cpp
// 主角是这两个：write_some 尽力写（可能只写一部分），read_some 有多少读多少
std::size_t n = socket.write_some(asio::buffer(msg), ec);
std::size_t n = socket.read_some(asio::buffer(buf), ec);

// send / receive 与它们一一对应，只是多一个底层标志位，实战里基本用不上
std::size_t n = socket.send(asio::buffer(msg), 0, ec);
std::size_t n = socket.receive(asio::buffer(buf), 0, ec);
```

`read_some` 的"some"就是字面意思：**内核接收缓冲里此刻有什么就搬什么，可能就一个字节**。第 1 篇的回显一次就读到了完整消息，纯属消息足够小。

发送侧同理："部分写"在同步阻塞模式下很少见（发送缓冲区满时才会发生），但 `write_some` / `send` 的返回值是本次实际写入的字节数，返回小于请求长度是合法的，为了应对这种情况要么自己循环补发，要么直接用 `write`。

​

### 循环到满：write 与 read

两个全局函数在内部循环调用 `*_some`，会替你凑满 buffer：

```cpp
// write：内部循环 write_some，直到整个 buffer 写完
std::size_t n = boost::asio::write(socket, asio::buffer(msg), ec);

// read：内部循环 read_some，直到读满整个 buffer
std::size_t n = boost::asio::read(socket, asio::buffer(buf), ec);
```

"读满"的目标由你给的 buffer 长度决定：给 1000 字节的数组，它就读到 1000 字节为止。

​

### 结果怎么判：返回值与 error\_code

六个函数都提供异常版和 `error_code` 版，但出错时的返回值规则分两套——正好对应上面"一次调用"与"循环到满"的划分：

| **情况**    | **异常版**                         | **error\_code 版**                    |
| --------- | ------------------------------- | ------------------------------------ |
| 成功        | 返回传输的字节数                        | 同左                                   |
| 出错（一次调用类） | 抛 `boost::system::system_error` | 返回 0，错误在 `ec` 里                      |
| 出错（循环到满类） | 抛 `boost::system::system_error` | 返回出错前已传输的字节数（可能是部分值，需要处理），错误在 `ec` 里 |

​

### 结束怎么知道：eof 与半关闭

对端**正常关闭**（发来 FIN）时，读操作以 `boost::asio::error::eof` 收尾；连接被**异常重置**（RST）时则是 `connection_reset`。判断永远看 `ec`，不要看返回值是否为 0。两个家族的 `eof` 行为还不一样。

**`read_some` / `receive`（单次读）**：`eof` 那次调用恒返回 0 字节——服务端的 `if (ec == eof) break;` 因此不会丢数据。

```cpp
std::array<char, 1024> buf{};
std::size_t len = socket.read_some(asio::buffer(buf), ec);

if (ec == asio::error::eof) {
    std::cout << "对端正常关闭\n";                  // read_some 的 eof 恒为 0 字节
} else if (ec) {
    std::cout << "错误: " << ec.message() << "\n";  // 如 connection_reset
} else {
    std::cout << "收到 " << len << " 字节\n";
}
```

**全局 `read`**：目标是"读满"，部分读取后遇 `eof` 时**返回已读字节数并置 `ec`**——这批尾巴是真实收到的数据。正确顺序是先消费 `len`、再判 `ec`：

```cpp
std::size_t len = boost::asio::read(socket, asio::buffer(buf), ec);
if (len > 0) {
    // EOF 前收到的尾巴也是数据，先处理
}
if (ec == asio::error::eof) {
    // 对端正常关闭
} else if (ec) {
    // 其他错误，如 connection_reset
}
```

两种情形里"返回值 0"的含义也不同：`read_some` 的 0 表示"这次调用没读到数据"，`read` 的 0 表示"从头到尾一个字节都没收到"。

至于"我发完了"怎么告诉对端：用 `socket.shutdown(tcp::socket::shutdown_send)` 只关写端（半关闭），对端下一次 `read_some` 就会看到 `eof`——客户端示例正是这么收尾的。

​

### 按分隔符收一帧：read\_until 与 streambuf

`read` 只能按字节数读满，可真实协议常以分隔符划界（比如每条消息一行、以 `\n` 结尾）。这时用 `read_until`——它在内部循环 `read_some`，直到缓冲里出现分隔符：

```cpp
boost::asio::streambuf buf;
boost::asio::read_until(socket, buf, '\n');
// 此刻 buf 里至少有一个以 '\n' 结尾的完整帧

std::istream is(&buf);
std::string line;
std::getline(is, line);   // 取出这一帧，剩下的数据留在 buf 里
```

它属于上面"循环到满"那一类，只是凑齐的目标从字节数换成了分隔符。收下的数据放在 `asio::streambuf` 里，由它自己管理可扩容的内存；`streambuf` 还会保留没消费完的数据，下次 `read_until` 接着往后找。

分隔符只是最省事的一种边界。固定长度、或者"头里带长度"的二进制帧，得自己写解析状态机，比如 id-length-value 帧。

***

## 参考资源

- Boost.Asio 官方文档：[https://think-async.com/Asio/](https://think-async.com/Asio/)
- cppreference（同步 I/O 对象与 error\_code 语义）：[https://en.cppreference.com/](https://en.cppreference.com/)
- llfc 博客（C++ 后端教程）：[https://llfc.club/](https://llfc.club/)
