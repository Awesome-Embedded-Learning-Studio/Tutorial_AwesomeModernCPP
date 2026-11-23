---
title: "Selected Reading: Three Blocks Assembling an Echo-Server"
description: "On a scheduler with IO wired in, wrap async_read, async_write, async_accept, and run_server as building blocks, assembling a coroutine Echo-Server that can serve real connections."
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
translation:
  source: documents/vol5-concurrency/chapter07-coroutines/08-echo-server.md
  source_hash: 13fdb97afde601ad1d50c7e0d5f618146b5183c3640d58e665de1ca66880da55
  translated_at: '2026-11-22T14:26:00+00:00'
  engine: glm-5.3
  token_count: 11105
---

# Selected Reading: Three Blocks Assembling an Echo-Server

IOManager turned epoll's events into schedulable trigger sources; this article installs the last three blocks — async_read, async_write, async_accept — and assembles run_server. This article carries over the previous one's scheduler and helpers, and simple_log's listing is collected in 7.5's appendix.

## Wrapping async_read, async_write, async_accept, and run_server

First let me show you our interfaces:

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

To effectively reduce confusion, please read the Socket code in Appendix 1 first.

Let's start with the simple part — wrapping async_read and async_write. IOManager has already done all our operations; what we need to do is wrap an Awaiter semantics for all the IO events. That way, the coroutine can know when an IO operation must suspend because there is more to read (placed into epoll).

Think it over — as discussed in [How `co_await` Joins the Wait](03-co-await-and-awaiters), thinking through Awaiter semantics follows these steps:

- await_ready: do we take over the coroutine's suspension? Yes — we want to do some operations ourselves
- await_suspend: once we request suspension, we add the new event to IOManager — this is our agreed convention: IOManager removes it once it takes the event, and the coroutine adds it back to the Awaiter the next time it cares about the event
- await_resume: on resumption we do nothing — just carry straight on with what follows the previously suspended code

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

await_io_event comprises two convenience functions; you will understand at a glance — I trust your level.

### Wrapping `async_read` Right Away (and Making the Semantics Clear)

Think — if we read everything _in one go_, then clearly **there is no need to suspend**: the coroutine can finish the current asynchronous I/O task at once, directly `co_return`ing the number of bytes read, keeping the POSIX-style return semantics (>=0 means the byte count read, 0 means the peer closed gracefully, -1 means an error with errno set).

But if `read()` is not ready (returns -1 with `errno == EAGAIN` / `EWOULDBLOCK`), we put the corresponding FD into the I/O manager's (`IOManager` / `epoll`) watch queue, **suspend the coroutine**, until the scheduler (`Schedular`) wakes it upon detecting the I/O readable. The wake-up chain is roughly: `epoll_wait()` -> `IOManager` marks the event -> the suspended `coroutine_handle` is resumed -> the coroutine continues and retries `read()`.

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

### `async_write`: Beyond Simple, Watch Partial Writes and Errors

On a non-blocking socket, `write()`/`send()` may write partial data (returning a byte count < the requested length) or return `EAGAIN` (buffer full). Hence `async_write` sends in a loop until the whole request is written or an unrecoverable error appears.

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

### `async_accept` and `run_server`

The complexity of listening/accept shows in two parts:

  1. `accept()` may return several connections at once (many completed connections sit in the kernel queue), or none at all (returning EAGAIN).
  2. We want the **accept loop to keep running** while **each connection's handler executes concurrently** — the first connection's handler must not block the accept loop.

Hence our design:

- Provide a `co_accept` coroutine interface: it tries `accept()` in a loop, and if nothing is ready it `co_await`s `EPOLLIN`.
- In the accept loop (`__accept_loop`), **`spawn`** the callback's coroutine out (a new scheduler task) and immediately continue accepting. That way every handler runs independently and never blocks the accept loop.

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

`co_await callback` would "wait for the handler to finish" — parking the accept loop on the first connection, so later connections could never be accepted. `spawn` instead turns the handler into an independent task, returning immediately; the accept loop keeps running, and concurrent clients are supported.

Let's see it in action. The `simple_log` used by the main below is the small logging utility I often use; its listing is collected in this article's appendix at the end:

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

## A Small Task Left for You — Wrap Them into Classes?

In the main text, async_read, async_write, async_accept, and run_server are all loose functions. Try wrapping them into classes — I myself later wrapped out AsyncPassiveSocket and AsyncServerSocket (see the practice repository in the references); once you finish yours, compare against my version.

## References

This article was expanded from my October 2025 blog, Understanding C++20's Revolutionary Feature — On Coroutines: Building a Mini Echo-Server with Coroutines: the preface, the Boost ASIO comparison, and the IOManager and async-wrapper explanations and code were carried over from the blog's original text. The code listings duplicated in the blog's appendix (the full async_socket and IOManager) were not collected again; the non-coroutine socket wrapper is collected in this article's appendix at the end, and my practice repository is [co_practices][repo]. The epoll and socket API details defer to the Linux man pages; this article does not repeat the Socket API explanations, and the needed blog links are at their positions in the body. This article is a selected reading; the main line lives in articles 7.4 and 7.5.

- [Deep Dive into the Boost ASIO Library (1)][asio-1] — the foundations of Boost ASIO's TCP/IP abstraction, the background of the comparison section.
- [Deep Dive into the Boost ASIO Library (2)][asio-2] — the second post of the same series.
- [Deep Dive into the Boost ASIO Library (3)][asio-3] — the third post of the same series.
- [Asynchronous IO Programming: Socket RAII Wrapper (my blog)][socket-raii-tech] — the full telling of the non-coroutine socket wrapper used in the main text; its listing is also collected in this article's Appendix 1.
- [Asynchronous IO Programming: Socket RAII Wrapper (CSDN)][socket-raii-csdn] — the CSDN version of the same post.
- [Asynchronous IO Programming: Epoll (my blog)][epoll-tech] — an introductory telling of epoll multiplexing, the background of IOManager.
- [Asynchronous IO Programming: Epoll (CSDN)][epoll-csdn] — the CSDN version of the same post.

[repo]: https://github.com/Charliechen114514/co_practices
[asio-1]: https://blog.csdn.net/charlie114514191/article/details/148514347
[asio-2]: https://blog.csdn.net/charlie114514191/article/details/148517197
[asio-3]: https://blog.csdn.net/charlie114514191/article/details/148518447
[socket-raii-tech]: https://www.charliechen114514.tech/archives/li-jie-c-c-yi-bu-iobian-cheng----zuo-yi-ge-raiide-socketchou-xiang
[socket-raii-csdn]: https://blog.csdn.net/charlie114514191/article/details/152599584
[epoll-tech]: https://www.charliechen114514.tech/archives/li-jie-c-c-yi-bu-iobian-cheng-ioduo-lu-fu-yong-ji-shu-yu-epollru-men
[epoll-csdn]: https://blog.csdn.net/charlie114514191/article/details/152597436

## Appendix 1: The Asynchronous Non-Coroutine Socket Wrapper

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
