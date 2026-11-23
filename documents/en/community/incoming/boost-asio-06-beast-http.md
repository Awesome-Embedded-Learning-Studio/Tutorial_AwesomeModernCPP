---
title: "Learning Boost.Asio (6): Using Boost.Beast"
description: "Building an HTTP server on top of Asio with Boost.Beast: comparing callback-based and coroutine-based versions, explaining request/response, http::async_read / async_write, flat_buffer, and error handling"
chapter: 1
order: 6
tags:
  - Network Programming
  - Asynchronous Programming
  - coroutine
  - Practical Examples
  - host
  - cpp-modern
  - advanced
difficulty: advanced
platform: host
cpp_standard: [20]
reading_time_minutes: 25
community_category: learning-notes
contributors:
  - github: Karl-Heinrich-Marx-520
    name: Hanling You
    roles: [author]
translation:
  source: documents/community/incoming/boost-asio-06-beast-http.md
  source_hash: 596fa3e887d4708aa2c2111359c3ef24133224fc89e24db80b87a3b2928a785e
  translated_at: '2026-11-22T13:59:13+00:00'
  engine: qwen3:14b
  token_count: 24387
---
# Learning Boost.Asio (6): Using Boost.Beast

> **Author**: Hanling You (Karl-Heinrich-Marx-520) · **Original Declaration**: This article is an original learning note, with example code written by the author and compiled and run locally (MinGW g++ 13.1 + Boost 1.91, C++20) for verification
> **Main References**: [Boost.Beast Official Documentation](https://www.boost.org/doc/libs/release/libs/beast/), [Boost.Asio Official Documentation](https://think-async.com/Asio/) \
> **Target Readers**: Readers who have completed the 5th article and want to make the server speak HTTP instead of a custom protocol
> **License Statement**: Agree to allow maintainers to adjust the title, format, placement, and some wording; published with MIT license along with the repository

In the 5th article, we used coroutines to wrap the callback state machine into a single function, but that server still used our custom id-length-value protocol. What happens if we switch to HTTP?

HTTP messages have a request line, several header fields, and a body that may be chunked. Parsing them requires another state machine, and this time, no one will define the format for you. Boost.Beast is here to take on this task: it builds on Asio, encapsulating the reading, parsing, and serialization of HTTP messages. We operate on two objects: `request` and `response`, not on the byte stream.

In this article, we'll first build a working HTTP server using the callback-based approach familiar from articles 3 and 4, explaining several core types in Beast. Finally, we'll rewrite it using coroutines from the 5th article. The example uses ports 14518 (callback version) and 14519 (coroutine version), avoiding the first five articles.

## Complete Example: A Working HTTP Server

This server performs four tasks: GET `/count` returns the cumulative number of requests, GET `/time` returns the current timestamp, POST `/email` receives a JSON and returns a JSON, and all other paths return 404, and all other methods return 400. Additionally, each connection has a 60-second timeout timer as a fallback.

### Server

```cpp
// http_server.cpp
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/ostream.hpp>
#include <boost/beast/http.hpp>
#include <boost/core/ignore_unused.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace my_program_state {
    std::size_t request_count() {
        static std::size_t count = 0;
        return count++;
    }
    std::time_t now() {
        return std::time(nullptr);
    }
}

// http 连接类
class http_connection : public std::enable_shared_from_this<http_connection> {
public:
    explicit http_connection(tcp::socket socket) : socket_(std::move(socket)) {}

    void start() {
        read_request();
        check_deadline();
    }

private:
    tcp::socket socket_;                           // 这条连接独占的 socket
    beast::flat_buffer buffer_{8192};              // 读缓冲；8192 是容量上限，不是请求大小上限
    http::request<http::dynamic_body> request_;    // 收到的请求，body 存在 multi_buffer 里
    http::response<http::dynamic_body> response_;  // 要发回去的响应
    asio::steady_timer deadline_{socket_.get_executor(), std::chrono::seconds{60}};  // 超时定时器

    void read_request() {
        auto self = shared_from_this();           // 把自身生命周期绑到回调上
        // beast::error_code 就是 boost::system::error_code 的别名，Asio 那套错误码在这里通用
        http::async_read(socket_, buffer_, request_,
            [self](beast::error_code ec, std::size_t bytes_transferred) {
                boost::ignore_unused(bytes_transferred);   // 整条读完才回调，这个数只留给调试
                if (!ec) {                        // ec 非零说明对端断开或报文非法
                    self->process_request();
                }
            });
    }

    void process_request() {
        response_.version(request_.version());   // 用和请求相同的 HTTP 版本回
        response_.keep_alive(false);   // 短连接：一请求一响应一断开

        switch (request_.method()) {   // method() 返回 http::verb 枚举
        case http::verb::get:
            response_.result(http::status::ok);            // 状态码 200
            response_.set(http::field::server, "Beast");   // 响应头 Server: Beast
            create_response();
            break;
        case http::verb::post:
            response_.result(http::status::ok);
            response_.set(http::field::server, "Beast");
            create_post_response();
            break;
        default:
            response_.result(http::status::bad_request);   // 状态码 400
            response_.set(http::field::content_type, "text/plain");
            // method_string() 拿到原始方法字符串（"PUT"、"FOO" 这种），回显给客户端
            beast::ostream(response_.body()) << "Invalid request-method '"
                << std::string(request_.method_string()) << "'";
            break;
        }
        write_response();
    }

    void create_response() {
        if (request_.target() == "/count") {   // target() 是路径，带查询串
            response_.set(http::field::content_type, "text/html");
            beast::ostream(response_.body())   // 往 body 流式写，缓冲区自动扩容
                << "<html>\n"
                << "<head><title>Request count</title></head>\n"
                << "<body>\n"
                << "<h1>Request count</h1>\n"
                << "<p>There have been "
                << my_program_state::request_count()
                << " requests so far.</p>\n"
                << "</body>\n"
                << "</html>\n";
        } else if (request_.target() == "/time") {
            response_.set(http::field::content_type, "text/html");
            beast::ostream(response_.body())
                << "<html>\n"
                << "<head><title>Current time</title></head>\n"
                << "<body>\n"
                << "<h1>Current time</h1>\n"
                << "<p>The current time is "
                << my_program_state::now()
                << " seconds since the epoch.</p>\n"
                << "</body>\n"
                << "</html>\n";
        } else {
            response_.result(http::status::not_found);
            response_.set(http::field::content_type, "text/plain");
            beast::ostream(response_.body()) << "File not found\r\n";
        }
    }

    void create_post_response() {
        if (request_.target() == "/email") {
            auto& body = request_.body();                                  // dynamic_body 的 body
            const auto body_str = beast::buffers_to_string(body.data());   // 多段缓冲拼成字符串
            std::cout << "receive body is " << body_str << "\n";

            nlohmann::json reply;
            // allow_exceptions=false：解析失败不抛异常，而是返回一个 discarded 值
            auto src = nlohmann::json::parse(body_str, nullptr, /*allow_exceptions=*/false);
            if (src.is_discarded()) {                     // 上一步没解析成功
                reply["error"] = 1001;
                reply["msg"] = "invalid json";
            } else {
                const auto email = src.value("email", std::string{});   // 取 email，缺字段给空串
                std::cout << "email is " << email << "\n";
                reply["error"] = 0;
                reply["email"] = email;
                reply["msg"] = "receive email post success";
            }
            response_.set(http::field::content_type, "application/json");
            beast::ostream(response_.body()) << reply.dump();   // dump() 把 JSON 序列化成字符串
        } else {
            response_.result(http::status::not_found);
            response_.set(http::field::content_type, "text/plain");
            beast::ostream(response_.body()) << "File not found\r\n";
        }
    }

    void write_response() {
        auto self = shared_from_this();
        response_.content_length(response_.body().size());   // 手填 Content-Length，必须与 body 一致
        http::async_write(socket_, response_,   // 把整条响应序列化后写出去
            [self](beast::error_code ec, std::size_t) {
                // 半关闭：只关发送方向（发一个 FIN），保证数据发完，接收方向留给对端
                self->socket_.shutdown(tcp::socket::shutdown_send, ec);
                self->deadline_.cancel();   // 响应写完，取消超时定时器
            });
    }

    void check_deadline() {
        auto self = shared_from_this();
        deadline_.async_wait([self](beast::error_code ec) {   // 到点才回调
            if (!ec) {                       // 60 秒到点（被 cancel 则 ec 为 aborted）
                self->socket_.close(ec);     // 超时了
            }
        });
    }
};

void http_server(tcp::acceptor& acceptor, asio::io_context& ioc) {
    acceptor.async_accept(
        // 这个重载把新连接的 socket 直接当参数递给你，不用自己建
        [&](const boost::system::error_code& ec, tcp::socket socket) {
            if (!ec) {
                // 交给 shared_ptr 管生命周期，最后一个回调结束时自动析构
                std::make_shared<http_connection>(std::move(socket))->start();
            }
            http_server(acceptor, ioc);  // 继续接受下一个连接
        });
}

int main() {
    try {
        asio::io_context ioc;
        // tcp::v4() 是 IPv4；地址不给就绑本机所有网卡
        tcp::acceptor acceptor(ioc, tcp::endpoint(tcp::v4(), 14518));
        http_server(acceptor, ioc);
        ioc.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
```

## What It Looks Like When Running

After starting, use `curl` to access four routes. `-i` is used to see the response headers as well:

```bash
curl -i http://127.0.0.1:14518/count
curl -i http://127.0.0.1:14518/time
curl -i http://127.0.0.1:14518/nope
curl -i -X POST -d '{"email":"test@qq.com"}' http://127.0.0.1:14518/email
curl -i -X PUT http://127.0.0.1:14518/count
```

The response headers for GET `/count` look like this, with `Connection: close` indicating a short-lived connection:

```text
HTTP/1.1 200 OK
Connection: close
Server: Beast
Content-Type: text/html
Content-Length: 137
```

Unknown paths return 404, and non-GET/POST methods return 400:

```text
HTTP/1.1 404 Not Found
Connection: close
Server: Beast
Content-Type: text/plain
Content-Length: 16

File not found
```

POST `/email` with a valid JSON body, the server responds like this:

```text
{"email":"test@qq.com","error":0,"msg":"receive email post success"}
```

Submitting a body that is not JSON results in an error code instead of a crash — here we use the `nlohmann::json::parse` switch `allow_exceptions=false`, which returns `is_discarded()` instead of an exception:

```text
{"error":1001,"msg":"invalid json"}
```

Server-side output:

```text
server:
receive body is {"email":"test@qq.com"}
email is test@qq.com
```

## Several Core Types in Beast

First, recognize a few familiar ones, and we'll break them down one by one:

| **Type**                 | **What It Is**                       | **How It's Used in This Article**               |
| ---------------------- | ----------------------------- | ----------------------- |
| `beast::flat_buffer`   | A read buffer used by the parser, with contiguous memory | One per connection, placed inside the connection class           |
| `http::request<Body>`  | An HTTP request object, containing method, path, version, headers, and body | Read directly into its fields              |
| `http::response<Body>` | An HTTP response object, symmetric in structure             | Fill in the fields and pass to `async_write`    |
| `http::verb`           | Method enumeration, with 34 values                  | `switch` dispatch             |
| `http::field`          | Header field enumeration, automatically handles standard casing               | `response.set(...)`     |
| `http::async_read`     | Reads and parses an entire message in one go               | Higher level than `async_read_some` |
| `http::async_write`    | Serializes the message object and writes it out                  | Call `shutdown` after writing if needed         |

In one sentence: **Asio handles how bytes are sent and received, Beast handles what these bytes mean.**

## API Summary

| **API**                                | **One-Sentence Purpose**                                        |
| -------------------------------------- | ------------------------------------------------ |
| `beast::flat_buffer`                   | A contiguous memory read buffer for the parser, with a capacity limit of `8192` |
| `http::request<Body>`                  | A request object, containing method, path, version, headers, and body |
| `http::response<Body>`                 | A response object, symmetric in structure, passed to `async_write` after filling in |
| `http::verb`                           | Method enumeration, with 34 values, `delete_` has an underscore |
| `http::status`                         | Status code enumeration, `ok`=200, `bad_request`=400, `not_found`=404 |
| `http::field`                          | Header field enumeration, automatically generates standard casing |
| `beast::error_code`                    | Alias for `boost::system::error_code` |
| `http::async_read`                     | Reads and parses an entire request, calling back only after completion |
| `http::async_write`                    | Serializes the entire response and writes it out |
| `beast::ostream(body)`                 | Streams to the body, automatically expanding the buffer |
| `beast::buffers_to_string(buf.data())` | Concatenates multiple buffers into a `std::string` |
| `req.method()` / `req.method_string()` | Method (enum) / Method (raw string) |
| `req.target()`                         | Request path, including query string |
| `req.body()`                           | Request body |
| `res.result(status)`                   | Sets the status code |
| `res.set(field, value)`                | Sets a header field, overwriting existing ones; `insert` appends |
| `res.content_length(n)`                | Manually sets `Content-Length` |
| `res.prepare_payload()`                | Automatically sets `Content-Length` based on body length |
| `res.keep_alive(bool)`                 | Whether to reuse this connection |
| `socket.shutdown(shutdown_send, ec)`   | Half-close, only closing the send direction |

***

## Breakdown

### flat_buffer: Read buffer used by the parser

```cpp
#include <boost/beast/core/flat_buffer.hpp>

beast::flat_buffer buffer{8192};
```

The easiest thing to get wrong is that constructor parameter. It is the buffer capacity limit, not the maximum request size. In the source code, `prepare(n)` only throws `std::length_error` (lines 323-325 in `impl/flat_buffer.hpp`) when `n > max_ - size()`. Beast reads HTTP messages by parsing and consuming as it goes — after parsing a line, it calls `consume` to remove it, and the unprocessed bytes in the buffer are far less than the entire request.

Practically tested this counterintuitive point: changing the limit to 64 still allows a complete 10013-byte POST body to be received, returning 200. **So don't treat it as a "gate" limiting request size**. If you really want to limit the request size, you should check `req.body().size()` after reading the message, or use `http::request_parser::body_limit()`.

Two other commonly used member functions:

```cpp
std::size_t size() const noexcept;              // 已存放的字节数
void consume(std::size_t n);                     // 从头部丢弃 n 字节
```

You usually don't need to call `consume` yourself — `async_read` internally consumes the parsed parts. However, in long connection scenarios, `flat_buffer` **must be declared outside the loop**, otherwise the residual data from the next request will be lost, which you can see in the coroutine version.

### request and response: The message is just two objects

After obtaining `request`, the various parts of the HTTP message are just ordinary fields, with no parsing action:

```cpp
http::verb method() const noexcept;          // 方法
string_view target() const noexcept;         // 路径（含查询串）
unsigned version() const noexcept;           // 版本，11 表示 HTTP/1.1
Body::value_type& body();                    // body
```

`http::verb` is an enumeration, with common values being `get`, `post`, `put`, `delete_`, `head`, `options`, `patch`. Note that **`delete_` has an underscore at the end** because `delete` is a C++ keyword; the entire enumeration has 34 values, including `propfind`, `mkcol` used by WebDAV.

The response side follows the same pattern, with three steps: setting the status code, setting the header fields, and writing the body:

```cpp
http::response<http::string_body> res;
res.result(http::status::ok);
res.set(http::field::server, "Beast");
res.set(http::field::content_type, "text/html");
res.body() = "<html><body>Hello</body></html>";
```

The difference between `set` and `insert` in one sentence: **`set` overwrites existing fields with the same name, `insert` appends fields with the same name**. You don't need to worry about the casing of header fields — `http::field` enumeration generates the standard format; custom headers can be directly passed as strings, for example, `res.set("X-My-Header", "value")`.

### body: Several storage methods for the same message

The storage method of the message body is determined by the template parameter:

| **Body Type**          | **Storage Method**                     | **Applicable Scenarios**              |
| -------------------- | ---------------------------- | --------------------- |
| `http::string_body`  | `std::string`                | Text, JSON, small data          |
| `http::vector_body`  | `std::vector`                | Binary data (images, small files)         |
| `http::file_body`    | File stream                          | Large file transfer, can be zero-copy           |
| `http::dynamic_body` | `boost::beast::multi_buffer` | Streaming, unknown length in advance          |
| `http::empty_body`   | No body                       | HEAD response, 204/304 with no content |

When using `dynamic_body`, the underlying storage is `multi_buffer`, consisting of several memory segments. To retrieve the content, you need to concatenate them into a `std::string` using `beast::buffers_to_string`:

```cpp
const auto body_str = beast::buffers_to_string(request_.body().data());
```

When writing content to the body, `beast::ostream` is more convenient than direct assignment, especially for `dynamic_body` which automatically expands:

```cpp
beast::ostream(response_.body()) << "Hello, world\n";
```

In this article's callback version, `dynamic_body` is used, and in the coroutine version, `string_body` is used, to walk through both. Use `dynamic_body` when you don't know the size in advance, and `string_body` when you know it's text, as the latter allows `res.body() = ...`, shortening the code.

### async_read: Read the entire message in one go

This is one of the most convenient APIs in Beast. It internally loops calling Asio's `async_read_some` until the **complete HTTP message is parsed** and then calls back:

```cpp
http::async_read(socket_, buffer_, request_,
    [self](beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (!ec) {
            self->process_request();
        }
    });
```

Because it calls back only after the entire message is read, the `request_` in the callback is guaranteed to be complete, and you don't need to determine yourself whether this read is sufficient. The cost is that `bytes_transferred` is rarely used — it's just the number of bytes received this time, and can be explicitly "used" with `boost::ignore_unused` if not needed, to avoid compiler warnings.

Three rules must be followed:

`buffer` and `request_` must be alive until the callback ends — they are members of the connection class, kept alive by `shared_from_this()`, which is the lifetime contract discussed in article 3. Don't initiate multiple `async_read` operations on the same socket in parallel — parsing is sequential, and parallel operations will be out of order. After a successful read, the `buffer` may still have residual data from the next request, and in long connections, you must not treat it as "discard after use".

### async_write and Content-Length

Before writing, the message must be self-consistent: **`Content-Length` must match the actual length of the body**, otherwise the client will wait indefinitely or truncate prematurely. Two approaches:

```cpp
response_.content_length(response_.body().size());   // 手动设置
res.prepare_payload();                                // 自动算
```

`prepare_payload()` is more convenient, as it automatically fills in `Content-Length` based on the body length, and does not overwrite values you have manually set — so set the body first, then call it, and don't reverse the order. For cases where the length is unknown in advance, it will switch to `Transfer-Encoding: chunked`, and these two cannot appear at the same time.

### Timeout: Let the timer race with the read operation

An HTTP server must prevent clients from hanging indefinitely by not sending anything. The approach used in article 3 is to start a 60-second timer as soon as the connection is established, and cancel it if the read operation completes.

```cpp
deadline_.async_wait([self](beast::error_code ec) {
    if (!ec) {                       // 60 秒到点（被 cancel 则 ec 为 aborted）
        self->socket_.close(ec);     // 超时了
    }
});
```

The key is why the condition is `if (!ec)`: **`async_wait` canceled by `cancel()` ends with `operation_aborted` (non-zero `ec`), only when it actually reaches the time limit is `ec == 0`**. So "close the connection only when the time is up" is written as `if (!ec)`, and writing it the other way around would result in "close the connection after the response is completed".

Correspondingly, call `deadline_.cancel()` after writing the response, so that the race is won by the read operation:

```cpp
self->socket_.shutdown(tcp::socket::shutdown_send, ec);
self->deadline_.cancel();   // 响应写完，取消超时定时器
```

### Short Connections and Long Connections: Serving Multiple Requests on a Single Connection

The callback version in this article is a short connection: `response_.keep_alive(false)` plus `shutdown` after writing, so one connection serves only one request before disconnecting.

Long connections are a different matter. HTTP/1.1 defaults to `keep_alive`, and multiple requests will come in on the same socket. The handling method is a serial loop: read a request, process it, write the response back, and then read the next one. On a single connection, there is always only one unfinished read or write.

Can multiple requests on the same connection be processed in parallel? Yes, but responses must be queued: send the request to a thread pool, continue `async_read` on the connection, and after processing, push the response into a queue, which serially `async_write`s it out.

***

## Rewriting with Coroutines: From Four Member Functions to One Function

As mentioned in article 5, the value of coroutines is most evident in long processes. An HTTP session is exactly that kind of process: read — process — write, and for long connections, it repeats. Translating the above `http_connection` class (four member functions, one `shared_from_this()` to keep the object alive, one timer member) into a coroutine looks like this:

```cpp
// http_coro_server.cpp
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <iostream>
#include <string>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using namespace std::chrono_literals;

namespace my_program_state {
    std::size_t request_count() {
        static std::size_t count = 0;
        return count++;
    }
    std::time_t now() {
        return std::time(nullptr);
    }
}

// 路由：业务逻辑是同步的，和 Asio 没关系
void build_response(const http::request<http::string_body>& req,
                    http::response<http::string_body>& res) {
    res.version(req.version());
    res.keep_alive(req.keep_alive());   // 客户端想要长连接就给长连接
    res.set(http::field::server, "Beast");

    if (req.method() == http::verb::get && req.target() == "/count") {
        res.result(http::status::ok);
        res.set(http::field::content_type, "text/html");
        res.body() = "<html>\n"
                     "<head><title>Request count</title></head>\n"
                     "<body>\n"
                     "<h1>Request count</h1>\n"
                     "<p>There have been "
                   + std::to_string(my_program_state::request_count())
                   + " requests so far.</p>\n"
                     "</body>\n"
                     "</html>\n";
    } else if (req.method() == http::verb::get && req.target() == "/time") {
        res.result(http::status::ok);
        res.set(http::field::content_type, "text/html");
        res.body() = "<html>\n"
                     "<head><title>Current time</title></head>\n"
                     "<body>\n"
                     "<h1>Current time</h1>\n"
                     "<p>The current time is "
                   + std::to_string(my_program_state::now())
                   + " seconds since the epoch.</p>\n"
                     "</body>\n"
                     "</html>\n";
    } else if (req.method() == http::verb::post && req.target() == "/email") {
        const auto& body_str = req.body();
        std::cout << "receive body is " << body_str << "\n";

        nlohmann::json reply;
        auto src = nlohmann::json::parse(body_str, nullptr,
                                         /*allow_exceptions=*/false);
        if (src.is_discarded()) {
            reply["error"] = 1001;
            reply["msg"] = "invalid json";
        } else {
            const auto email = src.value("email", std::string{});   // 取 email，缺字段给空串
            std::cout << "email is " << email << "\n";
            reply["error"] = 0;
            reply["email"] = email;
            reply["msg"] = "receive email post success";
        }
        res.result(http::status::ok);
        res.set(http::field::content_type, "application/json");
        res.body() = reply.dump();
    } else if (req.method() == http::verb::get || req.method() == http::verb::post) {
        res.result(http::status::not_found);
        res.set(http::field::content_type, "text/plain");
        res.body() = "File not found\r\n";
    } else {
        res.result(http::status::bad_request);
        res.set(http::field::content_type, "text/plain");
        res.body() = "Invalid request-method '" + std::string(req.method_string()) + "'";
    }
    res.prepare_payload();   // 按 body 长度自动填 Content-Length
}

// 一个会话：读—处理—写，长连接就再来一轮
asio::awaitable<void> session(tcp::socket socket) {
    beast::flat_buffer buffer{8192};   // 循环外声明：长连接时残留数据要留在里面
    try {
        for (;;) {                     // 长连接：一轮一个请求，直到对端说不
            http::request<http::string_body> req;   // string_body：body 就是一个 std::string
            co_await http::async_read(
                socket, buffer, req,
                asio::cancel_after(60s, asio::use_awaitable));   // 超时即取消这次读

            http::response<http::string_body> res;
            build_response(req, res);
            co_await http::async_write(socket, res, asio::use_awaitable);

            if (!res.keep_alive()) {
                break;
            }
        }
    } catch (const boost::system::system_error& e) {
        // 超时是 operation_aborted，对方断开是 eof，都在这里收尾
        if (e.code() == asio::error::operation_aborted) {
            std::cout << "会话超时\n";
        }
    }
    boost::system::error_code ignored;   // 收尾失败不用管，这里只是把发送方向关掉
    socket.shutdown(tcp::socket::shutdown_send, ignored);
}

asio::awaitable<void> listener(unsigned short port) {
    auto executor = co_await asio::this_coro::executor;
    tcp::acceptor acceptor(executor, tcp::endpoint(tcp::v4(), port));
    std::cout << "coroutine http server on " << port << "\n";
    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable);  // 等新连接
        asio::co_spawn(executor, session(std::move(socket)), asio::detached);      // 一条连接一个协程
    }
}

int main() {
    try {
        asio::io_context io;
        asio::co_spawn(io, listener(14519), asio::detached);
        io.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
```

The four member functions are merged into one `for` loop, matching item by item:

| **Item** | **Callback Version**                                   | **Coroutine Version**                       |
| ----- | ----------------------------------------- | ----------------------------- |
| Session State  | Four member functions of the class + three member objects                         | One function, all local variables                   |
| Object Lifetime  | `shared_from_this()` captured into each callback              | Not needed, socket lives in the coroutine frame             |
| Timeout    | `steady_timer` member + `check_deadline()` race | `cancel_after(60s, ...)` with one parameter |
| Long Connection   | Requires recursive callbacks, or another function                           | `for (;;)` naturally forms a loop             |
| Error Path  | Each callback individually `if (!ec)`                         | One `try/catch` covers all           |

**Three details to mention separately:**

- **Timeout**: `cancel_after(60s, ...)` is directly attached to `async_read`, and if the read doesn't complete within 60 seconds, it cancels this read. After cancellation, it throws `operation_aborted`, so the `catch` block checks `e.code() == asio::error::operation_aborted`.
- **Long Connection**: `res.keep_alive(req.keep_alive())` determines whether the loop continues.
- **Buffer Position**: `flat_buffer` must be placed outside the `for(;;)` loop. In long connections, it may have already read part of the next request; placing it inside the loop and rebuilding it each round would cause this data to be lost.

Running it is completely consistent with the callback version, the response body is identical, and the connection behavior is different as needed:

```text
{"email":"test@qq.com","error":0,"msg":"receive email post success"}
```

At this point, the entire series comes to an end. Going through the six articles follows the same thread: understanding the basic shape of Asio with synchronous C/S, clarifying the difference between "reading fully" and "reading a bit" with synchronous read/write, introducing callbacks and object lifetime with asynchronous read/write, building a server that is engineering-ready in article 4, wrapping it into a single function with coroutines in article 5, and in this article, switching to HTTP to make the server speak a real protocol. **The complexity of the toolchain is increasing, but each layer solves the problems left by the previous one**, which is also the arrangement logic of this series.

***

## Reference Resources

- Boost.Beast Official Documentation: [https://www.boost.org/doc/libs/release/libs/beast/](https://www.boost.org/doc/libs/release/libs/beast/)
- Boost.Asio Official Documentation: [https://think-async.com/Asio/](https://think-async.com/Asio/)
- cppreference (C++20 Coroutines): [https://en.cppreference.com/w/cpp/language/coroutines](https://en.cppreference.com/w/cpp/language/coroutines)
- llfc Blog (C++ Backend Tutorials): [https://llfc.club/](https://llfc.club/)
