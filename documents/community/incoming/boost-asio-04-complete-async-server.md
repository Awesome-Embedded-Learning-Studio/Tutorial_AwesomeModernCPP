---
title: "Boost.Asio 学习(4)：一个完备的异步服务器"
description: "把异步服务器升级到接近真实项目：粘包处理、发送队列、并发安全、逻辑线程解耦，以及多线程 io_context 与受控退出"
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
---

# Boost.Asio 学习(4)：一个完备的异步服务器

> **作者**：游翰灵（Karl-Heinrich-Marx-520） · **原创声明**：本文为本人原创学习笔记整理，示例代码均为本人编写并在本地（MinGW g++ 13.1 + Boost 1.91，C++20）编译运行验证
> **主要参考**：[Boost.Asio 官方文档](https://think-async.com/Asio/)、[cppreference](https://en.cppreference.com/)、[llfc 博客](https://llfc.club/)\
> **目标读者**：读完第 1、2、3 篇、想把服务器做扎实的现代 C++ 学习者
> **授权说明**：同意维护者调整标题、格式、放置位置与部分措辞；随仓库以 MIT 许可发布

第 3 篇的服务器还是单线程：`ioc.run()` 只有一个线程，所有回调串行，一旦某个会话的回调里做了耗时操作，所有连接都会被卡住。本篇把它升级成接近真实项目的完备服务器，补上四块能力——粘包处理、发送队列、并发安全、逻辑线程解耦，最后受控退出。读完你能写出一个多线程、能切帧、能把业务挪出 IO 线程的服务器。客户端修改按本篇 id-length-value 帧格式封包。

注意，本篇难度颇高，建议先按「完整示例」开头的读法地图走一遍：第一遍只看每个文件负责什么、代码大致长什么形状，不必逐行；等读完后面「逐个拆解」，再回头精读 `Session.cpp`。

***

## 完整示例：多线程 + 粘包 + 逻辑线程的服务器

服务器拆成八个部分：`const.h` 放协议常量，`MsgNode` 是消息的内存表示，`Session` 负责一条连接的解析与收发，`Server` 监听并管理会话，`LogicSystem` 是业务线程，`AsioThreadPool` 是 io\_context 线程池，`main` 把它们串起来。

此篇共贴十一个服务器侧文件加一个客户端，比前几篇长得多。建议按下面的顺序读：每读完一个文件，就停下来对一遍后面「逐个拆解」里对应的小节

| **顺序** | **文件**                      | **负责什么**             | **对照**        |
| ------ | --------------------------- | -------------------- | ------------- |
| 1      | `const.h`                   | 协议常量：帧长与各种上限         | —             |
| 2      | `MsgNode.h`                 | 消息的内存表示与 ILV 封包      | 粘包            |
| 3      | `Singleton.h`               | 两个单例的公共底座            | —             |
| 4      | `Session.h` → `Session.cpp` | 一条连接里切帧、投递、发送等业务     | 粘包 / 发送队列     |
| 5      | `Server.h` → `Server.cpp`   | 监听、接受、登记与注销会话        | IO线程          |
| 6      | `LogicSystem.h` → `.cpp`    | 业务线程：队列 + 条件变量       | 逻辑线程          |
| 7      | `AsioThreadPool.h`          | 单 io\_context + N 线程 | strand / 优雅退出 |
| 8      | `main.cpp`                  | 把上面串起来并注册退出信号        | 受控退出          |
| 9      | `client.cpp`                | 测试用，按帧格式发一帧、收一帧      | —             |

​

### 协议常量（const.h）

我们按 ILV 把协议确定好：帧头固定 4 字节，前 2 字节是消息 id，后 2 字节是 data 部分的长度，两者都走网络字节序。所有长度上限也集中放在这里，接收端校验和发送端拒绝超长用的是同一组常量。

集中放一处的好处是后面任何一个文件里都不会出现奇奇怪怪的数字，将来改协议只动这一个文件。

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

### 消息节点（MsgNode.h）

消息在内存里用 `MsgNode` 表示作为基类，两个派生类分别对应收发两个方向。

发送节点在构造时就写成大端格式（id 与长度转大端序，用 `boost::endian` 完成字节序转换），装完就可以直接丢给 `async_write` 了。

接收方向则不同。`Session::HandleRead` 先解析消息头，得到消息 ID 和消息体长度，再创建 `RecvNode`，按照消息体长度分配缓冲区，逐步填入接收到的消息体数据。

基类的析构写成 `virtual`，本篇不会经基类指针销毁，顺手防御性编程。

缓冲比标称长度多留 1 字节 '\0' ，纯粹为了调试打印方便，协议上并不需要。

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

### 单例模板（Singleton.h）

逻辑系统和线程池都做成单例，先给一个最简的 Meyers 单例模板，后面两个类继承它。

Singleton 的构造函数和析构函数都是 protected，意味着外部既不能构造也不能销毁 Singleton\，唯一拿到实例的方式就是 GetInstance()。

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

### 会话类（Session.h）

会话是本篇核心。线程池有 N 个线程并发跑同一个 `io_context`，回调可能由线程池中的任意线程执行，所以会话需要持有自己的 `strand`，socket 直接构造在该 strand 上，让它的所有回调都串行到这里。

Session拥有的成员：一条 strand、一条 socket、一个 uuid、一个指向服务器的弱引用、一个读缓冲、两个消息节点，以及一条带锁的发送队列和一个 `writing_` 标志。

踩坑经验

- `socket_` 直接构造在 strand 上。这样从 accept 拿到的第一次读、以及后续每一次读写的回调，都会默认落在这条 strand 上，不用处处手动 `bind_executor`。
- 反向引用用 `weak_ptr` 而不是 `shared_ptr` 避免循环引用。

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

### 会话实现（Session.cpp）

这是全篇最核心的实现，共 192 行。建议先了解几个容易踩坑的细节，再按职责分四段阅读。

这有个关于Session::StartWrite()的踩坑经验：

- 构造缓冲区要先于移动所有权。 不要直接在同一次 `async_write()` 调用的实参中，一边通过 `node` 构造缓冲区，一边将 `node` 移动到回调中。函数实参的求值顺序不由源码中的排列顺序决定，可能先移动、后访问，导致空指针解引用。

第一段是构造函数和 `Start` / `DoRead`（到 `HandleRead` 之前）：看 socket 怎么构造在 strand 上，以及读完一次之后怎么立刻排下一次读。

第二段是 `HandleRead`，粘包状态机，它把这次读到的字节先喂给消息头；头齐了解析出 id 和长度并做校验；再按长度凑消息体，凑齐一个就投给逻辑线程；然后继续消费同一批数据里剩下的字节。

第三段是 `Send` 与 `StartWrite`：入队要加锁、`writing_` 保证同一时刻只有一个写、写回调里续发下一包，那个求值顺序的坑也在这里。

第四段是 `Close` 与 `OnError`：关闭是幂等的，出错时顺手把自己从服务器注销掉。

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

### 服务器类（Server.h）

管理所有会话的是 `Server`，它经 `sessions_` 强持有每个会话。会话反向只持 `weak_ptr`，避免循环引用。

它自己也继承 `enable_shared_from_this`，因为 `DoAccept` 需要把 `weak_from_this()` 交给每个新会话。

`Server` 自己也有一条 strand：`acceptor_` 构造在它上面，`Start()`、`DoAccept()` 的回调和 `Stop()` 全都投到这条 strand 上串行执行。这一条不能省——accept 回调和 `Stop()` 会碰同一个 `acceptor_`、同一张会话表，而 Asio 对这类对象的标注是「Shared objects: Unsafe」，不允许并发访问。串到同一条 strand 之后，两者排成一队依次执行，既不会出现「遍历完会话表之后才登记新会话」这种漏掉会话的情况，也不会在已关闭的 acceptor 上继续发起 accept。

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

### 服务器实现（Server.cpp）

只有三个函数，但每个都藏着一条经验。

构造函数里能看到为什么启动必须放在 `Start()`：`DoAccept` 要把 `weak_from_this()` 交给新会话，而构造期内对象还没被任何 `shared_ptr` 接管。

`DoAccept` 每次接受成功后立刻再挂一个 accept，所以任意时刻只有一个 accept 在飞。注意失败分支同样会继续 `DoAccept`，否则一次错误就让服务器永久停摆。但并非所有错误都适合重试，例如关闭或取消 `acceptor_` 后，未完成的 accept 操作会以 `operation_aborted` 收尾。如果此时仍无条件调用 `DoAccept()`，就可能在已关闭的 acceptor 上不断重试，形成忙循环。

`ClearSession` 是给会话的 `OnError` 回调用的，加锁保护会话表，因为注销可能来自任意一个 IO 线程。

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

### 逻辑系统（LogicSystem.h）

将业务处理放到独立线程，避免阻塞 IO 线程，这样可以极大提高网络 IO 性能。JSON 解析用 nlohmann/json。

类里能看到逻辑线程的全部家当：一条消息队列、一把保护它的锁、一个条件变量、一个工作线程，以及一张「消息 id → 处理函数」的回调表。

还有一个 `std::atomic<bool> stop_`，它是给退出用的：析构时置上标志再唤醒线程，工作线程就知道该收工了。

`Post` 按值接收 `LogicNode`，由调用方决定是拷贝还是 move；`LogicNode` 里会话用 `shared_ptr`、消息体用 `unique_ptr`。

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

### 逻辑系统实现（LogicSystem.cpp）

构造即起线程，析构时置 `stop_` 再 `notify_all` 最后 `join`——这三步缺一不可，少了 `join` 就会在线程还在跑的时候去析构成员。

`Post` 故意按值收参再 move 进队列，调用方就不用操心生命周期。`DealMessages` 的退出条件是「`stop_` 且队列已空」，也就是退出前先把积压的消息处理完，而不是直接丢掉。

业务侧只用一个 `HandleHelloWorld` 做示范：解析 JSON、打日志、按同样的 id 回一包。真实项目里这里换成查库、鉴权都行，只要记住它跑在逻辑线程上，不占用 IO 线程。

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

### 线程池（AsioThreadPool.h）

线程池是单 `io_context` + N 线程，所有回调共用一个就绪队列、由 N 个线程并发消费。

这里刻意没有做成「一个线程一个 `io_context`」：共用一个反而是好事，回调能在 N 个线程之间自动摊开，哪个线程空着谁来干。

成员里有两个细节先记住：`work_guard_` 决定 `run()` 什么时候肯返回，`threads_` 是后面 `Stop()` 要 join 的对象。它们的作用在「受控退出」那节会展开。

构造里先 `make_work_guard`，再起 N 个线程各自 `run()`。`work_guard` 的作用是把 `run()` 钉住：没有它，事件队列一空 `run()` 就会返回，服务器会莫名其妙地退出。

`Stop()` 两步走——先释放 `work_guard_`，再 `join` 所有线程。这里刻意不调 `io_context_.stop()`，原因见后面「受控退出」那节。第二步不能省，线程还在跑的时候析构 `io_context_` 是未定义行为。

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

### 程序入口（main.cpp）

最后是入口，初始化线程池、启动服务器并注册退出信号。

`server->Start()` 单独占一行，对应前面说的「启动不能放在构造函数里」。

主线程用 `promise` / `future` 停下来等信号，而不是忙等或者 sleep——信号回调跑在 IO 线程上，`set_value()` 一到，主线程才继续往下走。

本例将 `signal_set` 关联到 `io_context`，让信号通过异步回调交给事件循环处理。

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

### 客户端（client.cpp）

客户端复用第 3 篇的同步写法，但按本篇的帧格式封包：发一条 `id=1001` 的 JSON，收到回复后读取并打印：

封包部分和服务端的 `SendNode` 是同一套规则——id 与长度各占 2 字节、都转大端，再拼上 JSON 正文。

收包这里用的是「先读 4 字节头、再按头里的长度读 body」。能这么省事，是因为客户端这边是一问一答：发完一帧就等着，回包不会夹带别的数据，也不会有两帧粘在一起。服务端之所以必须做粘包处理，是因为它不知道对端什么时候发、一次发几帧。`boost::asio::read` 会一直读满指定字节数才返回，所以这个写法只在确定字节数时成立。

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

## 跑起来是什么样

先启动服务器，再运行客户端。客户端发一帧、收回包后退出，服务端检测到对端关闭、注销该会话但继续等新连接。会话的 uuid 每次随机，下面用省略号占位：

```text
server:
Server running on port 14514. Press Ctrl+C to stop.
receive msg id: 1001 data: hello
session f453ee3e-...: peer closed gracefully

client:
reply id=1001: {"data":"server has received msg: hello","id":1001}
```

客户端拿到的 `reply` 是服务端逻辑线程处理的回包，回复由逻辑线程生成，再通过发送队列发回客户端。按 Ctrl+C 触发受控退出：

```text
Signal 2 received, stopping server...
Server stopped.
```

## 四块能力总览

与第 3 篇相比，本篇增加了以下四项能力：

| **能力** | **解决什么**                       | **用什么**                        |
| ------ | ------------------------------ | ------------------------------ |
| 粘包处理   | TCP 不保留消息边界，收发要重新切帧            | `id-length-value` 帧 + 状态机      |
| 发送队列   | 同一 socket 同时只能一个 `async_write` | `send_queue_` + `writing_` 循环写 |
| 并发安全   | 多线程 `run()` 下回调并发执行            | 会话用 `strand`，会话表用 `mutex`      |
| 逻辑线程解耦 | IO 线程不能被业务阻塞                   | `LogicSystem` 队列 + 工作线程        |

***

## 逐个拆解

### 粘包：字节流怎么重新切成帧

TCP 只保证字节流有序，不保留消息边界：两次 `Send` 的数据可能一次读到（粘包），一次 `Send` 也可能分几次才收完（拆包）。接收端必须按协议自己切帧。

本篇的帧格式：

```text
+--------------+---------------+---------------------+
| msg_id (2B)  | msg_len (2B)  | data (msg_len 字节)  |
+--------------+---------------+---------------------+
  网络字节序      网络字节序       数据
```

状态机靠 `head_parsed_` 在「凑头 → 解析头 → 凑体 → 投递」之间流转。每轮 `while` 只取 `min(需要, 剩余)` 那么多字节，一次读到几个包就切几个，切完再发下一次读。

拿一次读到 200 字节、而头还差 2 字节举例：先取 2 字节补完头，解析出 id 和 `msg_len`；再从剩下的 198 字节里按 `msg_len` 凑齐消息体，凑齐就投给逻辑线程，状态清回「凑头」；若还有剩，说明跟的是下一个包，循环继续。

整批数据只有两种归宿：要么被切成若干个完整包，要么剩下半截留在 `head_node_` 或 `msg_node_` 里等下次读。`read_buf_` 本身整块复用，不被切走。

校验两条都不能省：id 落在 `[1, kMsgIdMax]`、长度落在 `[1, kMsgLengthLimit]`，非法就关连接。长度决定要分配多少内存，不校验上限，对端随手发个 `msg_len = 65535` 就能让你替它开缓冲；id 决定消息往哪送，非法值说明对端在乱发。

​

### 发送队列：一个 socket 只挂一个 async\_write

第 3 篇强调过一条规则：同一 socket 上同一时刻只能有一个挂起的 `async_write`。本篇的会话同时处理读和写，还可能从逻辑线程收到要发的东西，所以用发送队列加 `writing_` 标志做「循环写」，启动一次写操作后，由写回调在队列非空时继续发送。

`writing_` 之所以不加锁，是因为它的读和改全都发生在 strand 上， `strand`  已经把同一个会话的回调排成一队，这个判断天然是串行的。反过来，`send_queue_` 的入队可能来自逻辑线程，那些代码跑在 strand 之外，所以必须自己加锁。

队列满和超长都直接拒绝，而不是排队等待。这两种情况都说明对端或业务侧已经出了问题，静静丢掉比无限堆积好。

​

### strand：多线程下怎么串行化

线程池有 N 个线程并发跑同一个 `io_context`，一个回调可能在任意线程执行。同一个会话的读回调、写回调若并发访问 `read_buf_`、`msg_node_` 就是数据竞争。

所以会话持有自己的 `strand`，socket 直接构造在  `strand`  上，所有回调都用 `bind_executor(strand_, ...)` 串行回去，因此通常不需要再为这些会话内部状态加锁。

什么时候  `strand`  就不够用、必须上真锁呢？只要有一处访问不是「跑在这条  `strand`  上的回调」， `strand`  就保护不到它。本篇正好有两处—— `Server` 的  `sessions_` 会被任意线程上的会话回调增删，发送队列会被逻辑线程写入，所以这两处都另配了锁。

​

### 逻辑线程：把业务从 IO 线程挪出去

会话收到完整消息后，因为解析 JSON、查库这类操作会阻塞后续收发，所以不能在 IO 线程里直接做业务。需要把消息投递给专门的逻辑线程。

为什么不能图省事直接在 IO 线程里做业务呢？回调是串行执行的，一个回调卡住，落在同一个线程上的其他会话就全在等。线程池有 N 个线程，看起来能兜住 N 个并发回调，但一次慢查询就能把这些线程同时占满，网络层直接停摆。

还要注意 `session->Send(...)` 是从逻辑线程发起的，它内部会把写操作 `post` 回会话的 strand，所以这个跨线程调用是安全的。

队列目前不设上限，这是一处刻意的简化：压力足够大时它会把内存吃掉。结尾说的「背压控制」指的就是给这里补一道限制——发送队列已经有上限了，逻辑队列还没有。

总结：IO 线程只负责搬字节，业务线程才负责做决定，中间靠队列和所有权交接。

​

### 受控退出：signal\_set 与 work guard

第 3 篇的服务器靠一个永远挂起的 accept 续命，Ctrl+C 直接杀进程。本篇要停得干净些。

本例不调用 `io_context::stop()`，因为它会导致已就绪 handler 不再派发、未完成操作的回调永不调用。

所以得主动把这些挂起操作取消掉，让它们以 `operation_aborted` 收尾、跑完各自的回调，队列才真正清空。退出分两步：

1. `Server::Stop()` 关掉 `acceptor_`，挂起的 accept 以 `operation_aborted` 收尾，回调遇到它直接返回、不再递归；再遍历 `sessions_` 调 `session->Stop()`，由它投回自己的 strand，队列空就关，否则等 `async_write` 写完再关。
2. `AsioThreadPool::Stop()` 只释放 `work_guard_`，不调 `stop()`，最后 `join()`。`work_guard_` 一摘，`run()` 在队列清空后自然返回。

顺序不能反，如果先摘 `work_guard_` 的话，`run()` 可能因为暂时没有挂起操作而返回，那时会话还没关。

`signal_set` 做的事只有一件：把信号转成一次回调。直接杀进程的话，析构、flush、注销这些都不会执行。

留个练习吧，目前代码实现的是受控退出，不是完整的优雅退出。`Session::Stop()` 虽然会等 send\_queue\_ 里的消息发完再停，但不会等逻辑线程里正处理的消息。

bug在于：消息已经投进队列、还没轮到处理时，会话看到发送队列空就关了，逻辑线程随后 `Send` 发到一个关闭的 socket 上，响应静默丢掉。想让积压请求也回完再退，得给 `Server::Stop()` 加个同步点等所有会话排空，让 `LogicSystem` 先于会话停，再给收尾加超时保护。

***

## 一个可扩展的骨架

本篇的服务器已经是一个多线程、能切帧、业务解耦、可受控退出的骨架，但离生产还有距离：心跳与超时、断线重连、日志分级、压力下的背压控制都还没做。更重要的是，回调嵌套让读、写、业务散落在好几个函数里，这就是回调地狱……

本篇可以概括为三个要点：状态机处理粘包，strand 保证会话内串行，逻辑线程承载业务处理。

## 参考资源

- Boost.Asio 官方文档：[https://think-async.com/Asio/](https://think-async.com/Asio/)
- cppreference（异步操作与 error\_code 语义）：[https://en.cppreference.com/](https://en.cppreference.com/)
- llfc 博客（C++ 后端教程）：[https://llfc.club/](https://llfc.club/)
