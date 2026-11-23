---
title: "Selected Reading: Wiring IO into the Scheduler — Epoll and IOManager"
description: "A selected reading: wiring IO into a scheduler that only ran pure computation; epoll's event registration turns socket reads and writes into co_await, and we serve a mini Echo Server after comparing with Boost ASIO's approach — skipping does not affect the main line."
chapter: 5
order: 7
tags:
  - host
  - cpp-modern
  - intermediate
  - coroutine
  - 异步编程
difficulty: intermediate
platform: host
cpp_standard: [20]
current_status:
  title: 正在进行打磨
  detail: 现在是LLM写好的初级稿件，引入是为了初步公开 + 希望收集反馈
  variant: polishing
translation:
  source: documents/vol5-concurrency/chapter07-coroutines/07-echo-io-manager.md
  source_hash: bee274686d6402231b33288cc1d8bd9e382e4febbdaf8a80c60f4cb9ce84697a
  translated_at: '2026-11-22T14:22:30+00:00'
  engine: glm-5.3
  token_count: 11789
---

# Selected Reading: Serving an Echo-Server with the Scheduler

This article is a selected reading: the main line lives in [Wrapping Results: A Teaching-Edition Task](04-task-type) and [Getting Coroutines to Queue Up: A Minimal Scheduler and Two Resumption Routes](05-task-scheduler); skipping this one does not affect the unit's progress. Those who want to see the scheduler serve real IO, stay. The previous article's scheduler queued purely computational coroutines, but it only knows how to run pure computation — beyond sleep, it does not know IO exists in this world. This article wires IO into it, using epoll to turn "the socket is readable" into one resumption, and serves a mini Echo Server.

## Preface

We nicely completed a mini scheduler `Schedular` and the corresponding Task abstraction. Now let's raise the difficulty of our project — use it to build the simplest possible Echo Server of our own.

Of course, demanding you produce a coroutine Echo Server right away would be too much — after all, jumping straight from a demo to a real-scenario application is a bit of a leap. So we had better go step by step:

- Boost ASIO has its own coroutine abstraction; let's see how Boost ASIO does it
- We swap in our own coroutine framework to complete an Echo Server

## See How Boost ASIO Does It

Boost ASIO has its own simple coroutine abstraction. As for Boost ASIO's TCP/IP abstraction itself — what a coincidence, I also wrote blog posts explaining and introducing it a few months ago. Here they are:

> - [Deep Dive into the Boost ASIO Library (1)](https://blog.csdn.net/charlie114514191/article/details/148514347)
> - [Deep Dive into the Boost ASIO Library (2)](https://blog.csdn.net/charlie114514191/article/details/148517197)
> - [Deep Dive into the Boost ASIO Library (3)](https://blog.csdn.net/charlie114514191/article/details/148518447)

So no repeated content here; for any questions about Boost ASIO, just check the blogs.

#### Example Code

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

Interestingly, because Boost ASIO is a library rather than standard syntax, it uses functions for generation and invocation.

#### `awaitable<T>`

`awaitable<T>` is the coroutine return type Boost.Asio provides (inside a coroutine you can `co_await` asynchronous operations and finally return `T`). In the example we use `awaitable<void>` to say the coroutine has no explicit return value.

#### `use_awaitable`

This is a completion token, telling Asio: please adapt this asynchronous operation into a form that can be `co_await`ed. Common usage:

```cpp
co_await socket.async_read_some(buffer, use_awaitable);
```

This line suspends the coroutine until the asynchronous read completes, then returns the number of bytes read (or throws an exception).

#### `co_spawn`

Starts a coroutine task. The signature's common form:

```cpp
co_spawn(executor_or_context, coroutine(), completion_token);
```

- The first parameter specifies on which executor / io_context the coroutine runs (relevant to thread affinity, strands, and the like).
- The last parameter is the completion policy; `detached` means not caring about the coroutine's completion result — suitable for long-lived background tasks. You can also pass a callback to collect errors or results.

#### `detached`

Means starting the coroutine "detached" — launched without waiting for its return value (not caring about the result). This is very common when a server accepts connections and starts a handling coroutine for each.

#### `co_await` and Exceptions

A `co_await` inside the coroutine throws the corresponding exception on errors, so it is common to wrap the coroutine body in `try/catch`, to clean up or log when a connection goes wrong (for example, the peer disconnects).

#### Move Semantics and the Executor

Note that `listener` takes `tcp::acceptor acceptor` (moved by value). When we `co_await acceptor.async_accept(...)` and get a new socket, we use:

```cpp
co_spawn(acceptor.get_executor(), echo(std::move(socket)), detached);
```

to move the socket into the echo coroutine, and to use the acceptor's executor so the coroutine runs in the same execution context (avoiding cross-executor resource races).

This code compiles quickly —

```bash
g++ -std=c++20 -O2 -pthread main.cpp -lboost_system -o echo_server
./echo_server
```

Test (with `nc` in another terminal):

```bash
$ nc localhost 12345
hello
hello          # 服务器会回显你发送的内容
```

## Using Our Own

### Native Socket Programmings

The above is Boost's demo, and it still looks a bit odd; why not try an Echo Server with the coroutine framework we wrote ourselves?

Of course, one thing I must stress here: if you want to follow along, be sure to first copy the Socket code from the blog below — it mainly creates non-blocking IO. For backup, I have also placed it in Appendix 1, for your reference.

> - [Asynchronous IO Programming: Socket RAII Wrapper — charliechen114514's blog](https://www.charliechen114514.tech/archives/li-jie-c-c-yi-bu-iobian-cheng----zuo-yi-ge-raiide-socketchou-xiang)
> - [Asynchronous IO Programming: Socket RAII Wrapper — CSDN](https://blog.csdn.net/charlie114514191/article/details/152599584)

So I do not want to repeat a single word about Socket-programming APIs.

### Using Epoll

To build a coroutine-based Echo Server, we are in effect wrapping Epoll to get our work done.

> About Epoll — what a coincidence, I have blogs on that too:
>
> - [Asynchronous IO Programming: Epoll — charliechen114514's blog](https://www.charliechen114514.tech/archives/li-jie-c-c-yi-bu-iobian-cheng-ioduo-lu-fu-yong-ji-shu-yu-epollru-men)
> - [Asynchronous IO Programming: Epoll — CSDN](https://blog.csdn.net/charlie114514191/article/details/152597436)

So we will not dwell on Epoll itself; if you find yourself unfamiliar with the above, go fill that in first, and then we continue with our code.

## Before We Start

Now we need to think — we plan to put Socket IO inside our coroutines. Last article already finished the Task and Schedular abstractions; what actually remains is making IO-readiness the event notification source — when IO is ready, we schedule the read/write operations.

But we now have a serious problem — the native scheduler has no way to accept event sources that trigger scheduling! So we must modify the code simply, letting Epoll events become triggerable coroutine scheduling. For that, an IOManager is needed — we will modify Schedular so its run loop can embed detection of the epoll singleton. Our work below is:

- Wrap Epoll into a schedulable trigger source, IOManager, triggering the scheduler's work
- Modify the scheduler's code to introduce IOManager, collecting the IO-ready routines and pushing them into the ready queue

## Revising Our Scheduler: Introducing IOManager to Schedule IO-Mapped Coroutines

We won't complicate things — we already arranged one singleton per thread (in fact, after further discussion with comrades yesterday, you can also loosen part of the singleton requirement, going from a strict singleton to thread_local, expressing one independent scheduler per thread); here we also make IOManager a singleton, to make our work a little simpler.

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

Waiter is our Epoll event handle; we operate IOManager to learn exactly which IO-mapped handles we care about and want to add or remove. This way we connect the events with their corresponding handles.

This "register the handle, resume when the event arrives" shape — you saw the same in the Gate section of [How `co_await` Joins the Wait](03-co-await-and-awaiters): Gate registers the waiter's handle at the door, and when set sends a value through the door it wakes the waiter in passing. IOManager does the same thing; only the registered subject changes from "a value has arrived" to "a stretch of IO is ready", and the waker changes from the code calling set to epoll's event notification. The idea of wait registration is one and the same; the gate in 7.3 is its minimal template.

IOManager's work is fairly dull. The three pieces of code below, I think you will understand at a glance; consider them a warm-up.

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

add_waiter is more complicated; what we must note is that our fd may already have been added to our epoll, yet perhaps not set to the attributes we are interested in.

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
	} else { // 咱们添加过了，咱们就做新映射而不是从头创建
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

After all this, do not forget: our job is the coroutine interface prepared for the scheduler's IO.

```cpp
	// poll events, timeout in ms (-1 block)
	void poll(int timeout_ms, std::vector<std::coroutine_handle<>>& out_handles);
```

In the interface above, the first parameter waits timeout_ms milliseconds while collecting the designated IO tasks, and the second fills out_handles. That parameter carries our collection of ready `std::coroutine_handle<>` handles. With that sorted out, things become effortless:

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

This way, we only need to modify a little code:

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

Done.


## References

This article (the preface, the Boost ASIO comparison, and the IOManager part) was expanded from my October 2025 blog, Understanding C++20's Revolutionary Feature — On Coroutines: Building a Mini Echo-Server with Coroutines; the explanations and code were carried over from the blog's original text. The epoll and socket API details defer to the Linux man pages; this article does not repeat the Socket API explanations, and the needed blog links are at their positions in the body.

- [Deep Dive into the Boost ASIO Library (1)][asio-1] — the foundations of Boost ASIO's TCP/IP abstraction, the background of the comparison section.
- [Deep Dive into the Boost ASIO Library (2)][asio-2] — the second post of the same series.
- [Deep Dive into the Boost ASIO Library (3)][asio-3] — the third post of the same series.
- [Asynchronous IO Programming: Epoll (my blog)][epoll-tech] — an introductory telling of epoll multiplexing, the background of IOManager.
- [Asynchronous IO Programming: Epoll (CSDN)][epoll-csdn] — the CSDN version of the same post.

[asio-1]: https://blog.csdn.net/charlie114514191/article/details/148514347
[asio-2]: https://blog.csdn.net/charlie114514191/article/details/148517197
[asio-3]: https://blog.csdn.net/charlie114514191/article/details/148518447
[epoll-tech]: https://www.charliechen114514.tech/archives/li-jie-c-c-yi-bu-iobian-cheng-ioduo-lu-fu-yong-ji-shu-yu-epollru-men
[epoll-csdn]: https://blog.csdn.net/charlie114514191/article/details/152597436
