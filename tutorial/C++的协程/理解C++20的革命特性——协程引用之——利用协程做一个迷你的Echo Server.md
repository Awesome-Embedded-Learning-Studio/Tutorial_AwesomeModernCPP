# 理解C++20的革命特性——协程引用之——利用协程做一个迷你的Echo Server

## 前言

​	我们很好的完成了一个迷你的调度器`Schedular`，和对应的Task任务抽象。现在我们来给我们的工程上难度——利用这个完成一个自己最最简单的Echo Server。

​	当然，一下子要求你立马完成一个Co Echo Server还是太为难——毕竟咱们一下子从demo跳到由实际场景的应用还是有点跳跃。因此，咱们最好是一步一步来：

- Boost ASIO有自己的协程抽象，我们看看Boost ASIO是怎么做的
- 我们替换成自己的写成框架，来完成一个Echo Server

## 看看Boost ASIO如何做的

​	Boost ASIO由自己简单的协程抽象。关于Boost ASIO本身的TCP/IP抽象，太巧了，笔者几个月前也写过博客说明和介绍。这里把它贴出来：

> - [Boost ASIO 库深入学习（1）](https://blog.csdn.net/charlie114514191/article/details/148514347)
> - [Boost ASIO 库深入学习（2）](https://blog.csdn.net/charlie114514191/article/details/148517197)
> - [Boost ASIO 库深入学习（3）](https://blog.csdn.net/charlie114514191/article/details/148518447)

​	所以，这里不再做重复的内容，大家对Boost ASIO有任何疑问，直接查看博客即可。

#### 示例代码

```cpp
#include <boost/asio.hpp>
#include <iostream>

using boost::asio::awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;
using boost::asio::ip::tcp;

awaitable<void> echo(tcp::socket socket) {
    try {
        char data[1024];
        for (;;) {
            // 异步读取数据（协程中以 co_await 方式等待）
            std::size_t n = co_await socket.async_read_some(boost::asio::buffer(data), use_awaitable);
            // 异步写回数据
            co_await boost::asio::async_write(socket, boost::asio::buffer(data, n), use_awaitable);
        }
    } catch (std::exception& e) {
        std::cout << "Client disconnected: " << e.what() << "\n";
    }
}

// 接受客户端连接并为每个新连接 spawn 一个 echo 协程
awaitable<void> listener(tcp::acceptor acceptor) {
    for (;;) {
        tcp::socket socket = co_await acceptor.async_accept(use_awaitable);
        std::cout << "New client connected\n";
        // 将 socket 移动进 echo 协程实例，使用 acceptor 的 executor 启动协程
        co_spawn(acceptor.get_executor(), echo(std::move(socket)), detached);
    }
}

int main() {
    try {
        boost::asio::io_context io;

        tcp::endpoint endpoint(tcp::v4(), 12345);
        tcp::acceptor acceptor(io, endpoint);

        // 启动监听协程（在 io_context 的上下文中）
        co_spawn(io, listener(std::move(acceptor)), detached);

        std::cout << "C++20 Boost.Asio TCP Echo Server running on port 12345...\n";
        io.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
}
```

​	有趣的是，Boost ASIO因为是库而不是标准的语法，因此采用的是函数的方式进行生成和调用。

#### `awaitable<T>`

`awaitable<T>` 是 Boost.Asio 提供的协程返回类型（在协程中可以 `co_await` 异步操作并最终返回 `T`）。在示例中我们使用 `awaitable<void>` 表示协程没有显式返回值。

#### `use_awaitable`

这是一个 completion token，用来告诉 Asio：请把这个异步操作适配成可 `co_await` 的形式。常见用法如：

```cpp
co_await socket.async_read_some(buffer, use_awaitable);
```

这行代码会让协程挂起直到异步读取完成，然后返回读取到的字节数（或抛出异常）。

#### `co_spawn`

启动一个协程任务。签名常见形式：

```cpp
co_spawn(executor_or_context, coroutine(), completion_token);
```

- 第一个参数指定在哪个 executor / io_context 上执行协程（线程亲和性、strand 等相关）。
- 最后一个参数是完成策略，`detached` 意味着不关心协程完成结果，适合长生命周期的后台任务；也可以传入回调收集错误或结果。

#### `detached`

表示“分离”启动协程 — 启动并且不等待其返回值（不关心返回结果）。这在服务端接受连接并为每个连接开启处理协程时很常用。

#### `co_await` 与异常

协程内部的 `co_await` 在发生错误时会抛出对应异常，因此常见将协程体包在 `try/catch` 中，以便在连接异常（例如对端断开）时进行清理或记录日志。

#### 移动语义与 executor

注意 `listener` 接受 `tcp::acceptor acceptor`（按值移动）。当我们 `co_await acceptor.async_accept(...)` 得到新 socket 时，使用：

```cpp
co_spawn(acceptor.get_executor(), echo(std::move(socket)), detached);
```

将 socket 移入 echo 协程，且使用 acceptor 的 executor 来保证协程在相同执行上下文运行（避免跨 executor 的资源竞争）。

这些代码可以快速编译——

```bash
g++ -std=c++20 -O2 -pthread main.cpp -lboost_system -o echo_server
./echo_server
```

测试（在另一终端用 `nc`）：

```bash
$ nc localhost 12345
hello
hello          # 服务器会回显你发送的内容
```

## 用一用咱们自己的？

#### Native Socket Programmings

​	上面的内容是Boost的Demo，看着还是有一些奇怪，咱们不妨用自己编写的协程框架来试一试Echo Server呢？

​	当然，笔者这里需要强调一个事情，您如果想要跟着做的话，无比先把Socket的代码从下面的博客复制一份——他主要是创建一个非阻塞的IO，或者为了备份，笔者也放到了附录1，供给位参考。

> - [异步IO编程：Socket RAII封装-charliechen114514的博客](https://www.charliechen114514.tech/archives/li-jie-c-c-yi-bu-iobian-cheng----zuo-yi-ge-raiide-socketchou-xiang)
> - [异步IO编程：Socket RAII封装-CSDN](https://blog.csdn.net/charlie114514191/article/details/152599584)

​	所以，任何Socket编程相关的API笔者不想重复一个字了。

#### Epoll的使用

​	我们要做基于协程的Echo Server，实际上就是要封装Epoll来完成我们的工作。

> 关于Epoll，太巧了笔者也有博客：
>
> - [异步IO编程：Epoll-charliechen114514的博客](https://www.charliechen114514.tech/archives/li-jie-c-c-yi-bu-iobian-cheng-ioduo-lu-fu-yong-ji-shu-yu-epollru-men)
> - [异步IO编程：Epoll-CSDN](https://blog.csdn.net/charlie114514191/article/details/152597436)

​	所以Epoll本身我们不会再强调了，如果发现自己对上面的内容不了解，可以先补充了解一下这些内容，然后我们继续我们的代码。

## 动手前。。。

​	我们现在需要思考——现在我们打算将Socket IO做在我们的协程里，之前的博客中，我们已经做好了Task和Schedular的抽象了，实际上，我们剩下的工作就是将IO准备就绪作为事件的通知源——当IO准备好了，我们schedule做读写操作。

​	但是我们现在有一个严重的问题——原生的调度器中是没有接受事件来源触发调度的！所以我们要简单的修改代码，让Epoll事件转化为可触发的协程调度。为此，IOManager是需要的——我们要修改Schedular，让他在run loop中可以嵌入对epoll单例的检测。下面我们的工作，就是

- 封装Epoll为一个可调度的触发源IOManager，触发调度器的工作
- 修改调度器的代码，引入IOManager来收集一部分IO就绪的routine，推送到就绪队列里

## 修订我们的调度器：引入IOManager来调度IO映射的协程

​	我们不把事情搞复杂——一个线程中我们已经安排了一个单例（实际上经过昨天跟其他同志进一步的探讨，你也可以宽松一部分单例要求，从严格单例变为thread_local，这是表明一个线程独立的一个调度器），我们这里也对IOManager上单例，让我们的工作变得简单一些。

```cpp
class IOManager : public SingleInstance<IOManager> {
public:
	friend class SingleInstance<IOManager>;
	// register a waiter for (fd, events). If already registered, expand/replace.
	void add_waiter(int fd, uint32_t events, std::coroutine_handle<> h);

	// remove a specific watcher
	void remove_waiter(int fd);

	// poll events, timeout in ms (-1 block)
	void poll(int timeout_ms, std::vector<std::coroutine_handle<>>& out_handles);

	// whether there are any watchers
	bool has_watchers() const;

private:
	IOManager();
	~IOManager();

	int epoll_fd { -1 };
	struct Waiter {
		uint32_t events;
		std::coroutine_handle<> handle;
	};
	std::unordered_map<int, Waiter> table;
};
```

​	Waiter就是我们的Epoll事件句柄，我们操作IOManager来得知到底我们要添加和移除哪一些我们关心和不关心的IO对应映射的句柄。这样我们就通过事件来跟对应的句柄搭上练习。

​	IOManager的工作比较乏味。下面的三个代码我想您一看就懂，这里就充当一个热身了

```cpp
IOManager::IOManager() {
	epoll_fd = epoll_create1(0);
	if (epoll_fd < 0) {
		throw std::runtime_error(std::string("epoll_create1 failed: ") + strerror(errno));
	}
}

IOManager::~IOManager() {
	if (epoll_fd >= 0)
		close(epoll_fd);
}

void IOManager::remove_waiter(int fd) {
	auto it = table.find(fd);
	if (it == table.end())
		return;
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
	table.erase(it);
}

```

​	add_waiter比较复杂，我们需要注意的是——我们的fd也许已经添加进过咱们的epoll了，但是可能没有被设置为我们感兴趣的属性。

```cpp
void IOManager::add_waiter(int fd, uint32_t events, std::coroutine_handle<> h) {
	// ensure ET
	uint32_t new_events = events | EPOLLET;
	auto it = table.find(fd);
	if (it == table.end()) {
        // 添加一个映射
		epoll_event ev;
		ev.events = new_events;
		ev.data.fd = fd;
        // 当然，下面的代码是防御性质的，可写可不写，笔者参考别人的代码的
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) != 0) {
			if (errno == EEXIST) {
				// try mod
				if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) != 0) {
					std::cerr << "epoll_ctl MOD failed in add_waiter: " << strerror(errno) << std::endl;
				}
			} else {
				std::cerr << "epoll_ctl ADD failed in add_waiter: " << strerror(errno) << std::endl;
			}
		}
		table.emplace(fd, Waiter { new_events, h });
	} else { // 我们添加过了，咱们就做新映射而不是从头创建
		// merge events & replace handle
		new_events |= it->second.events;
        // 注册进咱们的epoll
		epoll_event ev;
		ev.events = new_events;
		ev.data.fd = fd;
		if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) != 0) {
			std::cerr << "epoll_ctl MOD failed in add_waiter: " << strerror(errno) << std::endl;
		}
        // 建立映射句柄
		it->second.events = new_events;
		it->second.handle = h;
	}
}
```

​	我们做了这么多，不要忘记，我们的工作是给协程调度器IO预备的协程接口。

```cpp
	// poll events, timeout in ms (-1 block)
	void poll(int timeout_ms, std::vector<std::coroutine_handle<>>& out_handles);
```

​	上面的接口中，第一个参数是等待timeout_ms毫秒收集指定的IO任务的，第二个参数是填装out_handles。这个参数中承装了咱们的`std::coroutine_handle<>`就绪句柄集合。那么理清楚这个事情，事情变得轻而易举了

```cpp
void IOManager::poll(int timeout_ms, std::vector<std::coroutine_handle<>>& out_handles) {
	const int MAX_EVENTS = 64;
	epoll_event events[MAX_EVENTS];
	int n = epoll_wait(epoll_fd, events, MAX_EVENTS, timeout_ms);
	if (n < 0) {
		if (errno == EINTR)
			return;
		// std::cerr << "epoll_wait failed: " << strerror(errno) << std::endl;
		return;
	}
    // OK，收集成功，找出来这些内容，他们也许不再被需要了，不要放在这里，所以我们直接移走
    // 让协程重新掌握所有的对IO的控制权
	for (int i = 0; i < n; ++i) {
		int fd = events[i].data.fd;
		auto it = table.find(fd);
		if (it != table.end()) {
			auto handle = it->second.handle;
			// ET semantics: remove registration and 
             // let coroutine re-register if needed
			table.erase(it);
			epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
			if (handle)
				out_handles.push_back(handle);
		}
	}
}
```

​	这样，我们就只用修改一点代码：

```cpp
void Schedular::run() {
		while (!ready_coroutines.empty() || !sleepys.empty() || IOManager::instance().has_watchers()) {
			// resume all ready ones first
			while (!ready_coroutines.empty()) {
				auto front_one = ready_coroutines.front();
				ready_coroutines.pop();
				front_one.resume();
			}

			// move expired sleepers to ready queue
			auto now = current();
			while (!sleepys.empty() && sleepys.top().sleep <= now) {
				ready_coroutines.push(sleepys.top().coro_handle);
				sleepys.pop();
			}

			// compute timeout for epoll (ms)
			int timeout_ms = -1;
			if (!ready_coroutines.empty()) {
				timeout_ms = 0; // 大家都在忙，所以不等待有活就览没活干正事
			} else if (!sleepys.empty()) {
				auto next = sleepys.top().sleep;
				auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(next - now).count();
				timeout_ms = (int)std::max<long long>(0, diff); // 都在睡觉，处理睡觉的
			} else {
				timeout_ms = -1; // block until IO，连睡觉的都没有了，立马处理IO等待
			}

			// POLL IO and collect handles that should be resumed (ET: coroutine will re-register)
			std::vector<std::coroutine_handle<>> ready_from_io;
			IOManager::instance().poll(timeout_ms, ready_from_io);

			// push io-ready handles into ready_coroutines
			for (auto h : ready_from_io) {
				ready_coroutines.push(h);
			}

			// If still nothing ready and there are sleepers, sleep until next sleeper time
			if (ready_coroutines.empty() && !sleepys.empty()) {
				std::this_thread::sleep_until(sleepys.top().sleep);
			}
		}
	}
```

​	完事。

## 封装async_read, async_write, async_accept和run_server

​	笔者先告诉你我们的接口

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

​	为了有效的减少困惑，请您先阅读附录1的Socket代码。

​	我们先从简单的来——封装async_read和async_write出来。IOManager已经将我们所有的操作都做好了，我们要做的就是封装一个Awaiter语义给所有的IO事件。这样，我们就能让协程知道——什么时候IO操作由于还要继续读需要被挂起了（放置到epoll中）

​	思考一下，笔者在第一篇博客就谈到了，思考Awaiter语义要按照如下步骤：

- await_ready：要不要我们接管协程的挂起？要——我们要自己做一些操作
- await_suspend：一旦我们要求挂起，我们就要添加新的事件给IOManager，这是我们约定好的——IOManager取到了事件就移除，然后协程再次关心事件的时候加入到Awaiter中
- await_resume恢复的时候我们什么也不做，直接执行之前被挂起的代码的下文即可

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

​	await_io_event是两个方便的函数，你一看就懂，相信你的水平。

## 马上封装 `async_read`（并把语义讲清楚）

想一下——如果我们 *一次性* 读完了所有内容，那么显然**没必要挂起**：协程可以马上完成当前异步 I/O 的任务，直接 `co_return` 读取到的字节数，保持 POSIX 风格的返回语义（>=0 表示读到的字节数，0 表示对端已优雅关闭，-1 表示出错并设置 errno）。

但如果 `read()` 没有就绪（返回 -1 并且 `errno == EAGAIN` / `EWOULDBLOCK`），这时我们要把对应的 FD 放到 I/O 管理器（`IOManager` / `epoll`）的监听队列，**挂起协程**，直到调度器（`Schedular`）在检测到 I/O 可读时唤醒它。唤醒链路大致是：`epoll_wait()` -> `IOManager` 标记事件 -> 恢复挂起的 `coroutine_handle` -> 协程继续执行 `read()` 重试。

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

------

## `async_write`：除了简单，还要注意部分写入与错误

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

------

## `async_accept` 与 `run_server`

监听/accept 的复杂性体现在两部分：

1. `accept()` 可能一次返回多个连接（内核队列里有很多已完成的连接），或者一次都没有（返回 EAGAIN）。
2. 我们希望 **accept 循环持续运行**，并且**每个连接的 handler 并发执行**，不能让第一个连接的 handler 阻塞 accept 循环。

因此我们的设计思路：

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

​	我们来看看效果：

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

#### 留一个小任务——封装成类？

# 附录1：异步非协程socket封装

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

# 附录2：核心的代码

> async_socket.cpp/.hpp

```cpp
#pragma once
#include "socket.hpp"
#include "task.hpp"
#include <cstddef>
#include <memory>
#include <sys/types.h>
using client_comming_callback_t = Task<void> (*)(std::shared_ptr<PassiveClientSocket> socket);

namespace async_io {

class AsyncPassiveSocket : public Socket {
public:
	AsyncPassiveSocket(const socket_raw_t fd);
	~AsyncPassiveSocket();
	Task<ssize_t> async_read(void* buffer, size_t buffer_size);
	Task<ssize_t> async_write(const void* buffer, size_t buffer_size);

private:
	ssize_t read(void* buffer_ptr, size_t size);
	ssize_t write(const void* buffer_ptr, size_t size);
};

class AsyncServerSocket : public Socket {
public:
	using async_client_comming_callback_t = Task<void> (*)(std::shared_ptr<async_io::AsyncPassiveSocket> socket);
	AsyncServerSocket(AsyncServerSocket&&) = default;

	AsyncServerSocket(const int port)
	    : Socket(INVALID_SOCK_INTERNAL)
	    , open_port(port) {
	}

	socket_raw_t listen();
	void run_server(async_client_comming_callback_t callback);
	int port() const { return open_port; }

private:
	const int open_port;

private:
	std::shared_ptr<async_io::AsyncPassiveSocket> accept();
	Task<std::shared_ptr<async_io::AsyncPassiveSocket>> __async_accept();
	Task<void> __accept_loop(
	    async_io::AsyncServerSocket::async_client_comming_callback_t callback);
	AsyncServerSocket() = delete;
	AsyncServerSocket(const AsyncServerSocket&) = delete;
	AsyncServerSocket& operator=(AsyncServerSocket&&) = delete;
	AsyncServerSocket& operator=(const AsyncServerSocket&) = delete;
};

void run_server(
    std::shared_ptr<ServerSocket> server_socket,
    client_comming_callback_t callback);

Task<ssize_t> async_read(
    std::shared_ptr<PassiveClientSocket> socket, void* buffer, size_t buffer_size);

Task<ssize_t> async_write(
    std::shared_ptr<PassiveClientSocket> socket, const void* buffer, size_t buffer_size);

}

```

```cpp
#include "async_socket.hpp"
#include "helpers.h"
#include "schedular.hpp"
#include "socket.hpp"
#include "task.hpp"
#include <memory>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
// using async_io::WaitForEvent;

namespace {

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

};

async_io::AsyncPassiveSocket::AsyncPassiveSocket(socket_raw_t fd)
    : Socket(fd) {
}

async_io::AsyncPassiveSocket::~AsyncPassiveSocket() {
	IOManager::instance().remove_waiter(socket_fd);
	this->close();
}

ssize_t async_io::AsyncPassiveSocket::read(void* buffer, size_t buffer_size) {
	if (!is_valid())
		throw "invalid socket!";
	ssize_t n;
	do {
		n = ::recv(socket_fd, buffer, buffer_size, 0);
	} while (n < 0 && errno == EINTR);
	return n;
}

ssize_t async_io::AsyncPassiveSocket::write(const void* buffer, size_t buffer_size) {
	if (!is_valid())
		throw "invalid socket!";
	return ::send(socket_fd, buffer, buffer_size, 0);
}

Task<ssize_t> async_io::AsyncPassiveSocket::async_read(void* buffer, size_t buffer_size) {
	while (true) {
		ssize_t n = read(buffer, buffer_size);
		if (n >= 0) {
			co_return n;
		} else {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				co_await await_io_event(socket_fd, EPOLLIN);
				continue;
			} else {
				co_return -1;
			}
		}
	}
}

Task<ssize_t> async_io::AsyncPassiveSocket::async_write(const void* buffer, size_t buffer_size) {
	size_t sent = 0;
	while (sent < buffer_size) {
		ssize_t n = write((const char*)buffer + sent, buffer_size - sent);

		if (n > 0) {
			sent += (size_t)n;
			continue;
		}
		if (n <= 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				co_await await_io_event(socket_fd, EPOLLOUT);
				continue;
			} else {
				co_return -1; // quit
			}
		}
	}
	co_return buffer_size;
}

socket_raw_t async_io::AsyncServerSocket::listen() {
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

Task<void> async_io::AsyncServerSocket::__accept_loop(
    async_io::AsyncServerSocket::async_client_comming_callback_t callback) {
	while (true) {
		auto client_socket = co_await __async_accept();
		Schedular::instance().spawn(callback(client_socket));
	}
}

void async_io::AsyncServerSocket::run_server(async_client_comming_callback_t callback) {
	try {
		listen();
	} catch (...) {
		// log errors
		simple_log("Error happens when listen!");
		return;
	}

	Schedular::instance().spawn(
	    std::move(__accept_loop(callback)));
	Schedular::run();
}

std::shared_ptr<async_io::AsyncPassiveSocket> async_io::AsyncServerSocket::accept() {
	sockaddr_in cli {};
	socklen_t cli_len = sizeof(cli);

	int fd = ::accept4(socket_fd, (sockaddr*)&cli, &cli_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (fd < 0) {
		return nullptr;
	}
	return std::make_shared<async_io::AsyncPassiveSocket>(fd);
}

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

/*-------- Helpers if you use Sync Sockets ----------*/
namespace {
Task<std::shared_ptr<PassiveClientSocket>>
__async_accept(std::shared_ptr<ServerSocket> server_socket) {
	while (true) {
		auto client_socket = server_socket->accept();
		if (client_socket) {
			co_return client_socket;
		}
		co_await await_io_event(server_socket, EPOLLIN);
	}
}

Task<void> __accept_loop(
    std::shared_ptr<ServerSocket> server_socket,
    client_comming_callback_t callback) {
	while (true) {
		auto client_socket = co_await __async_accept(server_socket);
		Schedular::instance().spawn(callback(client_socket));
	}
}
}

void async_io::run_server(
    std::shared_ptr<ServerSocket> server_socket,
    client_comming_callback_t callback) {
	Schedular::instance().spawn(
	    std::move(__accept_loop(server_socket, callback)));
	Schedular::run();
}

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

> IOManager.hpp

```cpp
#pragma once

#include "single_instance.hpp"
#include <coroutine>
#include <cstdint>
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>

class IOManager : public SingleInstance<IOManager> {
public:
	friend class SingleInstance<IOManager>;
	// register a waiter for (fd, events). If already registered, expand/replace.
	void add_waiter(int fd, uint32_t events, std::coroutine_handle<> h);

	// remove a specific watcher
	void remove_waiter(int fd);

	// poll events, timeout in ms (-1 block)
	void poll(int timeout_ms, std::vector<std::coroutine_handle<>>& out_handles);

	// whether there are any watchers
	bool has_watchers() const;

private:
	IOManager();
	~IOManager();

	int epoll_fd { -1 };

	struct Waiter {
		uint32_t events;
		std::coroutine_handle<> handle;
	};

	std::unordered_map<int, Waiter> table;
};

```

> IOManager.cpp

```cpp
#include "io_manager.hpp"
#include <cstring>
#include <errno.h>
#include <iostream>
#include <stdexcept>
#include <sys/epoll.h>
#include <unistd.h>

IOManager::IOManager() {
	epoll_fd = epoll_create1(0);
	if (epoll_fd < 0) {
		throw std::runtime_error(std::string("epoll_create1 failed: ") + strerror(errno));
	}
}

IOManager::~IOManager() {
	if (epoll_fd >= 0)
		close(epoll_fd);
}

void IOManager::add_waiter(int fd, uint32_t events, std::coroutine_handle<> h) {
	// ensure ET
	uint32_t new_events = events | EPOLLET;
	auto it = table.find(fd);
	if (it == table.end()) {
		epoll_event ev;
		ev.events = new_events;
		ev.data.fd = fd;
		if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) != 0) {
			if (errno == EEXIST) {
				// try mod
				if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) != 0) {
					std::cerr << "epoll_ctl MOD failed in add_waiter: " << strerror(errno) << std::endl;
				}
			} else {
				std::cerr << "epoll_ctl ADD failed in add_waiter: " << strerror(errno) << std::endl;
			}
		}
		table.emplace(fd, Waiter { new_events, h });
	} else {
		// merge events & replace handle
		new_events |= it->second.events;
		epoll_event ev;
		ev.events = new_events;
		ev.data.fd = fd;
		if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev) != 0) {
			std::cerr << "epoll_ctl MOD failed in add_waiter: " << strerror(errno) << std::endl;
		}
		it->second.events = new_events;
		it->second.handle = h;
	}
}

void IOManager::remove_waiter(int fd) {
	auto it = table.find(fd);
	if (it == table.end())
		return;
	epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
	table.erase(it);
}

void IOManager::poll(int timeout_ms, std::vector<std::coroutine_handle<>>& out_handles) {
	const int MAX_EVENTS = 64;
	epoll_event events[MAX_EVENTS];
	int n = epoll_wait(epoll_fd, events, MAX_EVENTS, timeout_ms);
	if (n < 0) {
		if (errno == EINTR)
			return;
		// std::cerr << "epoll_wait failed: " << strerror(errno) << std::endl;
		return;
	}
	for (int i = 0; i < n; ++i) {
		int fd = events[i].data.fd;
		auto it = table.find(fd);
		if (it != table.end()) {
			auto handle = it->second.handle;
			// ET semantics: remove registration and let coroutine re-register if needed
			table.erase(it);
			epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
			if (handle)
				out_handles.push_back(handle);
		}
	}
}

bool IOManager::has_watchers() const {
	return !table.empty();
}

```

