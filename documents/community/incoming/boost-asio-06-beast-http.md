---
title: "Boost.Asio 学习(6)：用 Boost.Beast"
description: "站在 Asio 之上用 Boost.Beast 搭 HTTP 服务器：回调版与协程版对照，讲清 request / response、http::async_read / async_write、flat_buffer 与错误处理"
chapter: 1
order: 6
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
reading_time_minutes: 25
community_category: learning-notes
---

# Boost.Asio 学习(6)：用 Boost.Beast

> **作者**：游翰灵（Karl-Heinrich-Marx-520） · **原创声明**：本文为本人原创学习笔记整理，示例代码均为本人编写并在本地（MinGW g++ 13.1 + Boost 1.91，C++20）编译运行验证
> **主要参考**：[Boost.Beast 官方文档](https://www.boost.org/doc/libs/release/libs/beast/)、[Boost.Asio 官方文档](https://think-async.com/Asio/)**目标读者**：读完第 5 篇、想让服务器说 HTTP 而不只是自定义协议的现代 C++ 学习者
> **授权说明**：同意维护者调整标题、格式、放置位置与部分措辞；随仓库以 MIT 许可发布

第 5 篇用协程把回调状态机收回了一个函数，但那台服务器说的还是我们自己定的 id-length-value 协议。换成 HTTP 会怎样？

报文有请求行、若干头字段、可能是分块编码的 body，自己解析又是一台状态机，而且这次没人帮你定格式。Boost.Beast 就是来接这活的：它站在 Asio 之上，把 HTTP 报文的读取、解析、序列化全包了，我们操作的是 `request` 和 `response` 两个对象，不是字节流。

本篇先用第 3、4 篇熟悉的回调写法搭一台能跑的 HTTP 服务器，讲清 Beast 的几个核心类型，最后用第 5 篇的协程把它整个改写一遍。示例端口用 14518（回调版）和 14519（协程版），与前五篇错开。

## 完整示例：一个能跑的 HTTP 服务器

这台服务器做四件事：GET `/count` 返回累计请求数，GET `/time` 返回当前时间戳，POST `/email` 收一个 JSON 并回一个 JSON，其余路径 404、其余方法 400。另外每条连接挂一个 60 秒的超时定时器兜底。

### 服务器

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

## 跑起来是什么样

启动后用 `curl` 打四条路由。`-i` 是为了连响应头一起看：

```bash
curl -i http://127.0.0.1:14518/count
curl -i http://127.0.0.1:14518/time
curl -i http://127.0.0.1:14518/nope
curl -i -X POST -d '{"email":"test@qq.com"}' http://127.0.0.1:14518/email
curl -i -X PUT http://127.0.0.1:14518/count
```

GET `/count` 的响应头是这样的，`Connection: close` 说明这是一条短连接：

```text
HTTP/1.1 200 OK
Connection: close
Server: Beast
Content-Type: text/html
Content-Length: 137
```

未知路径返回 404，非 GET/POST 的方法返回 400：

```text
HTTP/1.1 404 Not Found
Connection: close
Server: Beast
Content-Type: text/plain
Content-Length: 16

File not found
```

POST `/email` 提交合法 JSON，服务器回包是这样的：

```text
{"email":"test@qq.com","error":0,"msg":"receive email post success"}
```

提交一个不是 JSON 的 body，回包变成错误码，而不是崩溃——这里用了 `nlohmann::json::parse` 的 `allow_exceptions=false` 开关，解析失败得到 `is_discarded()` 而不是异常：

```text
{"error":1001,"msg":"invalid json"}
```

服务器侧的打印：

```text
server:
receive body is {"email":"test@qq.com"}
email is test@qq.com
```

​

## Beast 的几个核心类型

先认几个面熟的，后面逐个拆：

| **类型**                 | **是什么**                       | **本篇怎么用**               |
| ---------------------- | ----------------------------- | ----------------------- |
| `beast::flat_buffer`   | 给解析器用的读缓冲，内存连续                | 每条连接一个，放在连接类里           |
| `http::request<Body>`  | 一个 HTTP 请求对象，含方法、路径、版本、头、body | 读进来直接读它的字段              |
| `http::response<Body>` | 一个 HTTP 响应对象，结构对称             | 填完字段交给 `async_write`    |
| `http::verb`           | 方法枚举，共 32 个值                  | `switch` 分发             |
| `http::field`          | 头字段枚举，自动处理标准大小写               | `response.set(...)`     |
| `http::async_read`     | 一次读完**整条**报文并解析               | 比 `async_read_some` 高一层 |
| `http::async_write`    | 把报文对象序列化后写出去                  | 写完按需 `shutdown`         |

一句话概括分工：**Asio 管字节怎么收发，Beast 管这些字节是什么意思。**

​

## API 汇总

| **API**                                | **一句话作用**                                        |
| -------------------------------------- | ------------------------------------------------ |
| `beast::flat_buffer`                   | 给解析器用的连续内存读缓冲，`8192` 是容量上限                       |
| `http::request<Body>`                  | 请求对象，方法、路径、版本、头、body 都在里面                        |
| `http::response<Body>`                 | 响应对象，结构对称，填完交给 `async_write`                     |
| `http::verb`                           | 方法枚举，共 32 个值，`delete_` 带下划线                      |
| `http::status`                         | 状态码枚举，`ok`=200、`bad_request`=400、`not_found`=404 |
| `http::field`                          | 头字段枚举，自动生成标准大小写                                  |
| `beast::error_code`                    | 就是 `boost::system::error_code` 的别名               |
| `http::async_read`                     | 读完并解析**整条**请求，完成才回调                              |
| `http::async_write`                    | 把整条响应序列化后写出去                                     |
| `beast::ostream(body)`                 | 往 body 里流式写，缓冲区自动扩容                              |
| `beast::buffers_to_string(buf.data())` | 把多段缓冲拼成一个 `std::string`                          |
| `req.method()` / `req.method_string()` | 方法（枚举） / 方法（原始字符串）                               |
| `req.target()`                         | 请求路径，含查询串                                        |
| `req.body()`                           | 请求体                                              |
| `res.result(status)`                   | 设状态码                                             |
| `res.set(field, value)`                | 设头字段，同名会**覆盖**；`insert` 则是追加                     |
| `res.content_length(n)`                | 手动设 `Content-Length`                             |
| `res.prepare_payload()`                | 按 body 长度自动填 `Content-Length`                    |
| `res.keep_alive(bool)`                 | 这条连接要不要复用                                        |
| `socket.shutdown(shutdown_send, ec)`   | 半关闭，只关发送方向                                       |

***

## 逐个拆解

### flat\_buffer：给解析器用的读缓冲区

```cpp
#include <boost/beast/core/flat_buffer.hpp>

beast::flat_buffer buffer{8192};
```

最容易记错的是那个构造参数。它是缓冲区容量上限，不是请求的最大值。源码里 `prepare(n)` 只有在 `n > max_ - size()` 时才抛 `std::length_error`（`impl/flat_buffer.hpp:323-325`），而 Beast 读 HTTP 时是边解析边消费——解析完一行就 `consume` 掉，缓冲区里的未解析字节远小于整个请求。

实测过这个反直觉的点：把上限改成 64，照样完整收下一个 10013 字节的 POST body，返回 200。**所以不要把它当成"限制请求大小的闸门"**，真要限制请求大小，应该在读完报文后自己检查 `req.body().size()`，或者用 `http::request_parser::body_limit()`。

另外两个常用成员函数：

```cpp
std::size_t size() const noexcept;              // 已存放的字节数
void consume(std::size_t n);                     // 从头部丢弃 n 字节
```

`consume` 你通常不用自己调——`async_read` 内部会消费掉已解析的部分。但长连接场景下 `flat_buffer` **必须在循环外声明**，否则残留的下一个请求数据会被丢掉，这点在协程版里能看到。

​

### request 与 response：报文就是两个对象

拿到 `request` 之后，HTTP 报文的各个部分都是普通字段，没有解析动作：

```cpp
http::verb method() const noexcept;          // 方法
string_view target() const noexcept;         // 路径（含查询串）
unsigned version() const noexcept;           // 版本，11 表示 HTTP/1.1
Body::value_type& body();                    // body
```

`http::verb` 是枚举，常用的有 `get`、`post`、`put`、`delete_`、`head`、`options`、`patch`。注意 **`delete_` 带尾下划线**，因为 `delete` 是 C++ 关键字；整个枚举共 32 个值，包含 WebDAV 用的 `propfind`、`mkcol` 这些。

响应侧是同一个套路，三步走完：设置状态码、设置头字段、写 body：

```cpp
http::response<http::string_body> res;
res.result(http::status::ok);
res.set(http::field::server, "Beast");
res.set(http::field::content_type, "text/html");
res.body() = "<html><body>Hello</body></html>";
```

`set` 和 `insert` 的区别一句话：**`set` 覆盖同名字段，`insert` 追加同名字段**。头字段的大小写不用管，`http::field` 枚举会生成标准写法；自定义头可以直接传字符串，比如 `res.set("X-My-Header", "value")`。

​

### body：同一份报文，几种存法

报文的 body 怎么存，是模板参数决定的：

| **Body 类型**          | **存储方式**                     | **适用场景**              |
| -------------------- | ---------------------------- | --------------------- |
| `http::string_body`  | `std::string`                | 文本、JSON、小型数据          |
| `http::vector_body`  | `std::vector`                | 二进制数据（图片、小文件）         |
| `http::file_body`    | 文件流                          | 大文件传输，可以零拷贝           |
| `http::dynamic_body` | `boost::beast::multi_buffer` | 流式传输，事先不知道长度          |
| `http::empty_body`   | 无 body                       | HEAD 响应、204/304 这类无内容 |

选 `dynamic_body` 时，body 的底层是 `multi_buffer`，分好几段内存，取内容要用 `beast::buffers_to_string` 拼成 `std::string`：

```cpp
const auto body_str = beast::buffers_to_string(request_.body().data());
```

往 body 里写内容时，`beast::ostream` 比直接赋值省事，尤其对 `dynamic_body` 这种会自动扩容的：

```cpp
beast::ostream(response_.body()) << "Hello, world\n";
```

本篇回调版用 `dynamic_body`、协程版用 `string_body`，就是为了把两种都走一遍。不知道来多大就用 `dynamic_body`，知道是文本就用 `string_body`，后者能直接 `res.body() = ...`，代码短一截。

​

### async\_read：整条报文一次读完

这是 Beast 最省事的一个 API。它在内部循环调用 Asio 的 `async_read_some`，直到**完整的 HTTP 报文被解析完毕**才回调：

```cpp
http::async_read(socket_, buffer_, request_,
    [self](beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);
        if (!ec) {
            self->process_request();
        }
    });
```

因为它是"整条读完才回调"，回调里的 `request_` 一定是完整的，你不需要自己判断"这次读了够不够"。代价是 `bytes_transferred` 基本没用了——它只是这次收到的字节数，调试时才看，不需要时可以用 `boost::ignore_unused` 显式"使用"掉，免得编译器警告。

三条纪律要守住：

`buffer` 和 `request_` 必须活到回调结束——它们都是连接类的成员，靠 `shared_from_this()` 保活，这正是第 3 篇讲的生命周期契约。不要对同一个 socket 并行发起多个 `async_read`——报文解析是有序的，并行会乱。读取成功后 `buffer` 里可能残留下一个请求的数据，长连接时千万别把它当成"用完就扔"。

​

### async\_write 与 Content-Length

写出去之前，报文必须自洽：**`Content-Length` 要和 body 的实际长度一致**，否则客户端会一直等或者提前截断。两种写法：

```cpp
response_.content_length(response_.body().size());   // 手动设置
res.prepare_payload();                                // 自动算
```

`prepare_payload()` 更省事，它按 body 长度自动填 `Content-Length`，并且不会覆盖你手动设过的值——所以先设 body、再调它，顺序别反。对于长度事先不知道的情况，它会改用 `Transfer-Encoding: chunked`，这两者不能同时出现。

​

### 超时：让计时器和读操作赛跑

HTTP 服务器必须防"连上了什么都不发"的客户端，否则连接会一直挂着。套路是第 3 篇用过的那个赛跑：连接建立即启动一个 60 秒定时器，读操作完成了就取消它。

```cpp
deadline_.async_wait([self](beast::error_code ec) {
    if (!ec) {                       // 60 秒到点（被 cancel 则 ec 为 aborted）
        self->socket_.close(ec);     // 超时了
    }
});
```

关键在于判断条件为什么是 `if (!ec)`：**被 `cancel()` 取消的 `async_wait` 以 `operation_aborted` 收尾（`ec` 非零），只有真正到点才是 `ec == 0`**。所以"到点才关连接"写成 `if (!ec)`，反过来写就变成"一响应完就关连接"。

响应写完的地方对应地调一次 `deadline_.cancel()`，让这场赛跑由读操作胜出：

```cpp
self->socket_.shutdown(tcp::socket::shutdown_send, ec);
self->deadline_.cancel();   // 响应写完，取消超时定时器
```

​

### 短连接与长连接：一个连接服务几个请求

本篇的回调版是短连接：`response_.keep_alive(false)` 加写完 `shutdown`，一个连接只服务一个请求就断开。

长连接则是另一回事。HTTP/1.1 默认 `keep_alive`，一个 socket 上会连续来多个请求，处理方式是串行循环：读一个请求、处理、写回响应、再读下一个，一条连接上始终只有一个未完成的读或写。

能不能并行处理同一个连接上的多个请求？可以，但响应必须排队：把请求丢给线程池，连接继续 `async_read`，处理完把响应推进队列，由队列串行地 `async_write` 出去。

***

## 用协程再写一遍：从四个成员函数到一个函数

第 5 篇说过，协程的价值在长流程上最明显。HTTP 会话正好是那种流程：读—处理—写，长连接还要再来一轮。把上面那个 `http_connection` 类（四个成员函数、一个 `shared_from_this()` 保活链、一个定时器成员）翻译成协程，是这样的：

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

​

四个成员函数合并成了一个 `for` 循环，逐个对照：

| **项** | **回调版**                                   | **协程版**                       |
| ----- | ----------------------------------------- | ----------------------------- |
| 会话状态  | 类的四个成员函数 + 三个成员对象                         | 一个函数，全是局部变量                   |
| 对象保活  | `shared_from_this()` 捕获进每个回调              | 不需要，socket 活在协程帧里             |
| 超时    | `steady_timer` 成员 + `check_deadline()` 赛跑 | `cancel_after(60s, ...)` 一个参数 |
| 长连接   | 要写递归回调，或者再拆一个函数                           | `for (;;)` 天然就是循环             |
| 错误路径  | 每个回调各自 `if (!ec)`                         | 一个 `try/catch` 兜住全部           |

**三个细节单独说：**

- **超时**：`cancel_after(60s, ...)` 直接挂在 `async_read` 上，60 秒没读完就取消这次读。取消后会抛 `operation_aborted`，所以 `catch` 里判断 `e.code() == asio::error::operation_aborted`。
- **长连接**：`res.keep_alive(req.keep_alive())` 决定循环要不要继续。
- **buffer 位置**：`flat_buffer` 要放在 `for(;;)` 外面。长连接时，它可能已经读到了下一个请求的一部分；放循环里每轮重建，就会把这些数据丢掉。

跑起来和回调版完全一致，响应体一字不差，连接行为按需不同：

```text
{"email":"test@qq.com","error":0,"msg":"receive email post success"}
```

​

到这里整个系列就结束了。六篇走下来是同一条线：同步 C/S 认识 Asio 的基本形状，同步读写搞清"读满"和"读一点"的区别，异步读写引入回调和对象保活，第 4 篇把这些攒成一台工程上站得住的服务器，第 5 篇用协程把它收进一个函数，本篇换上 HTTP 让服务器说一种真实协议。**工具链的复杂度是递增的，但每一层解决的问题都是上一层留下的**，这也是这个系列的排布逻辑。

***

## 参考资源

- Boost.Beast 官方文档：[https://www.boost.org/doc/libs/release/libs/beast/](https://www.boost.org/doc/libs/release/libs/beast/)
- Boost.Asio 官方文档：[https://think-async.com/Asio/](https://think-async.com/Asio/)
- cppreference（C++20 协程）：[https://en.cppreference.com/w/cpp/language/coroutines](https://en.cppreference.com/w/cpp/language/coroutines)
- llfc 博客（C++ 后端教程）：[https://llfc.club/](https://llfc.club/)
