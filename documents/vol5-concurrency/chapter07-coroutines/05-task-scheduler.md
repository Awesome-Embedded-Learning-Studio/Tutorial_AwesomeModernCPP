---
title: "让协程排队跑起来：单例、spawn 与睡眠"
description: "Task 的下一站是一个排队的家：单例模板、FIFO 就绪队列、spawn、sleep awaiter 与优先队列怎样搭出一个最小调度器，教材版的 Loop 与 Yield 摆在旁边对照，两条恢复路线各管什么，恢复落在哪条线程也在这篇收。"
chapter: 5
order: 5
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

# 让协程排队跑起来：最小调度器与两条恢复路线

上一篇把结果、异常、销毁收进了一个能用的 Task，可到那为止，协程之间还是各自单打独斗，一条链跑完就收工。看起来协程好鸡肋。啥也干不了。但是别着急，为了咱们可以进一步地发挥协程的威力，咱们给 Task 盖一个排队的家——调度器是 Task 的下一站，本篇就把这个家盖起来。

## 上一篇的亲手题：co_add 是怎么落地的

上一篇的亲手题是让 Task 能 `co_await` 一个返回值，完整的参考实现收在《[把结果包起来：教学版 Task](04-task-type)》的亲手题一节，这里不再把代码 CV 一遍，直接把它的讲究一处一处说。

首先，上一篇咱们已经提到了——任何跑在协程的函数必须返回**协程返回类型**，这个事情要求你不容商量的内嵌一个结构体`struct promise_type`，而且要求你必须实现接口，参考实现里一个接口都没有少。挑跟返回值有关的看：

本篇样例中，咱们不难理解的是——`co_add`并不需要创建协程即挂起，所以咱们只需要返回`std::suspend_never`即可，让咱们立马执行返回的结果`co_return a + b`上，`a + b`被计算好后，会被送到`return_value`当中去，需要注意的是——上一篇咱们已经讨论了生命周期上返回类型和协程句柄本身谁要更长了，这也是为什么咱们选择挂起，这样的话让更上一层的`Task`来负责析构协程对象，而不是它自己解决它自己。这个结构你不会感到陌生的，上一篇已经说明了这个结构到底在干啥。

`co_await`要求等待`Task<int>`，所以任何非空的`Task`还要实现Awaitable接口（注意，不是说带有PromiseType接口的返回结构体都要实现Awaitable接口，而是咱们需要co_await这个接口的时候才需要实现Awaitable接口，请各位搞清楚逻辑关系。）三件套在参考实现里是这样长的：

```cpp
	bool await_ready() {
		simple_log_with_func_name();
		return false; // always need suspend
	}

	void await_suspend(std::coroutine_handle<> h) {
		simple_log_with_func_name(); // Should never be here
		h.resume(); // resume these always, call await_resume then
	}

	T await_resume() {
		simple_log_with_func_name();
		return coroutine_handle.promise().cached_value;
	}
```

尽管逻辑上，咱们实际上不需要挂起接口，但是咱们的结果存储在coroutine_handle的promise_type里了，这个时候——咱们**需要接管等待的逻辑，所以还是要挂起来**。

> await_ready实际上也可以表达为——咱们需要接管等待的逻辑做咱们自己的处理

## 实现一个最简单的调度器

咱们下面马上就来实现一个最简单的调度器了。下面是咱们的要求

> - 写一个单例的**单线程调度器** （event loop），能调度多个 `Task`。（建议编写一个单例模板玩，另外，Task的基本代码由上一篇的亲手题已经做完了）
> - 实现 `sleep(ms)` awaiter
> - 检测能不能用——写 3 个协程并发运行：打印 “A”、“B”、“C”，交替输出。

### 第一步——实现一个单例的模板

笔者决定实现一个简单的单例模板，方便咱们其他项目的复用。关于单例模式的探讨，尽管依赖注入（DI）是更加合适的，但是咱们还是编写基于static的单例模板（协程是C++20才有的，C++11以上已经保证了static的初始化是线程安全的）

> single_instance.hpp

```cpp
#pragma once

template <typename SingleInstanceType>
class SingleInstance {
public:
	static SingleInstanceType& instance() {
		static SingleInstanceType instance;
		return instance;
	}

protected:
	SingleInstance() = default;
	virtual ~SingleInstance() = default;

private:
	SingleInstance(const SingleInstance&) = delete;
	SingleInstance& operator=(const SingleInstance&) = delete;
	SingleInstance(SingleInstance&&) = delete;
	SingleInstance& operator=(SingleInstance&&) = delete;
};
```

很显然，咱们禁用了任何形式的拷贝和构造，而且为了后续的使用方便，咱们要采用安全的虚析构函数。`SingleInstance()`要放到保护域下，咱们的单例子类要访问这个保证咱们在语法上回避第二个实例的创建。使用上，咱们只需要这样书写：

```cpp
class Schedular : public SingleInstance<Schedular>
{
    Schedular() = default; // 还是藏起来我们的构造函数
public:
	friend class SingleInstance<Schedular>;
}
```

> 好巧不巧，笔者写过单例模式的探讨，实现也是C++20的，参考博客：
>
> - [CSDN: 精读C++20设计模式——创造型设计模式：单例模式-CSDN博客](https://blog.csdn.net/charlie114514191/article/details/152166469)
> - [charliechen114514.tech: 精读C++20设计模式——创造型设计模式：单例模式](https://www.charliechen114514.tech/archives/chuang-zao-xing-she-ji-mo-shi-dan-li-mo-shi)

### 第二步：初步修改一下咱们的`Task`，让调度器有机会接管咱们的协程

很显然——咱们现在决定利用调度器来调度咱们的协程了——那么，任何的挂起操作都需要咱们来控制而不是返回结构体自行裁决，为此，咱们的初始化也需要立马被挂起（上一篇亲手题的参考实现里，`initial_suspend` 返回的是 `std::suspend_never`，急启动，创建即跑。交到调度器手里就得反过来：登记要赶在运行前面，7.4 讲过的道理，教材版的 Task 从一开始就是 `suspend_always`）：

```cpp
		// we need suspend when first suspend
		std::suspend_always initial_suspend() {
			// simple_log_with_func_name();
			return {};
		}
```

不管是哪一个泛型实现还是偏特化实现亦是如此。

### 第三步：思考调度器支持的接口

咱们下面准备思考调度器的接口了，好在咱们的协程不是抢占式调度，代码写起来非常的容易（但是容易不太可能），只需要遵循无让出时的FIFO调度就好了。

首先，调度器需要支持Sleep调用，也就是让当前的协程睡大觉（有其他协程任务就做其他的，没有咱们说明当前的线程是需要空闲的，调用`std::this_thread::sleep_*`接口就好了）

所以，咱们需要让调度器知道哪一些协程是需要睡大觉的——调度器需要有一个容器管理谁需要睡觉，和一个推送需要睡觉的指定协程。

需要知道一个事情——标准库为了方便，是存在一个叫做`sleep_until`的接口的，所以，为了方便管理和复用标准库接口，咱们设计一个调度器的sleep_until接口——它表明咱们要休眠到指定的时间点就可以准备被调度（再次强调，咱们需要注意协程的调度是协同调度，咱们只能保证休眠的下限事件）。

```cpp
void Schedular::sleep_until(std::coroutine_handle<> which, // 谁需要休眠？
	                 	std::chrono::steady_clock::time_point until_when);
```

另外，咱们还要有一个推送接口：spawn接口，用来接受协程函数返回结构体。这个结构体的所有调度都要被调度器接管。所以，别忘记在Task处声明调度器类为友元。

```cpp
	template <typename T>
	void Schedular::spawn(Task<T>&& task); // Task只可以被移动，所以放这个接口进来
```

最后还有一个调度接口——run接口

```cpp
void Schedular::run();
```

他将会开始咱们的协程调度。就三个！

### 第四步：实现上面的接口

#### 实现spawn接口，托管协程函数返回的协程返回结构体

咱们先从调度本身开始，首先，咱们需要缓存预备列的协程接口（注意不是Task本身，咱们在调度协程而不是协程的返回结构体），上面提到了咱们的调度策略是FIFO，因此，先来先到就要求咱们采用队列来处理咱们的存储。

```cpp
std::queue<std::coroutine_handle<>> ready_coroutines; // 一个简单的队列即可
```

所以，咱们的spawn接口变得非常好实现——

```cpp
void Schedular::internal_spawn(std::coroutine_handle<> h) {
    // private实现，用户不应该直接随意的触碰调度队列
	ready_coroutines.push(h);	// 加入调度队列
}

// spawn是一个桥接的接口，我们会取出来Task内托管的coroutine_handle协程句柄，交给我们的
// 调度器来管理
template <typename T>
inline void Schedular::spawn(Task<T>&& task) {
	internal_spawn(task.coroutine_handle);
	task.coroutine_handle = nullptr; // 让Task不再托管coroutine_handle本身
}
```

#### 实现睡眠机制

睡眠需要登记咱们睡多久，谁睡觉，而且还要按照一定的优先级排序（你想，如果有三个睡眠请求100ms，200ms，300ms的睡眠，肯定是优先睡眠100ms的，再睡200ms，再睡300ms，反过来的话，前两个黄瓜菜都凉了），显然咱们立马想到了优先级队列。但是优先级队列需要提供比较方法从而产生小/大根堆。所以咱们需要抽象`SleepItem`结构体——它登记咱们的根是睡眠事件最小的。或者说离当前事件点最近的。

```cpp
	struct SleepItem {
		SleepItem(std::coroutine_handle<> h,
		          std::chrono::steady_clock::time_point tp)
		    : coro_handle(h)
		    , sleep(tp) {
		}
		std::chrono::steady_clock::time_point sleep;
		std::coroutine_handle<> coro_handle;
		bool operator<(const SleepItem& other) const {
			return sleep > other.sleep;
		}
	};

	std::priority_queue<SleepItem> sleepys;
```

但是咱们还没有实现用户侧代码，用户期盼咱们可以这样睡眠：

```cpp
co_await sleep(300ms);
```

欸，怎么说的？看到co_await就要条件反射实现awaitable接口。所以——

```cpp
struct AwaitableSleep {
	AwaitableSleep(std::chrono::milliseconds how_long)
	    : duration(how_long)
	    , wake_time(std::chrono::steady_clock::now() + how_long) { }

	/**
	 * @brief await_ready always lets the sessions sleep!
	 *
	 */
	bool await_ready() { return false; } // 总是我们接管剩下的流程
	void await_suspend(std::coroutine_handle<> h) {
         // 执行推送，然后后面我们自己的调度器会取出来这个句柄扔到就绪队列中
		Schedular::instance().sleep_until(h, wake_time);
	}

	// 什么都不做
	void await_resume() { }

private:
	std::chrono::milliseconds duration; // 方便获取接口或者调试，性能优先下可以踢掉这个
	std::chrono::steady_clock::time_point wake_time;
};

inline AwaitableSleep sleep(std::chrono::milliseconds s) {
	return { s };
}
```

#### 实现调度逻辑

首先，睡眠是没活干才做，实现上的优先级很明显了——优先处理活跃的协程！

```cpp
	void run() {
		// if there is any corotines ready or sleepy unfinished
		while (!ready_coroutines.empty() || !sleepys.empty()) {
            // 进来这个逻辑，就表明我们现在是有事情做的——不管是睡大觉还是拉起一个协程。
			while (!ready_coroutines.empty()) {
				auto front_one = ready_coroutines.front();
				ready_coroutines.pop();
				front_one.resume(); // OK, hang this on!
			}

            ...
		}
	}
```

如果咱们任何活跃代码都执行完毕了，咱们才会去检查睡眠队列中有没有待唤醒的家伙——

```cpp
			auto now = current(); // current返回std::chrono::steady_clock::now()
			while (!sleepys.empty() && sleepys.top().sleep <= now) {
				ready_coroutines.push(sleepys.top().coro_handle);
				sleepys.pop();
			}
```

非常好，如果咱们现在的事件越过了指定睡眠唤醒的时间点（也就是sleepys.top().sleep），咱们就要放所有越过了时间点的协程送到咱们的就绪队列中。

下一步，如果咱们还有协程需要睡觉，且没有新的就绪队列到来，咱们立马就对本线程进行睡眠

```cpp
	void run() {
		// if there is any corotines ready or sleepy unfinished
		while (!ready_coroutines.empty() || !sleepys.empty()) {
			while (!ready_coroutines.empty()) {
				auto front_one = ready_coroutines.front();
				ready_coroutines.pop();
				front_one.resume(); // OK, hang this on!
			}

			auto now = current();
			while (!sleepys.empty() && sleepys.top().sleep <= now) {
				ready_coroutines.push(sleepys.top().coro_handle);
				sleepys.pop();
			}

			if (ready_coroutines.empty() && !sleepys.empty()) {
				// OK, we can sleep
				std::this_thread::sleep_until(sleepys.top().sleep);
			}
		}
	}
```

#### 继续修改Task的接口

现在任务需要直接向队列里推送了，咱们需要思考这些问题。咱们使用调度器会这样使用：

```cpp
Task<int> co_add(int a, int b) {
	co_await sleep(300ms);
	co_return a + b;
}

Task<void> worker(const char* name, int a, int b) {
	int result = co_await co_add(a, b);
	std::println("{}: {} + {} = {}", name, a, b, result);
}

Task<void> main_task() {
	co_await worker("TaskA", 1, 2);
	co_await worker("TaskB", 3, 4);
	co_await worker("TaskC", 5, 6);
}
```

所有的父协程会放下自身的运行，按照C++20无栈协程的逻辑——咱们要自己保存协程的句柄。所以咱们很容易想到——Task本身要存储父协程的句柄，方便咱们子协程恢复的时候恢复父协程的运行，才能继续代码。

可能太跳跃了，咱们一个一个慢慢来——咱们的父协程里写下代码——`co_await worker("TaskA", 1, 2);`的时候，父协程就要放弃自己的运行，等待worker的结果。这个时候，咱们回忆《[co_await 怎样参与等待](03-co-await-and-awaiters)》讲过的协程框架的运行逻辑：走`await_ready`查看是否挂起——咱们显然返回了否，要自己接管逻辑。所以下一步的执行流被转发到了`await_suspend`中，这一步就是咱们要的——父协程要被挂起，所以子协程要被推送！

```cpp
	// 在创建的子协程的协程返回体中
	void await_suspend(std::coroutine_handle<> h) {
		// simple_log_with_func_name(); // Should never be here
		simple_log("Current Routine will be suspend!");
		coroutine_handle.promise().parent_coroutine = h;
		simple_log("Child Routine will be called resume!");
		Schedular::instance().internal_spawn(coroutine_handle);
	}
```

`coroutine_handle.promise().parent_coroutine = h;`设置了子协程的父协程为当前线程，然后将子协程放到就绪队列里去。没啥毛病！（注意这个代码是子协程返回结构体）

现在，咱们的子协程就被送到就绪队列中，而且令人兴奋的是——它会被送到就绪的处理逻辑里，当咱们的调度器执行就绪的协程队列代码的时候，咱们就会执行这个逻辑——

```cpp
			while (!ready_coroutines.empty()) {
				auto front_one = ready_coroutines.front();
				ready_coroutines.pop();
				front_one.resume(); // OK, hang this on!
			}
```

子协程在这里被resume了，执行的就是worker的代码——子协程这下就被挂起。当worker执行结束之后，咱们仍然按照流程——调用的是`final_suspend`，还记得咱们存储的parent_coroutine嘛？这里发力了——子协程的结束要求父协程放下执行代码，所以事情变得非常的容易：

```cpp
		std::suspend_always final_suspend() noexcept {
			// simple_log_with_func_name();
			if (parent_coroutine) {
				simple_log("parent_coroutine will be wake up");
                 // 父协程拉起来执行代码
				Schedular::instance().internal_spawn(parent_coroutine);
			}
			return {};	// 子协程由Task结构体托管，这个逻辑不会发生改变
		}
```

走到这里，咱们所有的代码都完成了。咱们编译运行一下：

```text
[charliechen@Charliechen coroutines]$ build/schedular/schedular
10:36:12 :Current Routine will be suspend!
10:36:12 :Child Routine will be called resume!
10:36:12 :Current Routine will be suspend!
10:36:12 :Child Routine will be called resume!
10:36:13 :parent_coroutine will be wake up
TaskA: 1 + 2 = 3
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :parent_coroutine will be wake up
TaskB: 3 + 4 = 7
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :parent_coroutine will be wake up
TaskC: 5 + 6 = 11
```

代码工作得非常完美。

上面的日志如何产生的？答案如下：

```text
[charliechen@Charliechen coroutines]$ build/schedular/schedular
10:36:12 :Current Routine will be suspend! // main_task准备被挂起
10:36:12 :Child Routine will be called resume! // worker("TaskA", 1, 2);准备干活
10:36:12 :Current Routine will be suspend! // worker("TaskA", 1, 2)准备被挂起
10:36:12 :Child Routine will be called resume! // co_add准备干活
10:36:13 :parent_coroutine will be wake up // co_add作为叶子协程，准备结束自己，拉起父协程worker干活
TaskA: 1 + 2 = 3 // worker被拉起，执行打印逻辑

// 如下的逻辑是类似的
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :parent_coroutine will be wake up
TaskB: 3 + 4 = 7
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :Current Routine will be suspend!
10:36:13 :Child Routine will be called resume!
10:36:13 :parent_coroutine will be wake up
TaskC: 5 + 6 = 11
```

## 附录：helpers.h 与 helpers.cpp 清单

调度器代码里用到的两个日志小工具，清单照笔者的仓库原样收在这里。`simple_log` 打一行带时间戳的日志，`simple_log_with_func_name` 顺手把函数名也报出来。

> helpers.h

```cpp
#pragma once
#include <source_location>
#include <string>
void simple_log(const std::string& v, bool request_dump_time = true);

void simple_log_with_func_name(
    const std::string& other = "",
    const std::string& func_name
    = std::source_location::current().function_name(),
    bool request_dump_time = true);
```

> helpers.cpp

```cpp
#include "helpers.h"
#include <chrono>
#include <format>
#include <iomanip>
#include <iostream>
#include <print>

namespace {
void dump_time() {
	auto now = std::chrono::system_clock::now();
	std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
	std::tm localTime;
#ifdef _WIN32
	localtime_s(&localTime, &currentTime); // Windows 平台
#else
	localtime_r(&currentTime, &localTime); // Linux/Unix 平台
#endif

	std::cout << std::put_time(&localTime,
	                           "%H:%M:%S")
	          << " :";
}
}
void simple_log(const std::string& v, bool request_dump_time) {
	if (request_dump_time) {
		dump_time();
	}
	// logings
	std::println("{}", v);
}

void simple_log_with_func_name(
    const std::string& other,
    const std::string& func_name,
    bool request_dump_time) {

	simple_log(std::format(
	               "function: {} is involked, {}", func_name, other),
	           request_dump_time);
}
```
