---
title: "mutex 与 RAII 守卫"
description: "互斥锁和RAII锁守卫"
chapter: 10
order: 4
tags:
  - mutex
  - 锁
  - RAII
difficulty: advanced
reading_time_minutes: 18
prerequisites:
  - "Chapter 9: 函数式特性"
cpp_standard: [11, 14, 17, 20]
---

# 嵌入式现代C++开发——mutex与RAII守卫

## 引言

上一章我们讲了无锁数据结构，听起来很酷对吧？但说实话，90%的并发场景其实用不上那么复杂的东西。一把好用的互斥锁，配合正确的使用模式，就能解决大部分问题。

问题是，锁这东西用错了代价很大：忘记解锁会死锁、异常跳出会死锁、多个锁加锁顺序不对也会死锁。传统C风格的`lock()`/`unlock()`写法，在复杂代码里几乎不可能保证正确性。

C++给我们带来了RAII（Resource Acquisition Is Initialization）这个"魔法"。把锁的生命周期绑定到作用域，构造时加锁、析构时解锁，无论怎么退出（正常return、抛异常、early exit）都能正确释放。这不仅仅是语法糖，而是从根本上消除了一整类bug。

这一章我们会深入`std::mutex`的各种封装：`lock_guard`、`unique_lock`、`scoped_lock`，以及它们在嵌入式场景下的最佳实践。

> 一句话总结：**永远不要手动调用`lock()`和`unlock()`，用RAII锁守卫让编译器帮你做对。**

------

## std::mutex基础：别手动用它

`std::mutex`是C++标准库提供的最基础的互斥量，提供了`lock()`、`unlock()`、`try_lock()`三个操作。

### 看起来简单，用起来容易错

```cpp
#include <mutex>

std::mutex mtx;
int shared_counter = 0;

void bad_increment() {
    mtx.lock();          // 手动加锁
    shared_counter++;
    // 如果这里抛异常...unlock永远不会执行！
    mtx.unlock();        // 手动解锁
}
```text

这段代码有几个致命问题：

1. 如果`shared_counter++`抛出异常（虽然int不会，但换成复杂类型就可能），`unlock()`永远不会执行
2. 如果中间有多个return路径，很容易忘记在某个return前unlock
3. 如果代码逻辑复杂，很难看出锁的生命周期

### 更糟糕的情况：嵌套调用

```cpp
void function_a() {
    mtx.lock();
    // 调用另一个函数
    function_b();
    mtx.unlock();
}

void function_b() {
    mtx.lock();  // 死锁！同一把锁不能加两次
    // ...
    mtx.unlock();
}
```text

这种情况下，程序直接卡死。虽然这个例子很傻，但在大型代码库里，函数调用链复杂，很容易不知不觉就犯了这种错。

### 结论：别用裸锁

`std::mutex`的设计初衷不是让你直接用`lock()`/`unlock()`，而是作为各种RAII封装的底层实现。我们接下来要讲的`lock_guard`、`unique_lock`这些，才是你应该天天用的工具。

------

## lock_guard：简单场景的首选

`std::lock_guard`是C++11引入的最简单的RAII锁封装，也是90%场景下你应该选择的工具。

### 基本用法

```cpp
#include <mutex>

std::mutex mtx;
int shared_counter = 0;

void good_increment() {
    std::lock_guard<std::mutex> lock(mtx);  // 构造时自动lock
    shared_counter++;
    // 无论这里怎么退出（return、异常），lock析构时都会自动unlock
}
```text

就这么简单。`lock_guard`的构造函数会调用`mtx.lock()`，析构函数会调用`mtx.unlock()`。C++保证局部对象离开作用域时一定会调用析构函数，即使发生异常也是如此。

### 关键特性

`lock_guard`的设计哲学是"简单即美"：

1. **不可复制**：删除了拷贝构造和拷贝赋值
2. **不可移动**：C++17之前不支持移动，C++17支持但很少用
3. **不可手动解锁**：没有`unlock()`方法，必须等析构
4. **构造即加锁**：不能延迟加锁，必须持有锁才能构造

这些限制确保了使用方式清晰简单，不会出错。

### 嵌入式场景示例：保护共享外设状态

```cpp
class SPI_Driver {
public:
    void transfer(const uint8_t* tx, uint8_t* rx, size_t length) {
        std::lock_guard<std::mutex> lock(mtx);

        // 检查外设是否被占用
        if (busy) {
            return;  // 或者等待、返回错误
        }

        busy = true;
        // 执行SPI传输...
        hardware_transfer(tx, rx, length);

        busy = false;
        // lock离开作用域，自动解锁
    }

private:
    std::mutex mtx;
    bool busy = false;

    void hardware_transfer(const uint8_t* tx, uint8_t* rx, size_t length);
};
```text

这里用`lock_guard`有几个好处：

- 异常安全：如果`hardware_transfer()`抛异常，锁仍然会释放
- 代码清晰：一看就知道临界区的范围
- 性能可控：临界区就是整个函数体，简单明了

### 常见错误：忘记给变量命名

```cpp
// ❌ 错误：临时对象立即析构
void bad_function() {
    std::lock_guard<std::mutex>(mtx);  // 创建临时对象，立即析构！
    // 这里的代码没有受保护
    shared_counter++;
}

// ✅ 正确：给变量命名
void good_function() {
    std::lock_guard<std::mutex> lock(mtx);  // lock有名字，生命周期是整个作用域
    shared_counter++;
}
```text

这是个新手常犯的错误，编译器通常不会警告，所以一定要小心。

### 构造函数选项：adopt_lock

`lock_guard`还有一个不太常用但有时很有用的构造选项：

```cpp
void complex_function() {
    mtx.lock();  // 手动加锁（某些特殊情况）

    // 用adopt_lock告诉lock_guard：锁已经持有了，不要重复lock
    std::lock_guard<std::mutex> lock(mtx, std::adopt_lock);

    // 临界区代码...

    // 离开作用域时，lock仍然会unlock
}
```text

这个选项主要用于把手动加锁的代码迁移到RAII风格，或者某些需要手动控制加锁时机的特殊场景。但大部分情况下，你应该让`lock_guard`自己管理加锁。

------

## unique_lock：复杂场景的瑞士军刀

`std::unique_lock`是功能更强大也更灵活的RAII锁封装。它提供了`lock_guard`没有的几个关键能力：延迟加锁、手动解锁、条件变量支持、锁所有权转移。

### 基本用法

```cpp
std::mutex mtx;

void function_with_unique_lock() {
    std::unique_lock<std::mutex> lock(mtx);  // 默认构造即加锁
    // 临界区...
    // 离开作用域自动解锁
}
```text

用法和`lock_guard`一样简单，但它提供了更多选项。

### 延迟加锁：defer_lock

```cpp
void selective_locking(bool need_lock) {
    std::unique_lock<std::mutex> lock(mtx, std::defer_lock);  // 构造时不加锁

    if (need_lock) {
        lock.lock();  // 需要时才加锁
    }

    // 临界区...

    // 如果加锁了，离开作用域会自动解锁；没加锁也不会报错
}
```text

`defer_lock`让你在构造时不加锁，后续根据需要手动加锁。这在某些条件性加锁的场景很有用。

### 手动解锁：减少锁持有时间

```cpp
std::mutex mtx;
std::vector<int> data;

void process_and_save() {
    std::unique_lock<std::mutex> lock(mtx);

    // 加锁处理数据
    int result = expensive_computation(data);

    lock.unlock();  // 提前解锁

    // 不需要锁的操作：保存到文件、网络传输等
    save_to_file(result);

    // lock离开作用域，但已经unlock过了，不会重复unlock
}
```text

这是一个重要的性能优化技巧：尽快释放锁，让其他线程能尽早进入临界区。`lock_guard`做不到这一点，但`unique_lock`可以。

### 条件变量：必须用unique_lock

条件变量（`std::condition_variable`）的`wait()`方法要求传入`unique_lock`，这是因为它需要在等待时自动解锁、被唤醒时重新加锁：

```cpp
#include <condition_variable>
#include <mutex>
#include <queue>

template<typename T>
class ThreadSafeQueue {
public:
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(mtx);
        queue.push(value);
        cv.notify_one();  // 通知一个等待的线程
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mtx);  // 必须用unique_lock

        // wait会在条件不满足时解锁，被唤醒时重新加锁
        cv.wait(lock, [this] { return !queue.empty(); });

        T value = queue.front();
        queue.pop();
        return value;

        // lock离开作用域自动解锁
    }

private:
    std::queue<T> queue;
    std::mutex mtx;
    std::condition_variable cv;
};
```text

这是`unique_lock`不可替代的场景。如果你要用条件变量，就必须用`unique_lock`。

### 锁的所有权转移

`unique_lock`支持移动语义，可以转移锁的所有权：

```cpp
std::unique_lock<std::mutex> acquire_lock() {
    std::unique_lock<std::mutex> lock(mtx);
    // 做一些初始化工作...
    return lock;  // 移动返回锁的所有权
}

void use_lock() {
    std::unique_lock<std::mutex> lock = acquire_lock();  // 接收锁的所有权
    // 临界区...
    // lock离开作用域，自动解锁
}
```text

这个特性在某些复杂场景很有用，比如把锁传递给另一个函数或对象。但要注意：`lock_guard`不支持这种用法。

### 性能考虑

`unique_lock`比`lock_guard`稍微重一些，因为它需要维护额外的状态（是否持有锁、是否用了`defer_lock`等）。但差异通常很小，除非是极端性能敏感的场景。

**选择建议**：

- 90%的场景：优先用`lock_guard`，简单轻量
- 需要条件变量：必须用`unique_lock`
- 需要手动解锁/延迟加锁：用`unique_lock`
- 性能测试证明有差异：再考虑优化

------

## scoped_lock：C++17的多锁死锁预防

C++17引入了`std::scoped_lock`，专门用于同时锁定多个互斥量时避免死锁。

### 死锁问题：经典的多锁场景

```cpp
std::mutex mtx_a, mtx_b;

// 线程1：先锁A再锁B
void thread1() {
    std::lock_guard<std::mutex> lock_a(mtx_a);
    std::lock_guard<std::mutex> lock_b(mtx_b);
    // ...
}

// 线程2：先锁B再锁A
void thread2() {
    std::lock_guard<std::mutex> lock_b(mtx_b);
    std::lock_guard<std::mutex> lock_a(mtx_a);
    // ...
}
```text

如果这两个线程同时运行，可能出现：

- 线程1锁住了A，等待B
- 线程2锁住了B，等待A
- 两个线程互相等待，死锁

### 传统解决方案：std::lock

C++11提供了`std::lock()`函数，可以一次性锁定多个互斥量，内部使用死锁避免算法：

```cpp
void safe_function() {
    std::unique_lock<std::mutex> lock_a(mtx_a, std::defer_lock);
    std::unique_lock<std::mutex> lock_b(mtx_b, std::defer_lock);

    std::lock(lock_a, lock_b);  // 同时锁定，避免死锁

    // 临界区...
}
```text

`std::lock()`会保证要么全部锁成功，要么全部不锁，避免了死锁。

### C++17更简洁的方案：scoped_lock

`std::scoped_lock`把上面的模式简化为一个构造：

```cpp
void modern_safe_function() {
    std::scoped_lock lock(mtx_a, mtx_b);  // 一次性锁定多个互斥量
    // 临界区...
}
```text

`scoped_lock`的构造函数会调用`std::lock()`来锁定所有互斥量，析构时按相反顺序解锁。既简洁又安全。

### 嵌入式场景示例：跨模块数据同步

```cpp
class SystemState {
public:
    void update_sensor_data(const SensorData& data) {
        std::scoped_lock lock(sensor_mtx, state_mtx);
        // 同时访问两个互斥量保护的资源
        sensor_data = data;
        update_system_state();
    }

    SensorData get_sensor_data() const {
        std::lock_guard<std::mutex> lock(sensor_mtx);
        return sensor_data;
    }

    SystemStatus get_status() const {
        std::lock_guard<std::mutex> lock(state_mtx);
        return status;
    }

private:
    std::mutex sensor_mtx;
    std::mutex state_mtx;

    SensorData sensor_data;
    SystemStatus status;

    void update_system_state();
};
```text

这里`update_sensor_data()`需要同时访问两个互斥量，用`scoped_lock`可以安全地锁定它们，避免死锁。

### scoped_lock的单锁情况

`scoped_lock`也可以用于单个互斥量，这时它等价于`lock_guard`：

```cpp
void simple_function() {
    std::scoped_lock lock(mtx);  // 等价于 std::lock_guard<std::mutex> lock(mtx);
    // 临界区...
}
```text

但为了代码清晰，单个锁还是推荐用`lock_guard`，一眼就能看出意图。

------

## 嵌入式特殊考量

嵌入式环境使用互斥锁有一些特殊的注意事项，我们来看看。

### 中断服务程序（ISR）里不能用锁

这是个硬性规则：ISR里不能阻塞，而互斥锁的`lock()`会阻塞等待。

```cpp
std::mutex mtx;
volatile int flag = 0;

// ❌ 错误：ISR里不能锁
extern "C" void EXTI0_IRQHandler() {
    std::lock_guard<std::mutex> lock(mtx);  // 危险！可能死锁
    flag = 1;
}

// ✅ 正确：用原子变量
std::atomic<int> atomic_flag{0};

extern "C" void EXTI0_IRQHandler() {
    atomic_flag.store(1, std::memory_order_release);
}
```text

**原则**：中断和主线程之间用原子操作或无锁队列，不要用互斥锁。

### mutex的大小和开销

`std::mutex`通常占用一定大小的内存（常见是40-48字节），对于资源紧张的嵌入式系统可能需要考虑：

```cpp
// 检查mutex的大小
static_assert(sizeof(std::mutex) <= 48, "mutex too large!");

// 如果需要节省空间，可以考虑用一个mutex保护多个相关变量
class CompactProtection {
    std::mutex mtx;  // 只用一把锁
    int value1;
    int value2;
    int value3;
};
```text

### 检查是否支持lock-free

某些平台上的`std::mutex`可能不是完全无锁的（内部用`pthread_mutex`之类），但这通常不是问题——mutex本身的设计就是为了阻塞。更重要的是检查你的原子操作是否lock-free：

```cpp
std::atomic<int> flag;
static_assert(flag.is_always_lock_free, "Atomic must be lock-free!");
```text

### RTOS的互斥量

如果你使用RTOS（如FreeRTOS、RT-Thread），可能需要考虑RTOS原生的互斥量与C++标准库的配合。大部分现代RTOS都提供了与C++标准库兼容的实现，但要注意：

```cpp
// 某些RTOS可能需要特殊配置
// 或者优先使用RTOS提供的互斥量API

// FreeRTOS示例
SemaphoreHandle_t xMutex = xSemaphoreCreateMutex();

void task() {
    if (xSemaphoreTake(xMutex, portMAX_DELAY) == pdTRUE) {
        // 临界区
        xSemaphoreGive(xMutex);
    }
}
```text

具体要参考你使用的RTOS文档。

------

## 死锁预防：最佳实践

死锁是多线程编程里最让人头疼的问题之一，但遵循一些最佳实践可以大幅降低风险。

### 规则1：固定加锁顺序

如果必须锁定多个互斥量，始终按照固定的顺序加锁：

```cpp
class Account {
public:
    static void transfer(Account& from, Account& to, int amount) {
        // 始终按照地址大小排序，确保顺序一致
        if (&from < &to) {
            from.lock();
            to.lock();
        } else {
            to.lock();
            from.lock();
        }

        from.balance -= amount;
        to.balance += amount;

        from.unlock();
        to.unlock();
    }

private:
    std::mutex mtx;
    int balance;
    void lock() { mtx.lock(); }
    void unlock() { mtx.unlock(); }
};
```text

但更好的做法是用`std::scoped_lock`，它会自动处理顺序问题。

### 规则2：尽快释放锁

锁的持有时间越短，死锁概率越低，性能也越好：

```cpp
// ❌ 不好：持有锁时做耗时操作
void bad_function() {
    std::lock_guard<std::mutex> lock(mtx);
    data = read_from_network();  // 可能很慢！
    process_data(data);
}

// ✅ 好：只锁必要的部分
void good_function() {
    auto temp_data = read_from_network();  // 不需要锁

    std::lock_guard<std::mutex> lock(mtx);
    data = temp_data;  // 只锁共享数据的操作
    // process_data()可以在锁外进行
}
```text

### 规则3：避免在持锁时调用外部代码

```cpp
// ❌ 危险：不知道回调函数会做什么
void dangerous_function() {
    std::lock_guard<std::mutex> lock(mtx);
    user_callback();  // 可能也会尝试加锁！
}

// ✅ 安全：让调用者在加锁前调用回调
void safe_function() {
    user_callback();  // 先调用回调
    std::lock_guard<std::mutex> lock(mtx);
    // 临界区操作
}
```text

### 规则4：使用try_lock避免无限等待

```cpp
std::mutex mtx1, mtx2;

void function_with_timeout() {
    std::unique_lock<std::mutex> lock1(mtx1, std::defer_lock);
    std::unique_lock<std::mutex> lock2(mtx2, std::defer_lock);

    if (std::try_lock(lock1, lock2) == -1) {  // -1表示全部成功
        // 成功获取所有锁
        // 临界区...
    } else {
        // 获取锁失败，处理错误
        handle_deadlock_risk();
    }
}
```text

`std::try_lock()`会尝试锁定所有互斥量，失败时返回失败的索引（从0开始），-1表示全部成功。

### 规则5：使用超时锁（C++14+）

```cpp
#include <mutex>
#include <chrono>

std::timed_mutex tmtx;

void function_with_timeout() {
    if (tmtx.try_lock_for(std::chrono::milliseconds(100))) {
        std::lock_guard<std::timed_mutex> lock(tmtx, std::adopt_lock);
        // 临界区...
    } else {
        // 超时，处理错误
        handle_timeout();
    }
}
```text

`std::timed_mutex`和`std::recursive_timed_mutex`支持带超时的加锁操作，可以避免无限等待。

------

## 读写锁：shared_mutex

如果你的数据结构读多写少，用普通的互斥锁会浪费性能——多个读线程其实可以并发访问。C++17引入了`std::shared_mutex`解决这个问题。

```cpp
#include <shared_mutex>

class ThreadSafeMap {
public:
    void insert(const std::string& key, int value) {
        std::unique_lock<std::shared_mutex> lock(mtx);  // 写锁：独占
        map[key] = value;
    }

    int lookup(const std::string& key) const {
        std::shared_lock<std::shared_mutex> lock(mtx);  // 读锁：共享
        auto it = map.find(key);
        return (it != map.end()) ? it->second : 0;
    }

private:
    mutable std::shared_mutex mtx;
    std::map<std::string, int> map;
};
```text

**特性**：

- `unique_lock`：写锁，独占访问
- `shared_lock`：读锁，多个线程可以同时持有
- 写操作会阻塞所有读写，读操作只阻塞写操作

**性能**：在读者很多、写者很少的场景，`shared_mutex`比普通mutex性能好很多。但要注意它的开销比普通mutex大，不要盲目使用。

**嵌入式注意**：某些嵌入式平台的`shared_mutex`可能不支持或者实现很重，使用前检查文档和性能。

------

## 小结

我们讲了从`std::mutex`基础到各种RAII封装的完整图景，让我们回顾一下：

1. **不要手动lock/unlock**：用RAII封装避免错误
2. **lock_guard**：简单场景的首选，90%情况用它就够了
3. **unique_lock**：复杂场景需要（条件变量、手动解锁等）
4. **scoped_lock**：多锁场景防死锁，C++17起推荐使用
5. **shared_mutex**：读多写少场景的优化
6. **嵌入式注意**：ISR里不能用锁，用原子操作或无锁队列

**实践建议**：

- 优先用`lock_guard`，简单可靠
- 需要条件变量时必须用`unique_lock`
- 多锁场景用`scoped_lock`避免死锁
