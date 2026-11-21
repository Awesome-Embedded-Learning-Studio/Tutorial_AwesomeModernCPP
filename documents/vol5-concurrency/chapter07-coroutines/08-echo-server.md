---
title: "选读：三块积木拼出 Echo-Server"
description: "在接好 IO 的调度器上封装 async_read、async_write、async_accept 与 run_server 三块积木，拼出一个能伺候真实连接的协程 Echo-Server。"
chapter: 5
order: 8
tags:
  - host
  - cpp-modern
  - intermediate
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
---

# 选读：三块积木拼出 Echo-Server

IOManager 把 epoll 的事件变成了可调度的触发源，这一篇把最后三块积木装上去：async_read、async_write、async_accept，再拼出 run_server。本篇承接上一篇的调度器与 helpers，simple_log 的清单也收在 7.5 的附录里。

## 封装async_read, async_write, async_accept和run_server

笔者先告诉您咱们的接口

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

为了有效的减少困惑，请您先阅读附录1的Socket代码。

咱们先从简单的来——封装async_read和async_write出来。IOManager已经将咱们所有的操作都做好了，咱们要做的就是封装一个Awaiter语义给所有的IO事件。这样，咱们就能让协程知道——什么时候IO操作由于还要继续读需要被挂起了（放置到epoll中）

思考一下，笔者在《[co_await 怎样参与等待](03-co-await-and-awaiters)》就谈到了，思考Awaiter语义要按照如下步骤：

- await_ready：要不要咱们接管协程的挂起？要——咱们要自己做一些操作
- await_suspend：一旦咱们要求挂起，咱们就要添加新的事件给IOManager，这是咱们约定好的——IOManager取到了事件就移除，然后协程再次关心事件的时候加入到Awaiter中
- await_resume恢复的时候咱们什么也不做，直接执行之前被挂起的代码的下文即可

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

await_io_event是两个方便的函数，你一看就懂，相信你的水平。

### 马上封装 `async_read`（并把语义讲清楚）

想一下——如果咱们 _一次性_ 读完了所有内容，那么显然**没必要挂起**：协程可以马上完成当前异步 I/O 的任务，直接 `co_return` 读取到的字节数，保持 POSIX 风格的返回语义（>=0 表示读到的字节数，0 表示对端已优雅关闭，-1 表示出错并设置 errno）。

但如果 `read()` 没有就绪（返回 -1 并且 `errno == EAGAIN` / `EWOULDBLOCK`），这时咱们要把对应的 FD 放到 I/O 管理器（`IOManager` / `epoll`）的监听队列，**挂起协程**，直到调度器（`Schedular`）在检测到 I/O 可读时唤醒它。唤醒链路大致是：`epoll_wait()` -> `IOManager` 标记事件 -> 恢复挂起的 `coroutine_handle` -> 协程继续执行 `read()` 重试。

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

### `async_write`：除了简单，还要注意部分写入与错误

`write()`/`send()` 在非阻塞 socket 下可能会写入部分数据（返回写入字节数 < 请求长度），也可能返回 `EAGAIN`（缓冲区满）。因此 `async_write` 循环发送直到把整个请求写完或遇到不可恢复错误。

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

### `async_accept` 与 `run_server`

监听/accept 的复杂性体现在两部分：

  1. `accept()` 可能一次返回多个连接（内核队列里有很多已完成的连接），或者一次都没有（返回 EAGAIN）。
  2. 咱们希望 **accept 循环持续运行**，并且**每个连接的 handler 并发执行**，不能让第一个连接的 handler 阻塞 accept 循环。

因此咱们的设计思路：

- 提供一个 `co_accept` 协程接口：它循环尝试 `accept()`，如果没有就绪就 `co_await` 等待 `EPOLLIN`。
- 在 accept 循环 (`__accept_loop`) 中把 callback 的协程 **`spawn`**（调度器新任务）出来并立即继续 accept。这样每个 handler 都是独立运行，不会阻塞 accept 循环。

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

`co_await callback` 会“等待 handler 完成”——这会让 accept 循环停在第一个连接，导致后续连接无法被 accept。`spawn` 则把 handler 变成一个独立任务，立即返回，accept 循环继续跑，支持并发客户端。

咱们来看看效果。下面这段 main 用到的 `simple_log` 是笔者常用的日志小工具，清单收在本篇篇末的附录：

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

## 留一个小任务——封装成类？

正文里的 async_read、async_write、async_accept 与 run_server 都是散装的函数。动手把它们封装成类——笔者自己后来封装出了 AsyncPassiveSocket 与 AsyncServerSocket（练手仓库见参考资源），您封完可以跟笔者的一版对一对。

## 参考资源

本篇由笔者 2025 年 10 月的博客《理解 C++20 的革命特性——协程引用之——利用协程做一个迷你的 Echo-Server》扩写而来：前言、Boost ASIO 对照、IOManager 与 async 封装的讲解和代码照博客原文平移，博客附录里重复的代码清单（async_socket 与 IOManager 全文）没有再收，非协程的 socket 封装收在本篇篇末的附录，笔者的练手仓库是 [co_practices][repo]。epoll 与 socket 的 API 细节以 Linux man 手册为准，本篇不重复 Socket API 的讲解，需要的博客链接都在正文相应位置。本篇是选读，正主在 7.4 与 7.5 两篇。

- [Boost ASIO 库深入学习（1）][asio-1]，Boost ASIO 的 TCP/IP 抽象基础，对照节的背景。
- [Boost ASIO 库深入学习（2）][asio-2]，同系列第二篇。
- [Boost ASIO 库深入学习（3）][asio-3]，同系列第三篇。
- [异步IO编程：Socket RAII封装（笔者博客）][socket-raii-tech]，正文用到的非协程 socket 封装的完整讲解，清单也收在本篇附录1。
- [异步IO编程：Socket RAII封装（CSDN）][socket-raii-csdn]，同篇的 CSDN 版。
- [异步IO编程：Epoll（笔者博客）][epoll-tech]，epoll 多路复用的入门讲解，IOManager 的背景。
- [异步IO编程：Epoll（CSDN）][epoll-csdn]，同篇的 CSDN 版。

[repo]: https://github.com/Charliechen114514/co_practices
[asio-1]: https://blog.csdn.net/charlie114514191/article/details/148514347
[asio-2]: https://blog.csdn.net/charlie114514191/article/details/148517197
[asio-3]: https://blog.csdn.net/charlie114514191/article/details/148518447
[socket-raii-tech]: https://www.charliechen114514.tech/archives/li-jie-c-c-yi-bu-iobian-cheng----zuo-yi-ge-raiide-socketchou-xiang
[socket-raii-csdn]: https://blog.csdn.net/charlie114514191/article/details/152599584
[epoll-tech]: https://www.charliechen114514.tech/archives/li-jie-c-c-yi-bu-iobian-cheng-ioduo-lu-fu-yong-ji-shu-yu-epollru-men
[epoll-csdn]: https://blog.csdn.net/charlie114514191/article/details/152597436

## 附录1：异步非协程socket封装

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
