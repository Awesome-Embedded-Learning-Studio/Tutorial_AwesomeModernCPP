# Modern C++ for Embedded Development—mutex and RAII Guards

## Introduction

In the previous chapter, we covered lock-free data structures—sounds pretty cool, right? But honestly, 90% of concurrent scenarios don't require anything that complex. A good mutex, combined with the correct usage patterns, solves most problems.

The catch is that misusing locks comes at a high cost: forgetting to unlock causes a dead lock, an exception escaping causes a dead lock, and acquiring multiple locks in the wrong order also causes a dead lock. In complex code, the traditional C-style ``lock()``/``unlock()`` approach makes it almost impossible to guarantee correctness.

C++ brings us the "magic" of RAII (Resource Acquisition Is Initialization). By binding a lock's lifetime to a scope—locking on construction and unlocking on destruction—we guarantee correct release no matter how we exit (normal return, thrown exception, or early exit). This isn't just syntactic sugar; it fundamentally eliminates an entire class of bugs.

In this chapter, we will dive deep into the various wrappers around ``std::mutex``: ``lock_guard``, ``unique_lock``, and ``scoped_lock``, along with their best practices in embedded scenarios.

> In a nutshell: **Never manually call ``lock()`` and ``unlock()``; use RAII lock guards and let the compiler do the right thing for you.**

------

## std::mutex Basics: Don't Use It Manually

``std::mutex`` is the most basic mutex provided by the C++ standard library, exposing three operations: ``lock()``, ``unlock()``, and ``try_lock()``.

### Looks Simple, Easy to Get Wrong

````cpp
#include <mutex>

std::mutex mtx;
int shared_counter = 0;

void bad_increment() {
    mtx.lock();          // 手动加锁
    shared_counter++;
    // 如果这里抛异常...unlock永远不会执行！
    mtx.unlock();        // 手动解锁
}

````

This code has a few fatal flaws:

1. If ``shared_counter++`` throws an exception (an `int` won't, but a complex type might), ``unlock()`` will never execute.
2. If there are multiple return paths in the middle, it is easy to forget to unlock before a specific return.
3. If the code logic is complex, it is difficult to see the lock's lifetime.

### An Even Worse Scenario: Nested Calls

````cpp
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

````

In this case, the program freezes completely. While this example seems silly, in large codebases with complex call chains, it is easy to make this mistake without even realizing it.

### Conclusion: Don't Use Raw Locks

``std::mutex`` was never designed for you to call ``lock()``/``unlock()`` directly. Instead, it serves as the underlying implementation for various RAII wrappers. The ``lock_guard`` and ``unique_lock`` we are about to discuss are the tools you should be using every day.

------

## lock_guard: The Go-To for Simple Scenarios

``std::lock_guard`` is the simplest RAII lock wrapper introduced in C++11, and it is the tool you should reach for in 90% of scenarios.

### Basic Usage

````cpp
#include <mutex>

std::mutex mtx;
int shared_counter = 0;

void good_increment() {
    std::lock_guard<std::mutex> lock(mtx);  // 构造时自动lock
    shared_counter++;
    // 无论这里怎么退出（return、异常），lock析构时都会自动unlock
}

````

It is just that simple. The constructor of ``lock_guard`` calls ``mtx.lock()``, and its destructor calls ``mtx.unlock()``. C++ guarantees that a local object's destructor will be called when it leaves scope, even if an exception occurs.

### Key Characteristics

The design philosophy of ``lock_guard`` is "simplicity is beauty":

1. **Non-copyable**: Copy constructor and copy assignment are deleted.
2. **Non-movable**: Not supported before C++17; supported in C++17 but rarely used.
3. **No manual unlocking**: There is no ``unlock()`` method; you must wait for destruction.
4. **Locks on construction**: You cannot defer locking; the mutex must be held to construct the guard.

These restrictions ensure that the usage pattern is clear, simple, and error-free.

### Embedded Scenario Example: Protecting Shared Peripheral State

````cpp
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

````

Using ``lock_guard`` here provides several benefits:

- Exception safety: If ``hardware_transfer()`` throws an exception, the lock is still released.
- Clear code: You can see the critical section's scope at a glance.
- Predictable performance: The critical section is the entire function body—simple and straightforward.

### Common Mistake: Forgetting to Name the Variable

````cpp
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

````

This is a frequent mistake for beginners. Compilers usually will not warn about it, so you must be careful.

### Constructor Option: adopt_lock

``lock_guard`` has another rarely used but sometimes helpful constructor option:

````cpp
void complex_function() {
    mtx.lock();  // 手动加锁（某些特殊情况）

    // 用adopt_lock告诉lock_guard：锁已经持有了，不要重复lock
    std::lock_guard<std::mutex> lock(mtx, std::adopt_lock);

    // 临界区代码...

    // 离开作用域时，lock仍然会unlock
}

````

This option is primarily used when migrating manually locked code to an RAII style, or in special scenarios where you need precise control over the locking timing. However, in most cases, you should let ``lock_guard`` manage the locking itself.

------

## unique_lock: The Swiss Army Knife for Complex Scenarios

``std::unique_lock`` is a more powerful and flexible RAII lock wrapper. It provides several key capabilities that ``lock_guard`` lacks: deferred locking, manual unlocking, condition variable support, and lock ownership transfer.

### Basic Usage

````cpp
std::mutex mtx;

void function_with_unique_lock() {
    std::unique_lock<std::mutex> lock(mtx);  // 默认构造即加锁
    // 临界区...
    // 离开作用域自动解锁
}

````

The usage is as simple as ``lock_guard``, but it offers more options.

### Deferred Locking: defer_lock

````cpp
void selective_locking(bool need_lock) {
    std::unique_lock<std::mutex> lock(mtx, std::defer_lock);  // 构造时不加锁

    if (need_lock) {
        lock.lock();  // 需要时才加锁
    }

    // 临界区...

    // 如果加锁了，离开作用域会自动解锁；没加锁也不会报错
}

````

``defer_lock`` allows you to construct the guard without locking, manually locking later as needed. This is useful in scenarios where locking is conditional.

### Manual Unlocking: Reducing Lock Hold Time

````cpp
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

````

This is an important performance optimization technique: release the lock as soon as possible so other threads can enter the critical section sooner. ``lock_guard`` cannot do this, but ``unique_lock`` can.

### Condition Variables: unique_lock is Mandatory

The ``wait()`` method of a condition variable (``std::condition_variable``) requires a ``unique_lock`` because it needs to automatically unlock while waiting and re-lock upon being woken up:

````cpp
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

````

This is a scenario where ``unique_lock`` cannot be substituted. If you use a condition variable, you must use ``unique_lock``.

### Lock Ownership Transfer

``unique_lock`` supports move semantics, allowing the transfer of lock ownership:

````cpp
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

````

This feature is useful in certain complex scenarios, such as passing a lock to another function or object. But note: ``lock_guard`` does not support this usage.

### Performance Considerations

``unique_lock`` is slightly heavier than ``lock_guard`` because it needs to maintain additional state (whether the lock is held, whether ``defer_lock`` was used, etc.). However, the difference is usually negligible unless you are in an extremely performance-sensitive scenario.

**Selection Guidelines**:

- 90% of scenarios: Prefer ``lock_guard`` for its simplicity and lightweight nature.
- Need a condition variable: Must use ``unique_lock``.
- Need manual unlocking/deferred locking: Use ``unique_lock``.
- Performance testing proves there is a difference: Only then consider optimizing.

------

## scoped_lock: C++17's Multi-Lock Deadlock Prevention

C++17 introduced ``std::scoped_lock``, specifically designed to avoid dead locks when locking multiple mutexes simultaneously.

### The Deadlock Problem: A Classic Multi-Lock Scenario

````cpp
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

````

If these two threads run concurrently, the following can happen:

- Thread 1 locks A and waits for B.
- Thread 2 locks B and waits for A.
- Both threads wait on each other—a dead lock.

### The Traditional Solution: std::lock

C++11 provides the ``std::lock()`` function, which can lock multiple mutexes at once using an internal dead lock avoidance algorithm:

````cpp
void safe_function() {
    std::unique_lock<std::mutex> lock_a(mtx_a, std::defer_lock);
    std::unique_lock<std::mutex> lock_b(mtx_b, std::defer_lock);

    std::lock(lock_a, lock_b);  // 同时锁定，避免死锁

    // 临界区...
}

````

``std::lock()`` guarantees that either all mutexes are locked successfully, or none are locked, thereby avoiding dead locks.

### The Cleaner C++17 Solution: scoped_lock

``std::scoped_lock`` simplifies the above pattern into a single construction:

````cpp
void modern_safe_function() {
    std::scoped_lock lock(mtx_a, mtx_b);  // 一次性锁定多个互斥量
    // 临界区...
}

````

The constructor of ``scoped_lock`` calls ``std::lock()`` to lock all mutexes, and unlocks them in reverse order upon destruction. It is both concise and safe.

### Embedded Scenario Example: Cross-Module Data Synchronization

````cpp
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

````

Here, ``update_sensor_data()`` needs to access two mutexes simultaneously. Using ``scoped_lock`` locks them safely and avoids dead locks.

### scoped_lock with a Single Lock

``scoped_lock`` can also be used with a single mutex, in which case it is equivalent to ``lock_guard``:

````cpp
void simple_function() {
    std::scoped_lock lock(mtx);  // 等价于 std::lock_guard<std::mutex> lock(mtx);
    // 临界区...
}

````

For code clarity, however, we still recommend using ``lock_guard`` for a single lock so the intent is obvious at a glance.

------

## Special Embedded Considerations

Using mutexes in embedded environments comes with a few special caveats. Let's take a look.

### No Locks in Interrupt Service Routines (ISRs)

This is a hard rule: ISRs must not block, and a mutex's ``lock()`` blocks while waiting.

````cpp
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

````

**Principle**: Use atomic operations or lock-free queues between interrupts and the main thread; do not use mutexes.

### Mutex Size and Overhead

A ``std::mutex`` typically consumes a certain amount of memory (commonly 40–48 bytes), which might be a concern for resource-constrained embedded systems:

````cpp
// 检查mutex的大小
static_assert(sizeof(std::mutex) <= 48, "mutex too large!");

// 如果需要节省空间，可以考虑用一个mutex保护多个相关变量
class CompactProtection {
    std::mutex mtx;  // 只用一把锁
    int value1;
    int value2;
    int value3;
};

````

### Checking for Lock-Free Support

On certain platforms, ``std::mutex`` might not be entirely lock-free (it might use ``pthread_mutex`` internally), but this is usually not an issue—mutexes are designed to block by nature. What is more important is checking whether your atomic operations are lock-free:

````cpp
std::atomic<int> flag;
static_assert(flag.is_always_lock_free, "Atomic must be lock-free!");

````

### RTOS Mutexes

If you are using an RTOS (such as FreeRTOS or RT-Thread), you might need to consider how the RTOS's native mutexes integrate with the C++ standard library. Most modern RTOSes provide implementations compatible with the C++ standard library, but keep the following in mind:

````cpp
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

````

Refer to your specific RTOS documentation for details.

------

## Deadlock Prevention: Best Practices

Dead locks are one of the most frustrating problems in multithreaded programming, but following a few best practices can drastically reduce the risk.

### Rule 1: Maintain a Fixed Locking Order

If you must lock multiple mutexes, always acquire them in a fixed order:

````cpp
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

````

An even better approach is to use ``std::scoped_lock``, which handles the ordering automatically.

### Rule 2: Release Locks as Soon as Possible

The shorter the lock hold time, the lower the probability of a dead lock, and the better the performance:

````cpp
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

````

### Rule 3: Avoid Calling External Code While Holding a Lock

````cpp
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

````

### Rule 4: Use try_lock to Avoid Waiting Indefinitely

````cpp
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

````

``std::try_lock()`` attempts to lock all mutexes, returning the index of the first failure (starting from 0) on failure, or -1 if all were successfully locked.

### Rule 5: Use Timed Locks (C++14+)

````cpp
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

````

``std::timed_mutex`` and ``std::recursive_timed_mutex`` support locking operations with timeouts, which can prevent indefinite waiting.

------

## Read-Write Locks: shared_mutex

If your data structure is read-heavy and write-light, using a standard mutex wastes performance—multiple reader threads could actually access it concurrently. C++17 introduced ``std::shared_mutex`` to solve this problem.

````cpp
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

````

**Characteristics**:

- ``unique_lock``: Write lock, exclusive access.
- ``shared_lock``: Read lock, multiple threads can hold it simultaneously.
- Write operations block all reads and writes, while read operations only block writes.

**Performance**: In scenarios with many readers and few writers, ``shared_mutex`` performs significantly better than a standard mutex. However, be aware that its overhead is larger than a standard mutex, so do not use it blindly.

**Embedded Note**: ``shared_mutex`` on certain embedded platforms might be unsupported or have a heavy implementation. Always check the documentation and benchmark performance before using it.

------

## Summary

We have covered the complete picture from ``std::mutex`` basics to various RAII wrappers. Let's recap:

1. **Don't manually lock/unlock**: Use RAII wrappers to avoid errors.
2. **lock_guard**: The go-to for simple scenarios; sufficient 90% of the time.
3. **unique_lock**: Needed for complex scenarios (condition variables, manual unlocking, etc.).
4. **scoped_lock**: Prevents dead locks in multi-lock scenarios; recommended from C++17 onward.
5. **shared_mutex**: An optimization for read-heavy, write-light scenarios.
6. **Embedded notes**: Do not use locks in ISRs; use atomic operations or lock-free queues instead.

**Practical Recommendations**:

- Prefer ``lock_guard`` for its simplicity and reliability.
- You must use ``unique_lock`` when working with condition variables.
- Use ``scoped_lock`` in multi-lock scenarios to avoid dead locks.
