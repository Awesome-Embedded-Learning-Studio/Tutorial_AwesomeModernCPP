---
title: "std::thread"
description: "表示单个执行线程的类，允许并发执行多个函数"
chapter: 99
order: 2
tags:
  - host
  - mutex
  - beginner
difficulty: beginner
cpp_standard: [11, 14, 17, 20, 23]
---

# std::thread（C++11）

## 一句话

C++ 标准库提供的原生线程封装，创建对象即启动底层 OS 线程，用于实现真正的多任务并发。

## 头文件

`#include <thread>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 构造 | `thread() noexcept;` | 默认构造，不关联任何线程 |
| 构造 | `template< class Function, class... Args > explicit thread( Function&& f, Args&&... args );` | 构造并立即启动线程 |
| 析构 | `~thread();` | 销毁前必须已 join 或 detach，否则调用 std::terminate |
| 赋值 | `thread& operator=( thread&& other ) noexcept;` | 移动赋值 |
| 可等待 | `bool joinable() const noexcept;` | 检查线程是否可 join（即是否关联活跃线程） |
| 等待结束 | `void join();` | 阻塞当前线程，等待目标线程执行完毕 |
| 分离 | `void detach();` | 将线程与 thread 对象分离，使其在后台独立运行 |
| 获取 ID | `id get_id() const noexcept;` | 返回线程标识符 |
| 硬件并发数 | `static unsigned int hardware_concurrency() noexcept;` | 返回实现支持的并发线程数 |

## 最小示例

```cpp
#include <iostream>
#include <thread>

void task(int n) {
    for (int i = 0; i < n; ++i)
        std::cout << "worker: " << i << "\n";
}

int main() {
    std::thread t(task, 3);
    t.join(); // 阻塞等待线程 t 执行完毕
    std::cout << "done\n";
}
// Standard: C++11
```

## 嵌入式适用性：高

- 零抽象开销，`std::thread` 直接映射为底层 OS 线程（如 RTOS 的 Task 或 POSIX pthread）
- `hardware_concurrency()` 可用于在运行时探测可用核心数，动态决定线程池规模
- 配合 `std::mutex`、`std::atomic` 可安全保护共享外设寄存器或全局缓冲区
- 需注意 OS 线程栈开销（通常几 KB 到几十 KB），在极小内存 MCU 上需精确控制线程数量与栈大小

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 4.6 | 3.1 | 19.0 |

## 另见

- [cppreference: std::thread](https://en.cppreference.com/w/cpp/thread/thread)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
