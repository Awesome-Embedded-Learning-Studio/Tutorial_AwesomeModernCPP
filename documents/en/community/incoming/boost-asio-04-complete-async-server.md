---
title: "Boost.Asio Learning (4): A Complete Asynchronous Server"
description: "Upgrade the asynchronous server to be close to a real project: handling packet sticking, sending queue, concurrency safety, decoupling logic threads, and multi-threaded io_context with controlled shutdown"
chapter: 1
order: 4
tags:
  - 网络编程
  - 异步编程
  - 实战
  - host
  - cpp-modern
  - advanced
difficulty: advanced
platform: host
cpp_standard: "20"
reading_time_minutes: 30
community_category: learning-notes
contributors:
  - github: Karl-Heinrich-Marx-520
    name: You Hanling
    roles: [author]
translation:
  source: documents/community/incoming/boost-asio-04-complete-async-server.md
  source_hash: d69ecb759439aaa53d14d5f56ce4f36245f92f53db86fc99a1ff3965d6a9e07c
  translated_at: '2026-11-22T13:48:07+00:00'
  engine: qwen3:14b
  token_count: 32346
---
# Boost.Asio Learning (4): A Complete Asynchronous Server

> **Author**: You Hanling (Karl-Heinrich-Marx-520) · **Original Declaration**: This article is an original learning note summary, and all example code has been written by the author and compiled and run locally (MinGW g++ 13.1 + Boost 1.91, C++20)
> **Main References**: [Boost.Asio Official Documentation](https://think-async.com/Asio/), [cppreference](https://en.cppreference.com/), [llfc blog](https://llfc.club/)
> **Target Readers**: Readers who have read the first three parts and want to build a solid server using modern C++
> **License Statement**: Agree to allow maintainers to adjust the title, format, placement, and some wording; released with the MIT license along with the repository

The server in the third part is still single-threaded: `ioc.run()` runs on only one thread, and all callbacks are executed sequentially. Once a callback in a session performs a time-consuming operation, all connections will be blocked. This part upgrades it to a complete server close to a real project, adding four capabilities—handling packet sticking, sending queue, concurrency safety, decoupling logic threads, and multi-threaded io_context with controlled shutdown. After reading this, you will be able to write a multi-threaded server that can split frames, move business logic out of the IO thread, and handle controlled shutdown. The client modifies the packet format to use the id-length-value frame format.

Note that this part is quite challenging. It is recommended to follow the "Complete Example" reading map first: the first time, just look at what each file is responsible for and get a general idea of the code structure, without reading line by line. After reading the "Detailed Breakdown" section later, go back and read `Session.cpp` carefully.

***

## Complete Example: Multi-threaded + Packet Sticking + Logic Thread Server

The server is divided into eight parts: `const.h` holds protocol constants, `MsgNode` is the in-memory representation of messages, `Session` is responsible for parsing and sending/receiving for a single connection, `Server` listens and manages sessions, `LogicSystem` is the business thread, `AsioThreadPool` is the io_context thread pool, and `main` puts them all together.

This part includes eleven server-side files and one client, which is much longer than the previous parts. It is recommended to read them in the following order: after reading each file, pause and match it with the corresponding section in the "Detailed Breakdown" section.

| **Order** | **File**                      | **Responsible for**             | **Corresponding**        |
| -------- | --------------------------- | -------------------- | ------------- |
| 1        | `const.h`                   | Protocol constants: frame length and various limits         | —             |
| 2        | `MsgNode.h`                 | In-memory representation of messages and ILV packaging      | Packet sticking     |
| 3        | `Singleton.h`               | Common base for two singletons            | —             |
| 4        | `Session.h` → `Session.cpp` | Frame splitting, delivery, and sending for a single connection     | Packet sticking / sending queue     |
| 5        | `Server.h` → `Server.cpp`   | Listening, accepting, registering, and deregistering sessions        | IO thread          |
| 6        | `LogicSystem.h` → `.cpp`    | Business thread: queue + condition variable       | Logic thread          |
| 7        | `AsioThreadPool.h`          | Single io_context + N threads | strand / graceful shutdown |
| 8        | `main.cpp`                  | Putting them all together and registering shutdown signals        | Controlled shutdown          |
| 9        | `client.cpp`                | Test client, sending and receiving one frame according to the frame format      | —             |

​

### Protocol Constants (const.h)

We define the protocol using ILV: the frame header is fixed at 4 bytes, the first 2 bytes are the message ID, and the next 2 bytes are the length of the data part, both in network byte order. All length limits are also centralized here, and the same set of constants is used for validation on the receiving end and rejection on the sending end if the length is too long.

The benefit of centralizing them is that no strange numbers will appear in any other file, and in the future, if the protocol is changed, only this one file needs to be modified.

```cpp
#pragma once

#include <cstddef>

namespace msg {

inline constexpr std::size_t kMaxBufferLength = 1024;  // 单次读取缓冲上限
inline constexpr std::size_t kMsgLengthLimit  = 1024;  // 单条消息 data 部分上限
inline constexpr std::size_t kHeadTotalLength = 4;     // id(2) + len(2)
inline constexpr std::size_t kHeadIdLength    = 2;
inline constexpr std::size_t kHeadDataLength  = 2;
inline constexpr std::size_t kMaxSendQueue    = 1000;
inline constexpr unsigned short kMsgIdMax     = 1024;  // 合法 id 上限（含 1001）

enum class MsgId : unsigned short {
    kHelloWorld = 1001,
};

}
```

​

### Message Node (MsgNode.h)

The message is represented in memory by `MsgNode` as a base class, with two derived classes corresponding to sending and receiving directions.

The sending node is constructed in big-endian format (ID and length are converted to big-endian, using `boost::endian` for byte order conversion), and can be directly passed to `async_write`.

The receiving direction is different. `Session::HandleRead` first parses the message header to get the message ID and message body length, then creates a `RecvNode`, allocates a buffer according to the message body length, and gradually fills in the received message body data.

The base class's destructor is written as `virtual`, and this part will not be destroyed through the base class pointer, but it's also defensive programming.

The buffer is allocated with an extra 1 byte '\0' for debugging print convenience, but it's not required by the protocol.

```cpp
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <boost/endian/conversion.hpp>

#include "const.h"

// 消息体的统一内存表示：连续缓冲区 + 当前填充长度。
class MsgNode {
public:
    explicit MsgNode(std::size_t total_len)
        : data_(total_len + 1, '\0'), total_len_(total_len) {}  // 多 1 字节 '\0'，便于调试打印

    // 多态基类：经基类接口销毁派生对象时保证正确析构
    virtual ~MsgNode() = default;

    void Clear() {
        std::fill(data_.begin(), data_.end(), '\0');
        cur_len_ = 0;
    }

    std::string GetString() const { return std::string(data_.data(), cur_len_); }

    std::vector<char>& data() { return data_; }
    std::size_t total_len() const { return total_len_; }
    std::size_t cur_len() const { return cur_len_; }
    void set_cur_len(std::size_t len) { cur_len_ = len; }

protected:
    std::vector<char> data_;
    std::size_t total_len_;
    std::size_t cur_len_ = 0;
};

// 接收节点
class RecvNode : public MsgNode {
public:
    RecvNode(std::size_t max_len, unsigned short msg_id)
        : MsgNode(max_len), msg_id_(msg_id) {}

    unsigned short GetMsgId() const { return msg_id_; }

private:
    unsigned short msg_id_;
};

// 发送节点 ILV 格式
class SendNode : public MsgNode {
public:
    SendNode(std::string_view payload, unsigned short msg_id)
        : MsgNode(msg::kHeadTotalLength + payload.size()) {
        const auto id_net  = boost::endian::native_to_big(msg_id);
        const auto len_net = boost::endian::native_to_big(
            static_cast<unsigned short>(payload.size()));
        std::memcpy(data_.data(), &id_net, msg::kHeadIdLength);
        std::memcpy(data_.data() + msg::kHeadIdLength, &len_net, msg::kHeadDataLength);
        std::memcpy(data_.data() + msg::kHeadTotalLength, payload.data(), payload.size());
        cur_len_ = total_len_;
    }
};
```

​

### Singleton Template (Singleton.h)

Both the logic system and the thread pool are made into singletons. First, provide the simplest Meyers singleton template, and the two classes inherit from it.

The constructor and destructor of Singleton are protected, meaning that external code cannot construct or destroy Singleton. The only way to get an instance is through `GetInstance()`.

```cpp
#pragma once

template <typename T>
class Singleton {
public:
    static T& GetInstance() {
        static T instance;
        return instance;
    }

    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

protected:
    Singleton() = default;
    ~Singleton() = default;
};
```

​

### Session Class (Session.h)

The session is the core of this part. The thread pool has N threads running the same `io_context` concurrently, and callbacks may be executed by any thread in the thread pool. Therefore, the session needs to hold its own `strand`, and the socket is directly constructed on this `strand`, ensuring that all callbacks are executed sequentially here.

Members owned by the session: one `strand`, one `socket`, a `uuid`, a weak reference to the server, a read buffer, two message nodes, a locked sending queue, and a `writing_` flag.

Pitfalls:

- The `socket_` is directly constructed on the `strand`. This way, the first read from accept and all subsequent read and write callbacks will default to this `strand`, without needing to manually `bind_executor` everywhere.
- A `weak_ptr` is used for the reverse reference instead of a `shared_ptr` to avoid circular references.

```cpp
#pragma once

#include <array>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>

#include <boost/asio.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "const.h"
#include "MsgNode.h"

class Server;

// 一个连接对应一个会话：负责消息解析、发送队列和回调串行化
class Session : public std::enable_shared_from_this<Session> {
public:
    Session(boost::asio::io_context& ioc, std::weak_ptr<Server> server);

    boost::asio::ip::tcp::socket& Socket() { return socket_; }
    const std::string& GetUuid() const { return uuid_; }

    void Start();
    void Send(std::string_view text, unsigned short msg_id);
    void Close();
    void Stop();

private:
    void DoRead();
    void HandleRead(const boost::system::error_code& ec, std::size_t bytes_transferred);
    void StartWrite();  // 仅在 strand 上调用
    void OnError(std::string message);  // 记日志 + 关闭 + 向服务器注销自己

    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::ip::tcp::socket socket_;  // 构造于 strand executor 上
    std::string uuid_;
    std::weak_ptr<Server> server_;  // 反向弱引用：服务器经 sessions_ 强持有本会话

    std::array<char, msg::kMaxBufferLength> read_buf_{};

    bool head_parsed_ = false;
    // 这里用 unique_ptr 即可
    std::unique_ptr<MsgNode> head_node_ =
        std::make_unique<MsgNode>(msg::kHeadTotalLength);
    std::unique_ptr<RecvNode> msg_node_;

    std::mutex send_mutex_;  // 保护发送队列：Send 与 StartWrite 可能并发访问
    std::queue<std::unique_ptr<SendNode>> send_queue_;
    bool writing_ = false;   // 用于 strand 上判断是否有写操作挂起
    bool closing_ = false;
};

// 投递给逻辑线程的一条消息
// shared_ptr 保证消息排队、处理期间 Session 不会提前析构；
// unique_ptr 表示消息体所有权从接收流程移交给逻辑线程。
class LogicNode {
public:
    LogicNode() = default;
    LogicNode(std::shared_ptr<Session> session, std::unique_ptr<RecvNode> node)
        : session_(std::move(session)), recv_node_(std::move(node)) {}

    std::shared_ptr<Session> session_;
    std::unique_ptr<RecvNode> recv_node_;
};
```

​

### Session Implementation (Session.cpp)

This is the core implementation of the entire article, totaling 192 lines. It is recommended to first understand a few common pitfalls before reading it in four sections based on responsibilities.

There is a pitfall about `Session::StartWrite()`:

- The buffer should be constructed before moving ownership. Do not directly construct the buffer in the same `async_write()` call parameter as moving the `node` into the callback. The evaluation order of function parameters is not determined by the source code's order, and may move first and access later, leading to a null pointer dereference.

The first section is the constructor and `Start` / `DoRead` (before `HandleRead`): see how the `socket` is constructed on the `strand`, and how the next read is immediately scheduled after one read is completed.

The second section is `HandleRead`, the packet sticking state machine, which first feeds the bytes read this time to the message header; once the header is complete, it parses the ID and length and performs validation; then it assembles the message body according to the length, and once a complete message is assembled, it is passed to the logic thread; then it continues to consume the remaining bytes in the same batch of data.

The third section is `Send` and `StartWrite`: enqueueing requires a lock, `writing_` ensures that only one write is active at the same time, and the write callback continues to send the next packet. The pitfall of evaluation order is also here.

The fourth section is `Close` and `OnError`: closing is idempotent, and when an error occurs, it is convenient to deregister itself from the server.

```cpp
// Session.cpp
#include "Session.h"

#include <algorithm>
#include <cstring>
#include <iostream>

#include "Server.h"
#include "LogicSystem.h"

Session::Session(boost::asio::io_context& ioc, std::weak_ptr<Server> server)
    : strand_(boost::asio::make_strand(ioc)),
      socket_(strand_),  // socket 直接构造在 strand 上
      server_(std::move(server)) {
    uuid_ = boost::uuids::to_string(boost::uuids::random_generator()());
}

void Session::Start() {
    boost::asio::post(strand_, [self = shared_from_this()] { self->DoRead(); });
}

void Session::DoRead() {
    auto self = shared_from_this();
    socket_.async_read_some(
        boost::asio::buffer(read_buf_),
        boost::asio::bind_executor(strand_,
            [self](const boost::system::error_code& ec, std::size_t bytes) {
                self->HandleRead(ec, bytes);
            }));
}

// 拼凑消息这个函数很繁琐，建议以后都交给AI代劳
void Session::HandleRead(const boost::system::error_code& ec,
                         std::size_t bytes_transferred) {
    if (ec == boost::asio::error::eof) {
        OnError("peer closed gracefully");  // 对端已正常关闭连接
        return;
    }
    if (ec) {
        OnError("read failed: " + ec.message());
        return;
    }

    std::size_t offset = 0;
    while (bytes_transferred > 0) {
        if (!head_parsed_) {
            // ---- 阶段一：凑齐 4 字节头 ----
            const std::size_t head_need =
                msg::kHeadTotalLength - head_node_->cur_len();
            const std::size_t take = std::min(head_need, bytes_transferred);
            std::memcpy(head_node_->data().data() + head_node_->cur_len(),
                        read_buf_.data() + offset, take);
            head_node_->set_cur_len(head_node_->cur_len() + take);
            offset += take;
            bytes_transferred -= take;

            if (head_node_->cur_len() < msg::kHeadTotalLength) {
                break;  // 消息头尚未收完整，剩余部分等待下一次读取
            }

            // ---- 阶段二：解析头 ----
            unsigned short msg_id = 0;
            unsigned short msg_len = 0;
            std::memcpy(&msg_id, head_node_->data().data(), msg::kHeadIdLength);
            std::memcpy(&msg_len,
                        head_node_->data().data() + msg::kHeadIdLength,
                        msg::kHeadDataLength);
            msg_id  = boost::endian::big_to_native(msg_id);
            msg_len = boost::endian::big_to_native(msg_len);

            // id 用无符号类型并校验合法范围：
            // 若用有符号 short，高位为 1 的包会转成负数而绕过检查
            if (msg_id == 0 || msg_id > msg::kMsgIdMax) {
                OnError("invalid msg id: " + std::to_string(msg_id));
                return;
            }
            if (msg_len == 0 || msg_len > msg::kMsgLengthLimit) {
                OnError("invalid msg len: " + std::to_string(msg_len));
                return;
            }

            msg_node_ = std::make_unique<RecvNode>(msg_len, msg_id);
            head_parsed_ = true;
            head_node_->Clear();
            continue;
        }

        // ---- 阶段三：按 msg_len 凑齐消息体 ----
        const std::size_t body_need =
            msg_node_->total_len() - msg_node_->cur_len();
        const std::size_t take = std::min(body_need, bytes_transferred);
        std::memcpy(msg_node_->data().data() + msg_node_->cur_len(),
                    read_buf_.data() + offset, take);
        msg_node_->set_cur_len(msg_node_->cur_len() + take);
        offset += take;
        bytes_transferred -= take;

        if (msg_node_->cur_len() == msg_node_->total_len()) {
            // 消息体所有权移交给逻辑线程
            LogicSystem::GetInstance().Post(
                LogicNode{shared_from_this(), std::move(msg_node_)});
            head_parsed_ = false;  // 继续消费本缓冲区里可能残留的下一个包
            head_node_->Clear();
        }
    }

    DoRead();  // 本批数据处理完毕，继续读
}

void Session::Send(std::string_view text, unsigned short msg_id) {
    // 超长消息直接拒绝，避免 size_t → unsigned short 截断
    if (text.size() > msg::kMsgLengthLimit) {
        std::cerr << "session " << uuid_ << " message too long: "
                  << text.size() << "\n";
        return;
    }
    auto node = std::make_unique<SendNode>(text, msg_id);
    {
        std::lock_guard<std::mutex> lock(send_mutex_);
        if (send_queue_.size() >= msg::kMaxSendQueue) {
            std::cerr << "session " << uuid_ << " send queue full\n";
            return;
        }
        send_queue_.push(std::move(node));
    }
    // 写操作必须投递回 strand，因为 Send 可能由逻辑线程调用。
    boost::asio::post(strand_, [self = shared_from_this()] { self->StartWrite(); });
}

void Session::StartWrite() {  // strand 上执行
    if (writing_) {
        return;  // 已有写操作挂起，等它的回调来续发
    }
    // 把待发包从队列移出：所有权直接交给写回调，写完（或失败）析构
    std::unique_ptr<SendNode> node;
    {
        std::lock_guard<std::mutex> lock(send_mutex_);
        if (send_queue_.empty()) {
            if (closing_) Close();
            return;
        }
        node = std::move(send_queue_.front());
        send_queue_.pop();
    }
    writing_ = true;
    auto self = shared_from_this();
    // 我多次踩坑的工程细节：
    // 应该先求 buffer 再 move。因为async_write 的实参求值顺序未指定，若初始化捕获
    // node = std::move(node) 先执行，buffer(node->total_len()) 会解引用空指针
    auto payload = boost::asio::buffer(node->data().data(), node->total_len());
    boost::asio::async_write(
        socket_, payload,
        boost::asio::bind_executor(strand_,
            [self, node = std::move(node)](  // node 保活：payload 指向它的内存
                const boost::system::error_code& ec, std::size_t) {
                self->writing_ = false;
                if (ec) {
                    self->OnError("write failed: " + ec.message());
                    return;
                }
                self->StartWrite();  // 队列里还有就继续发
            }));
}

void Session::Close() {
    boost::system::error_code ignore;
    socket_.close(ignore);  // 幂等：重复 close 无害；挂起的操作以 aborted 收尾
}

void Session::OnError(std::string message) {
    std::cerr << "session " << uuid_ << ": " << message << "\n";
    Close();
    // 反向引用 weak_ptr 需要解锁升级
    if (auto server = server_.lock()) {
        server->ClearSession(uuid_);
    }
}

void Session::Stop() {
    boost::asio::post(strand_, [self = shared_from_this()] {
        self->closing_ = true;
        bool idle = false;
        {
            std::lock_guard<std::mutex> lock(self->send_mutex_);
            idle = self->send_queue_.empty();
        }
        if (idle && !self->writing_) {
            self->Close();       // 队列空且没有在途写，直接关
        }
        // 否则什么都不做，交给写回调收尾
    });
}
```

​

### Server Class (Server.h)

The `Server` manages all sessions, and holds each session with a `shared_ptr`. The session holds a `weak_ptr` for the reverse reference to avoid circular references.

It also inherits from `enable_shared_from_this`, because `DoAccept` needs to pass `weak_from_this()` to each new session.

The `Server` itself also has a `strand`: the `acceptor_` is constructed on it, and the callbacks of `Start()`, `DoAccept()`, and `Stop()` are all serially executed on this `strand`. This cannot be omitted — the `accept` callback and `Stop()` will access the same `acceptor_` and the same session table, and Asio marks these types of objects as "Shared objects: Unsafe", which are not allowed to be accessed concurrently. After serializing them on the same `strand`, they are executed in sequence, avoiding situations where sessions are missed, such as "iterating through the session table and then registering a new session" after the session table is traversed, or continuing to initiate `accept` on a closed `acceptor`.

```cpp
// Server.h
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <boost/asio.hpp>

class Session;

// 监听并管理所有会话：接受连接、登记会话、按 uuid 注销会话。
class Server : public std::enable_shared_from_this<Server> {
public:
    Server(boost::asio::io_context& ioc, unsigned short port);
    void Start();  // 构造完成后调用：开始接受连接
    void Stop();
    void ClearSession(const std::string& uuid);

private:
    void DoAccept();

    boost::asio::io_context& ioc_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::ip::tcp::acceptor acceptor_;

    std::mutex session_mutex_;  // 保护 sessions_：accept 回调与各会话错误清理并发
    // Server 通过 sessions_ 持有所有会话
    std::unordered_map<std::string, std::shared_ptr<Session>> sessions_;
};
```

​

### Server Implementation (Server.cpp)

Only three functions, but each hides an experience.

In the constructor, you can see why the start must be placed in `Start()`: `DoAccept` needs to pass `weak_from_this()` to the new session, but the object is not yet managed by any `shared_ptr` during the construction period.

`DoAccept` immediately schedules another `accept` after each successful acceptance, so there is always only one `accept` in flight at any time. Note that the failure branch also continues to call `DoAccept`, otherwise a single error would cause the server to permanently stop. However, not all errors are suitable for retrying, for example, after closing or canceling `acceptor_`, the unfinished `accept` operation will end with `operation_aborted`. If `DoAccept()` is still unconditionally called at this point, it may repeatedly retry on a closed `acceptor`, forming a busy loop.

`ClearSession` is used by the session's `OnError` callback, and it locks the session table because deregistration may come from any IO thread.

```cpp
// Server.cpp
#include "Server.h"

#include <iostream>

#include "Session.h"

Server::Server(boost::asio::io_context& ioc, unsigned short port)
    : ioc_(ioc),
      strand_(boost::asio::make_strand(ioc)),
      acceptor_(strand_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {
    // 踩坑经验：
    // 不能在构造函数里直接 DoAccept()：它要把 weak_from_this() 传给会话，
    // 而构造期内对象尚未被任何 shared_ptr 接管。启动统一放在 Start()。
}

void Server::Start() {
    boost::asio::post(strand_, [self = shared_from_this()] {
        self->DoAccept();
    });
}

void Server::DoAccept() {
    auto self = shared_from_this();
    auto new_session = std::make_shared<Session>(ioc_, weak_from_this());
    acceptor_.async_accept(
        new_session->Socket(),
        boost::asio::bind_executor(strand_,
            [self, new_session](const boost::system::error_code& ec) {
                if (!ec) {
                    {
                        std::lock_guard<std::mutex> lock(self->session_mutex_);
                        self->sessions_.emplace(new_session->GetUuid(), new_session);
                    }
                    new_session->Start();
                } else if (ec == boost::asio::error::operation_aborted) {
                    return;
                } else {
                    std::cerr << "accept failed: " << ec.message() << "\n";
                }
                self->DoAccept();
            }));
}

void Server::ClearSession(const std::string& uuid) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    sessions_.erase(uuid);
}

void Server::Stop() {
    boost::asio::post(strand_, [self = shared_from_this()] {
        boost::system::error_code ignore;
        self->acceptor_.close(ignore);

        std::lock_guard<std::mutex> lock(self->session_mutex_);
        for (auto& [uuid, session] : self->sessions_) {
            session->Stop();
        }
    });
}
```

​

### Logic System (LogicSystem.h)

Putting business processing on a separate thread avoids blocking the IO thread, which greatly improves network IO performance. JSON parsing uses nlohmann/json.

The class shows all the assets of the logic thread: a message queue, a lock protecting it, a condition variable, a working thread, and a callback table mapping message IDs to processing functions.

There is also a `std::atomic<bool> stop_` for shutdown: when destructing, the flag is set and the thread is awakened, and the working thread knows when to stop.

`Post` receives `LogicNode` by value, and the caller decides whether to copy or move it; `LogicNode` uses `shared_ptr` for the session and `unique_ptr` for the message body.

```cpp
// LogicSystem.h
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "Session.h"
#include "Singleton.h"

// 逻辑线程：业务处理从 IO 线程解耦，会话把完整消息投递到这里处理。
class LogicSystem : public Singleton<LogicSystem> {
public:
    using Callback = std::function<void(std::shared_ptr<Session>,
                                        unsigned short, const std::string&)>;

    ~LogicSystem();
    void Post(LogicNode msg);  // 按值接收消息，再移动到队列中。

private:
    friend class Singleton<LogicSystem>;
    LogicSystem();
    LogicSystem(const LogicSystem&) = delete;
    LogicSystem& operator=(const LogicSystem&) = delete;

    void RegisterCallbacks();
    void DealMessages();
    void HandleHelloWorld(std::shared_ptr<Session> session,
                          unsigned short msg_id, const std::string& body);

    std::queue<LogicNode> msg_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::unordered_map<unsigned short, Callback> callbacks_;
};
```

​

### Logic System Implementation (LogicSystem.cpp)

The thread is started in the constructor, and during destruction, `stop_` is set, then `notify_all` is called, and finally `join` is called — these three steps are indispensable. If `join` is omitted, the members will be destructed while the thread is still running.

`Post` intentionally receives parameters by value and then moves them into the queue, so the caller doesn't have to worry about the lifetime. The exit condition for `DealMessages` is that `stop_` is true and the queue is empty, meaning that before exiting, all accumulated messages are processed, rather than being discarded directly.

The business side only has one `HandleHelloWorld` as a demonstration: parsing JSON, logging, and sending a response with the same ID. In real projects, this can be replaced with database queries or authentication, as long as it is remembered that it runs on the logic thread and does not block the IO thread.

```cpp
// LogicSystem.cpp
#include "LogicSystem.h"

#include <iostream>

LogicSystem::LogicSystem() {
    RegisterCallbacks();
    worker_ = std::thread([this] { DealMessages(); });
}

LogicSystem::~LogicSystem() {
    stop_ = true;
    queue_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void LogicSystem::Post(LogicNode msg) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        msg_queue_.push(std::move(msg));
    }
    queue_cv_.notify_one();
}

void LogicSystem::RegisterCallbacks() {
    callbacks_[static_cast<unsigned short>(msg::MsgId::kHelloWorld)] =
        [this](std::shared_ptr<Session> session, unsigned short id,
               const std::string& body) {
            HandleHelloWorld(std::move(session), id, body);
        };
}

void LogicSystem::HandleHelloWorld(std::shared_ptr<Session> session,
                                   unsigned short msg_id,
                                   const std::string& body) {
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(body);
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "invalid json: " << e.what() << "\n";
        session->Send(nlohmann::json{{"error", 1001}, {"msg", "invalid json"}}.dump(),
                      msg_id);
        return;
    }

    std::cout << "receive msg id: " << parsed.value("id", 0)
              << " data: " << parsed.value("data", std::string{}) << "\n";

    nlohmann::json reply{
        {"id", parsed.value("id", 0)},
        {"data", "server has received msg: " + parsed.value("data", std::string{})}};
    session->Send(reply.dump(), msg_id);
}

void LogicSystem::DealMessages() {
    while (true) {
        LogicNode node;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return stop_ || !msg_queue_.empty(); });
            if (stop_ && msg_queue_.empty()) {
                break;  // 退出前把队列清空
            }
            node = std::move(msg_queue_.front());
            msg_queue_.pop();
        }
        const auto msg_id = node.recv_node_->GetMsgId();
        if (auto it = callbacks_.find(msg_id); it != callbacks_.end()) {
            it->second(std::move(node.session_), msg_id, node.recv_node_->GetString());
        }
    }
}
```

​

### Thread Pool (AsioThreadPool.h)

The thread pool is a single `io_context` + N threads, and all callbacks share a ready queue, which is consumed concurrently by N threads.

Here, it is intentionally not made into "one thread one `io_context`": sharing one is actually better, as callbacks can automatically spread across N threads, and whichever thread is idle will take the job.

Two details in the members should be remembered first: `work_guard_` determines when `run()` will return, and `threads_` is the object that will be joined in `Stop()`. Their roles will be expanded in the "Controlled Shutdown" section.

In the constructor, `make_work_guard` is called first, then N threads are started, each calling `run()`. The role of `work_guard` is to pin `run()`: without it, the event queue will be empty and `run()` will return, and the server will exit unexpectedly.

`Stop()` is done in two steps — first release `work_guard_`, then `join` all threads. Here, `io_context_.stop()` is not called intentionally, the reason will be explained in the "Controlled Shutdown" section. The second step cannot be omitted, as it is undefined behavior to destruct `io_context_` while the threads are still running.

```cpp
// AsioThreadPool.h
#pragma once

#include <thread>
#include <vector>

#include <boost/asio.hpp>

#include "Singleton.h"

// 单 io_context + N 线程：所有回调共用一个就绪队列，N 个线程并发消费。
// 不同会话的回调可能在不同线程并行执行——共享状态必须显式串行化。
class AsioThreadPool : public Singleton<AsioThreadPool> {
public:
    boost::asio::io_context& GetIOContext() { return io_context_; }
    void Stop() {
    work_guard_.reset();     // 摘掉"保活"，事件队列清空后 run 会返回
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
}
    ~AsioThreadPool() {
    Stop();
}

private:
    friend class Singleton<AsioThreadPool>;

    AsioThreadPool()
    : work_guard_(boost::asio::make_work_guard(io_context_)) {
    const auto thread_count = std::max(1u, std::thread::hardware_concurrency());
    threads_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        threads_.emplace_back([this] { io_context_.run(); });
    }
}
    AsioThreadPool(const AsioThreadPool&) = delete;
    AsioThreadPool& operator=(const AsioThreadPool&) = delete;

    boost::asio::io_context io_context_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
    std::vector<std::thread> threads_;
};
```

​

### Program Entry (main.cpp)

Finally, the entry point: initialize the thread pool, start the server, and register the shutdown signal.

`server->Start()` is on a separate line, corresponding to what was said earlier that "start cannot be placed in the constructor".

The main thread uses `promise` / `future` to wait for the signal, rather than busy waiting or sleeping — the signal callback runs on the IO thread, and when `set_value()` is called, the main thread continues to proceed.

In this example, `signal_set` is associated with `io_context`, so the signal is passed through an asynchronous callback to the event loop for processing.

```cpp
// main.cpp
#include <csignal>
#include <cstdlib>
#include <future>
#include <iostream>

#include <boost/asio.hpp>

#include "AsioThreadPool.h"
#include "Server.h"

int main() {
    try {
        auto& io_context = AsioThreadPool::GetInstance().GetIOContext();
        auto server = std::make_shared<Server>(io_context, 14514);
        server->Start();  // 不能在 Server 构造函数里启动：weak_from_this() 构造期不可用

        std::promise<void> exit_signal;
        auto exit_future = exit_signal.get_future();

        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait(
            [&exit_signal](const boost::system::error_code&, int signal_number) {
                std::cout << "\nSignal " << signal_number
                          << " received, stopping server...\n";
                exit_signal.set_value();
            });

        std::cout << "Server running on port 14514. Press Ctrl+C to stop.\n";
        exit_future.wait();                       // 主线程阻塞等信号
        server->Stop();
        AsioThreadPool::GetInstance().Stop();     // 摘 work guard + join
        std::cout << "Server stopped.\n";
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return 0;
}
```

​

### Client (client.cpp)

The client reuses the synchronous writing method from the third part, but packages according to the frame format of this part: send a JSON with `id=1001`, and after receiving the response, read and print it:

The packaging part uses the same rules as the server's `SendNode` — ID and length each take 2 bytes, both converted to big-endian, then concatenated with the JSON body.

Receiving uses "first read 4 bytes of the header, then read the body according to the length in the header". This is possible because the client is one question and one answer: after sending a frame, it waits, and the response does not contain other data, nor does it have two frames stuck together. The server must handle packet sticking because it does not know when the other end will send or how many frames will be sent at once. `boost::asio::read` will keep reading until the specified number of bytes is read, so this method only works when the number of bytes is certain.

```cpp
// client.cpp
#include <array>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <boost/endian/conversion.hpp>

int main() {
    namespace asio = boost::asio;
    using tcp = asio::ip::tcp;

    try {
        asio::io_context ioc;
        tcp::socket socket(ioc);
        socket.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), 14514));

        // 组装一帧：id(2, 大端) + len(2, 大端) + data
        const std::string body = R"({"id":1001,"data":"hello"})";
        const unsigned short msg_id = 1001;
        const unsigned short msg_len = static_cast<unsigned short>(body.size());

        std::vector<char> frame(4 + body.size());
        const auto id_net  = boost::endian::native_to_big(msg_id);
        const auto len_net = boost::endian::native_to_big(msg_len);
        std::memcpy(frame.data(), &id_net, 2);
        std::memcpy(frame.data() + 2, &len_net, 2);
        std::memcpy(frame.data() + 4, body.data(), body.size());

        boost::asio::write(socket, asio::buffer(frame));

        // 收回包：先读 4 字节头，再按头里的长度读 body
        std::array<char, 4> head{};
        boost::asio::read(socket, asio::buffer(head));
        unsigned short rid = 0, rlen = 0;
        std::memcpy(&rid, head.data(), 2);
        std::memcpy(&rlen, head.data() + 2, 2);
        rid  = boost::endian::big_to_native(rid);
        rlen = boost::endian::big_to_native(rlen);

        std::vector<char> rbody(rlen);
        boost::asio::read(socket, asio::buffer(rbody));
        std::cout << "reply id=" << rid << ": "
                  << std::string(rbody.data(), rlen) << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

## What It Looks Like When Running

Start the server first, then run the client. The client sends a frame, receives a response, and exits. The server detects that the client has closed, deregisters the session, but continues to wait for new connections. The session's `uuid` is random each time, and the following uses ellipsis as a placeholder:

```text
server:
Server running on port 14514. Press Ctrl+C to stop.
receive msg id: 1001 data: hello
session f453ee3e-...: peer closed gracefully

client:
reply id=1001: {"data":"server has received msg: hello","id":1001}
```

The `reply` received by the client is the response from the server's logic thread. The response is generated by the logic thread and sent back to the client through the sending queue. Press Ctrl+C to trigger controlled shutdown:

```text
Signal 2 received, stopping server...
Server stopped.
```

## Overview of the Four Capabilities

Compared to the third part, this part adds the following four capabilities:

| **Capability** | **Solves What**                       | **What It Uses**                        |
| ------ | ------------------------------ | ------------------------------ |
| Packet Sticking Handling | TCP does not preserve message boundaries, so sending and receiving must re-split into frames            | `id-length-value` frame + state machine      |
| Sending Queue   | Only one `async_write` can be active on the same socket at the same time | `send_queue_` + `writing_` loop write |
| Concurrency Safety   | Callbacks execute concurrently under multi-threaded `run()`            | Session uses `strand`, session table uses `mutex`      |
| Decoupling Logic Threads | IO threads cannot be blocked by business logic                   | `LogicSystem` queue + worker thread        |

***

## Detailed Breakdown

### Packet Sticking: How to Re-Split Byte Streams into Frames

TCP only guarantees ordered byte streams and does not preserve message boundaries: two `Send` operations may be read once (packet sticking), and one `Send` may be received in multiple reads (packet splitting). The receiver must re-split the frames according to the protocol.

The frame format in this part:

```text
+--------------+---------------+---------------------+
| msg_id (2B)  | msg_len (2B)  | data (msg_len 字节)  |
+--------------+---------------+---------------------+
  网络字节序      网络字节序       数据
```

The state machine uses `head_parsed_` to transition between "assembling header → parsing header → assembling body → delivering" states. Each round of `while` only takes `min(required, remaining)` bytes, and if multiple packets are read in one read, they are split and processed one by one, and the next read is initiated after processing.

Take an example where 200 bytes are read in one read, and the header is missing 2 bytes: first take 2 bytes to complete the header, parse out the ID and `msg_len`; then take the remaining 198 bytes and assemble the message body according to `msg_len`, and once the message body is assembled, it is delivered to the logic thread, and the state is reset to "assembling header"; if there are remaining bytes, it means that the next packet is following, and the loop continues.

The entire batch of data has only two destinations: either it is split into several complete packets, or the remaining bytes are left in `head_node_` or `msg_node_` to wait for the next read. `read_buf_` itself is reused as a whole and is not split.

Two checks cannot be omitted: the ID must be within `[1, kMsgIdMax]`, and the length must be within `[1, kMsgLengthLimit]`. If they are invalid, the connection is closed. The length determines how much memory to allocate, and without checking the upper limit, the other end can send a `msg_len = 65535` at will, causing you to allocate memory for it. The ID determines where the message is sent, and an invalid ID indicates that the other end is sending random messages.

​

### Sending Queue: Only One `async_write` Can Be Active on a Socket

The third part emphasized a rule: at the same time, only one `async_write` can be active on the same socket. This part's session handles both reading and writing, and may receive something to send from the logic thread, so it uses a sending queue and the `writing_` flag to do "loop writing", starting one write operation at a time, and the write callback continues to send the next packet if the queue is not empty.

`writing_` is not locked because its read and modification are all done on the `strand`, and the `strand` has already queued the callbacks of the same session, making this judgment inherently serial. Conversely, the `send_queue_` enqueue may come from the logic thread, and the code runs outside the `strand`, so it must be locked itself.

Queue full and over-length are directly rejected instead of waiting in line. These two situations indicate that the other end or the business side has already had a problem, and it's better to drop them than to accumulate infinitely.

​

### Strand: How to Serialize in a Multi-threaded Environment

The thread pool has N threads running the same `io_context` concurrently, and a callback may be executed on any thread. If the read callback and write callback of the same session access `read_buf_`, `msg_node_` concurrently, it's a data race.

Therefore, the session holds its own `strand`, and the socket is directly constructed on the `strand`, and all callbacks are serialized back using `bind_executor(strand_, ...)`, so usually no additional locking is needed for these session internal states.

When is the `strand` not sufficient and a real lock must be used? As long as there is a place where the access is not "run on the callback of this `strand`", the `strand` cannot protect it. This part has exactly two such places — the `sessions_` of the `Server` may be added or removed by the session callback on any thread, and the sending queue may be written by the logic thread, so these two places are separately locked.

​

### Logic Thread: Moving Business Logic Out of the IO Thread

After the session receives a complete message, because operations such as parsing JSON and querying the database will block subsequent sends and receives, it cannot directly perform business logic in the IO thread. It needs to pass the message to a dedicated logic thread.

Why not just do the business in the IO thread for convenience? Callbacks are executed sequentially, and if one callback is blocked, other sessions on the same thread will also wait. The thread pool has N threads, which seems to be able to handle N concurrent callbacks, but a single slow query can occupy all these threads at the same time, causing the network layer to stop.

Also note that `session->Send(...)` is initiated from the logic thread, and it internally posts the write operation back to the session's `strand`, so this cross-thread call is safe.

The queue currently has no upper limit, which is a deliberate simplification: under sufficient pressure, it will eat up memory. The "backpressure control" mentioned at the end refers to adding a limit to this — the sending queue already has an upper limit, but the logic queue does not.

Summary: The IO thread is only responsible for moving bytes, and the business thread is responsible for making decisions, with the queue and ownership transfer in between.

​

### Controlled Shutdown: signal_set and work guard

The server in the third part relies on a permanently hanging accept to keep it alive, and Ctrl+C directly kills the process. This part needs to shut down more cleanly.

In this example, `io_context::stop()` is not called, because it will cause already ready handlers to no longer be dispatched, and callbacks for incomplete operations to never be called.

Therefore, these pending operations must be actively canceled, so that they end with `operation_aborted`, run their respective callbacks, and the queue is truly emptied. The shutdown is done in two steps:

1. `Server::Stop()` turns off `acceptor_`, pending accepts end with `operation_aborted`, the callback encounters it and returns directly, not recursively; then iterate through `sessions_` and call `session->Stop()`, which posts back to its own `strand`, and if the queue is empty, it closes, otherwise waits for `async_write` to finish before closing.
2. `AsioThreadPool::Stop()` only releases `work_guard_`, does not call `stop()`, and finally `join()`. Once `work_guard_` is removed, `run()` naturally returns after the queue is empty.

The order cannot be reversed, because if `work_guard_` is removed first, `run()` may return before the pending operations are completed, and the session may not be closed yet.

`signal_set` only does one thing: convert the signal into a single callback. Killing the process directly will not execute destruct, flush, or deregister operations.

As an exercise, the current code implements controlled shutdown, but not a complete graceful shutdown. `Session::Stop()` waits for the send_queue_ to be empty before stopping, but does not wait for messages currently being processed in the logic thread.

The bug is that when a message has already been queued but hasn't been processed yet, the session sees the send_queue_ empty and closes, and the logic thread then `Send`s to a closed socket, and the response is silently dropped. To make sure all pending requests are completed before exiting, `Server::Stop()` needs to add a synchronization point to wait for all sessions to be emptied, and `LogicSystem` should stop before the sessions, with timeout protection added for the cleanup.

***

## A Scalable Skeleton

The server in this part is already a multi-threaded, frame-splitting, business-decoupled, and controlled-shutdown skeleton, but it's still far from production: heartbeat and timeout, disconnection reconnection, log levels, and backpressure control under pressure are not yet implemented. More importantly, callback nesting causes read, write, and business logic to be scattered across several functions — this is the callback hell...

This part can be summarized into three key points: state machine for handling packet sticking, `strand` to ensure serial execution within a session, and logic thread to handle business processing.

## Reference Resources

- Boost.Asio Official Documentation: [https://think-async.com/Asio/](https://think-async.com/Asio/)
- cppreference (Asynchronous Operations and error_code semantics): [https://en.cppreference.com/](https://en.cppreference.com/)
- llfc blog (C++ backend tutorial): [https://llfc.club/](https://llfc.club/)
