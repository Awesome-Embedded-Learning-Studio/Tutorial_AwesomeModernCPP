---
title: "选读：给调度器接上 IO——Epoll 与 IOManager"
description: "选读一篇：给只会跑纯计算的调度器接上 IO，epoll 的事件登记把 socket 读写变成 co_await，对照 Boost ASIO 的做法伺候一个迷你 Echo Server，跳过不影响主线。"
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
---

# 选读：用调度器伺候一个 Echo-Server

本篇是选读：主线在《[把结果包起来：教学版 Task](04-task-type)》与《[让协程排队跑起来：最小调度器与两条恢复路线](05-task-scheduler)》两篇，跳过这一篇不影响单元推进。想看调度器伺候真实 IO 的，留下来。上一篇的调度器把纯计算的协程排进了队列，可它只会跑纯计算的活——sleep 之外，它不知道世界上还有 IO 这回事。这一篇给它接上 IO，拿 epoll 把“socket 可以读了”变成一次恢复，伺候一个迷你 Echo Server。

## 前言

咱们很好地完成了一个迷你的调度器`Schedular`，和对应的Task任务抽象。现在咱们来给咱们的工程上难度——利用这个完成一个自己最最简单的Echo Server。

当然，一下子要求您立马完成一个Co Echo Server还是太为难——毕竟咱们一下子从demo跳到由实际场景的应用还是有点跳跃。因此，咱们最好是一步一步来：

- Boost ASIO有自己的协程抽象，咱们看看Boost ASIO是怎么做的
- 咱们替换成自己的协程框架，来完成一个Echo Server

## 看看Boost ASIO如何做的

Boost ASIO有自己的简单的协程抽象。关于Boost ASIO本身的TCP/IP抽象，太巧了，笔者几个月前也写过博客说明和介绍。这里把它贴出来：

> - [Boost ASIO 库深入学习（1）](https://blog.csdn.net/charlie114514191/article/details/148514347)
> - [Boost ASIO 库深入学习（2）](https://blog.csdn.net/charlie114514191/article/details/148517197)
> - [Boost ASIO 库深入学习（3）](https://blog.csdn.net/charlie114514191/article/details/148518447)

所以，这里不再做重复的内容，大家对Boost ASIO有任何疑问，直接查看博客即可。

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

有趣的是，Boost ASIO因为是库而不是标准的语法，因此采用的是函数的方式进行生成和调用。

#### `awaitable<T>`

`awaitable<T>` 是 Boost.Asio 提供的协程返回类型（在协程中可以 `co_await` 异步操作并最终返回 `T`）。在示例中咱们使用 `awaitable<void>` 表示协程没有显式返回值。

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

注意 `listener` 接受 `tcp::acceptor acceptor`（按值移动）。当咱们 `co_await acceptor.async_accept(...)` 得到新 socket 时，使用：

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

## 用一用咱们自己的

### Native Socket Programmings

上面的内容是Boost的Demo，看着还是有一些奇怪，咱们不妨用自己编写的协程框架来试一试Echo Server呢？

当然，笔者这里需要强调一个事情，您如果想要跟着做的话，务必先把Socket的代码从下面的博客复制一份——他主要是创建一个非阻塞的IO，或者为了备份，笔者也放到了附录1，供各位参考。

> - [异步IO编程：Socket RAII封装-charliechen114514的博客](https://www.charliechen114514.tech/archives/li-jie-c-c-yi-bu-iobian-cheng----zuo-yi-ge-raiide-socketchou-xiang)
> - [异步IO编程：Socket RAII封装-CSDN](https://blog.csdn.net/charlie114514191/article/details/152599584)

所以，任何Socket编程相关的API笔者不想重复一个字了。

### Epoll的使用

咱们要做基于协程的Echo Server，实际上就是要封装Epoll来完成咱们的工作。

> 关于Epoll，太巧了笔者也有博客：
>
> - [异步IO编程：Epoll-charliechen114514的博客](https://www.charliechen114514.tech/archives/li-jie-c-c-yi-bu-iobian-cheng-ioduo-lu-fu-yong-ji-shu-yu-epollru-men)
> - [异步IO编程：Epoll-CSDN](https://blog.csdn.net/charlie114514191/article/details/152597436)

所以Epoll本身咱们不会再强调了，如果发现自己对上面的内容不了解，可以先补充了解一下这些内容，然后咱们继续咱们的代码。

## 动手前

咱们现在需要思考——现在咱们打算将Socket IO做在咱们的协程里，上一篇中，咱们已经做好了Task和Schedular的抽象了，实际上，咱们剩下的工作就是将IO准备就绪作为事件的通知源——当IO准备好了，咱们schedule做读写操作。

但是咱们现在有一个严重的问题——原生的调度器中是没有接受事件来源触发调度的！所以咱们要简单的修改代码，让Epoll事件转化为可触发的协程调度。为此，IOManager是需要的——咱们要修改Schedular，让它在run loop中可以嵌入对epoll单例的检测。下面咱们的工作，就是

- 封装Epoll为一个可调度的触发源IOManager，触发调度器的工作
- 修改调度器的代码，引入IOManager来收集一部分IO就绪的routine，推送到就绪队列里

## 修订咱们的调度器：引入IOManager来调度IO映射的协程

咱们不把事情搞复杂——一个线程中咱们已经安排了一个单例（实际上经过昨天跟其他同志进一步的探讨，你也可以宽松一部分单例要求，从严格单例变为thread_local，这是表明一个线程独立的一个调度器），咱们这里也对IOManager上单例，让咱们的工作变得简单一些。

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

Waiter就是咱们的Epoll事件句柄，咱们操作IOManager来得知到底咱们要添加和移除哪一些咱们关心和不关心的IO对应映射的句柄。这样咱们就通过事件来跟对应的句柄搭上联系。

这个“登记句柄、事件到了再恢复”的形状，您在《[co_await 怎样参与等待](03-co-await-and-awaiters)》的 Gate 一节见过同款：Gate 把等待者的句柄登记在门口，set 送值进门时顺手把它叫醒。IOManager 做的是同一件事，只是登记的对象从“一个值到位”换成了“一段 IO 就绪”，叫醒的人从调 set 的代码换成了 epoll 的事件通知。等待登记的思想是同一件事，7.3 那扇门是它的最小样板。

IOManager的工作比较乏味。下面的三个代码笔者想您一看就懂，这里就充当一个热身了

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

add_waiter比较复杂，咱们需要注意的是——咱们的fd也许已经添加进过咱们的epoll了，但是可能没有被设置为咱们感兴趣的属性。

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

咱们做了这么多，不要忘记，咱们的工作是给协程调度器IO预备的协程接口。

```cpp
	// poll events, timeout in ms (-1 block)
	void poll(int timeout_ms, std::vector<std::coroutine_handle<>>& out_handles);
```

上面的接口中，第一个参数是等待timeout_ms毫秒收集指定的IO任务的，第二个参数是填装out_handles。这个参数中承装了咱们的`std::coroutine_handle<>`就绪句柄集合。那么理清楚这个事情，事情变得轻而易举了

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

这样，咱们就只用修改一点代码：

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

完事。


## 参考资源

本篇（前言、Boost ASIO 对照、IOManager 部分）由笔者 2025 年 10 月的博客《理解 C++20 的革命特性——协程引用之——利用协程做一个迷你的 Echo-Server》扩写而来，讲解与代码照博客原文平移。epoll 与 socket 的 API 细节以 Linux man 手册为准，本篇不重复 Socket API 的讲解，需要的博客链接都在正文相应位置。

- [Boost ASIO 库深入学习（1）][asio-1]，Boost ASIO 的 TCP/IP 抽象基础，对照节的背景。
- [Boost ASIO 库深入学习（2）][asio-2]，同系列第二篇。
- [Boost ASIO 库深入学习（3）][asio-3]，同系列第三篇。
- [异步IO编程：Epoll（笔者博客）][epoll-tech]，epoll 多路复用的入门讲解，IOManager 的背景。
- [异步IO编程：Epoll（CSDN）][epoll-csdn]，同篇的 CSDN 版。

[asio-1]: https://blog.csdn.net/charlie114514191/article/details/148514347
[asio-2]: https://blog.csdn.net/charlie114514191/article/details/148517197
[asio-3]: https://blog.csdn.net/charlie114514191/article/details/148518447
[epoll-tech]: https://www.charliechen114514.tech/archives/li-jie-c-c-yi-bu-iobian-cheng-ioduo-lu-fu-yong-ji-shu-yu-epollru-men
[epoll-csdn]: https://blog.csdn.net/charlie114514191/article/details/152597436
