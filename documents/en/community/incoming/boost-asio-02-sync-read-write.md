---
title: "Boost.Asio Learning (2): Synchronous Read and Write"
description: "Explains the differences between the six synchronous send and receive APIs: send / write_some / write and receive / read_some / read, as well as asio::buffer, EOF detection, and frame reception based on delimiters."
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
    name: You Hanling
    roles: [author]
translation:
  source: documents/community/incoming/boost-asio-02-sync-read-write.md
  source_hash: e8f113998b1c257ad78abd3d647152d937098e2e6f956093079a538a5cb574ca
  translated_at: '2026-11-22T13:18:32+00:00'
  engine: qwen3:14b
  token_count: 9506
---
# Boost.Asio Learning (2): Synchronous Read and Write

> **Original Declaration**: This article is an original learning note summary. All example code was written by the author and compiled and run locally (MinGW g++ 13.1 + Boost 1.91, C++20) for verification.
> **Main References**: [Boost.Asio Official Documentation](https://think-async.com/Asio/), [cppreference](https://en.cppreference.com/), [llfc blog](https://llfc.club/)\*\*\*\*
>
> **Target Readers**: Learners with basic socket knowledge who want to get started with Boost.Asio in modern C++; it is recommended to read Part 1 first.
> **License Statement**: The author agrees to allow maintainers to adjust the title, format, placement, and some wording; it is released with the MIT license along with the repository.

In Part 1, the client sent one message and the server echoed one message, and that message was read in one go purely because it was small enough. In this article, we will cover all the synchronous send and receive APIs: the sending functions `send` / `write_some` / `write`, and the receiving functions `receive` / `read_some` / `read`, as well as the shared dependency `asio::buffer`. After reading this article, you will understand what each of the six functions does, how to interpret their return values, how to detect the remote end closing, and how to receive the next complete message based on a delimiter. This article simplifies error handling and focuses on the core logic.

## Complete Example: Comparing `read` and `read_some`

The server uses a small 256-byte buffer to loop and `read_some` for echoing (reads as much as possible), while the client sends 1000 bytes and uses `read` to read all 1000 bytes in one go (looping until full), and finally triggers the server's `eof` by half-closing.

### Server

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

### Client

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

## What It Looks Like When Running

Start `sync_server.exe` first, then run `sync_client.exe`:

```text
sync_server:
Echo server listening on port 8081...
Client connected from 127.0.0.1:xxxxx
Peer closed connection (eof). Echoed 1000 bytes in total.

sync_client:
Sent 1000 bytes
Received 1000 bytes, content matches
```

The server's "Echoed 1000 bytes" proves that the 1000 bytes were read in chunks by the 256-byte buffer; the client's `matches` proves that `read` reassembled the complete message exactly as it was.

## Overview of the Six Functions: One Call vs Loop Until Full

These six functions are not six separate entities, but two categories — **"one-time call, give you what you get"**, and **"loop until full" global functions**:

| **Function**                  | **Type** | **Category**        | **Typical Scenario**  |
| ----------------------- | ------ | ------------- | --------- |
| `write_some` / `send`   | Member Function   | One-time write, not guaranteed to finish     | You control the writing pace   |
| `read_some` / `receive` | Member Function   | One-time read, not guaranteed to be full     | Custom boundary logic |
| `write`                 | Global Function   | Loop to write the entire buffer | Default choice      |
| `read`                  | Global Function   | Loop to read the entire buffer | Known-length complete message |

A rule of thumb: **Use the global `write` / `read` if you need to "gather N bytes"; use the member version `*_some` if you want to control the pace yourself.** This division is also the map for understanding asynchronous APIs — the `async_write_some` / `async_write` in Part 3 are exactly the same pair.

***

## Breakdown

Start with the most basic step: what to pass in.

### asio::buffer

The send and receive functions do not directly accept `std::string` or arrays, but instead accept a buffer object created by `asio::buffer(...)`. It only describes "where the memory starts and how long it is," and does not own the memory:

```cpp
char raw[64];                              // 可写内存
auto wbuf = asio::buffer(raw);             // mutable_buffer：用于收（数据写进这里）

const char* data = "hello";
auto cbuf = asio::buffer(data, 5);         // const_buffer：用于发（只读）
```

For receiving (read / receive), you need to pass a mutable buffer, and for sending (write / send), you need to pass a const buffer. However, `asio::buffer(...)` automatically selects based on the constness of the argument, so you don't need to distinguish manually.

When you don't manually specify the length, `buffer` infers it from the argument type:

| **Argument**                            | **Inferred Length** | **Description**                     |
| --------------------------------- | ---------- | -------------------------- |
| `char[N]` / `std::array<char, N>` | N (entire array)    | Note that it's the full array length, not the string length            |
| `std::vector<char>`               | `size()`   |                          |
| `std::string`                     | `size()`   |                          |
| Raw pointer `char*`                       | Cannot infer       | Must manually specify the length: `buffer(ptr, len)` |

Passing a raw `char*` pointer is a classic pitfall:

```cpp
char data[100] = "hello";
socket.send(asio::buffer(data));    // 发送 100 字节！"hello" 后面的 95 个 '\0' 会被原样发出去
socket.send(asio::buffer(data, 5)); // 只发 "hello" —— 需要精确长度时手动指定
```

The last 95 bytes of `data` are initialized with `'\0'` (not garbage), but under the first usage, they are still sent because the buffer from the array automatically passes the full array length. Therefore, to send only the valid content, you must manually specify the length.

### One-time call: "As much as possible"

All four member functions only perform one system call, returning as many bytes as possible:

```cpp
// 主角是这两个：write_some 尽力写（可能只写一部分），read_some 有多少读多少
std::size_t n = socket.write_some(asio::buffer(msg), ec);
std::size_t n = socket.read_some(asio::buffer(buf), ec);

// send / receive 与它们一一对应，只是多一个底层标志位，实战里基本用不上
std::size_t n = socket.send(asio::buffer(msg), 0, ec);
std::size_t n = socket.receive(asio::buffer(buf), 0, ec);
```

The "some" in `read_some` literally means: **the kernel receives the buffer and moves whatever is there at the moment, which could be just one byte**. In Part 1, the echo read the complete message in one go purely because the message was small enough.

Similarly on the sending side: "partial writes" are rare in synchronous blocking mode (only occur when the send buffer is full), but the return value of `write_some` / `send` is the number of bytes actually written this time. Returning less than the requested length is valid. To handle this, either loop and re-send manually or use `write` directly.

### Loop until full: `write` and `read`

The two global functions internally loop calling `*_some`, and will fill the buffer for you:

```cpp
// write：内部循环 write_some，直到整个 buffer 写完
std::size_t n = boost::asio::write(socket, asio::buffer(msg), ec);

// read：内部循环 read_some，直到读满整个 buffer
std::size_t n = boost::asio::read(socket, asio::buffer(buf), ec);
```

The goal of "reading to full" is determined by the length of the buffer you provide: if you give a 1000-byte array, it will read until 1000 bytes are read.

### How to judge the result: return value and error_code

All six functions provide an exception version and an `error_code` version, but the return value rules are divided into two sets — exactly corresponding to the above "one-time call" and "loop until full" division:

| **Situation**    | **Exception Version**                         | **error_code Version**                    |
| --------- | ------------------------------- | ------------------------------------ |
| Success        | Returns the number of bytes transferred                        | Same as above                                   |
| Error (one-time call) | Throws `boost::system::system_error` | Returns 0, error in `ec`                      |
| Error (loop until full) | Throws `boost::system::system_error` | Returns the number of bytes transferred before the error (could be partial, needs handling), error in `ec` |

### How to know when it's over: `eof` and half-close

When the remote end **closes normally** (sends a FIN), the read operation ends with `boost::asio::error::eof`; when the connection is **abnormally reset** (RST), it is `connection_reset`. Always check `ec` to determine the end, not the return value being 0. The behavior of `eof` differs between the two families.

**`read_some` / `receive` (single read)**: `eof` always returns 0 bytes — the server's `if (ec == eof) break;` thus does not lose data.

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

**Global `read`**: The goal is to "read to full", and if `eof` is encountered after partial reading, it **returns the number of bytes read and sets `ec`** — this tail is real data received. The correct order is to consume `len` first, then check `ec`:

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

The meaning of "return value 0" differs in the two scenarios: `read_some`'s 0 means "no data was read in this call", while `read`'s 0 means "no bytes were received from start to finish".

As for how to tell the remote end that "I have finished sending": use `socket.shutdown(tcp::socket::shutdown_send)` to close only the write end (half-close), and the remote end will see `eof` on the next `read_some` — the client example ends exactly like this.

### Receiving a frame based on a delimiter: `read_until` and `streambuf`

`read` can only read to a fixed byte count, but real protocols often use delimiters to define boundaries (e.g., each message is a line ending with `\n`). In such cases, use `read_until` — it loops `read_some` internally until the delimiter appears in the buffer:

```cpp
boost::asio::streambuf buf;
boost::asio::read_until(socket, buf, '\n');
// 此刻 buf 里至少有一个以 '\n' 结尾的完整帧

std::istream is(&buf);
std::string line;
std::getline(is, line);   // 取出这一帧，剩下的数据留在 buf 里
```

It belongs to the "loop until full" category, but the goal of "filling up" is now the delimiter instead of the byte count. The received data is stored in `asio::streambuf`, which manages the expandable memory itself; `streambuf` also retains the unprocessed data, and the next `read_until` continues searching from there.

Delimiters are just the simplest type of boundary. For fixed-length or binary frames with length in the header, you need to write your own parsing state machine, such as the id-length-value frame.

***

## Reference Resources

- Boost.Asio Official Documentation: [https://think-async.com/Asio/](https://think-async.com/Asio/)
- cppreference (synchronous I/O objects and error_code semantics): [https://en.cppreference.com/](https://en.cppreference.com/)
- llfc blog (C++ backend tutorial): [https://llfc.club/](https://llfc.club/)
