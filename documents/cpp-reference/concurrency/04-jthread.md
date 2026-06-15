---
chapter: 99
cpp_standard:
- 20
- 23
description: 自动 join 的线程类，析构时发送停止请求并等待线程退出
difficulty: beginner
order: 4
reading_time_minutes: 2
tags:
- host
- cpp-modern
- beginner
title: std::jthread
---
<!--
参考卡模板 (Reference Card Template)
用于 documents/cpp-reference/ 下的特性速查页。
与 article-template.md 不同，参考卡走精炼的结构化格式，不需要叙事风格。

标签使用规则：
1. 必须包含 1 个 platform 标签（参考卡统一用 host）
2. 必须包含 1 个 difficulty 标签
3. 至少包含 1 个 topic 标签
4. 从 scripts/validate_frontmatter.py 的 VALID_TAGS 集合中选取
-->

# std::jthread（C++20）

## 一句话

自带 RAII 语义的线程类——析构时自动发送停止请求并 join，彻底消灭忘记 join 导致的 crash。

## 头文件

`#include <thread>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 构造（带函数） | `template<class F> jthread(F&& f, Args&&... args)` | 启动新线程执行 f(args...) |
| 构造（带 stop_token） | `template<class F> jthread(F&& f)` | f 的首参接收 `std::stop_token` |
| 析构 | `~jthread()` | 请求停止 + join（若 joinable） |
| 请求停止 | `bool request_stop() noexcept` | 请求协作式停止，返回是否成功 |
| 获取停止令牌 | `std::stop_token get_stop_token() const noexcept` | 获取当前线程的停止令牌 |
| 等待完成 | `void join()` | 阻塞等待线程结束 |
| 分离线程 | `void detach()` | 分离，线程独立运行 |
| 是否可 join | `bool joinable() const noexcept` | 检查线程是否可 join |
| 获取 ID | `std::thread::id get_id() const noexcept` | 返回线程标识 |

## 最小示例

```cpp
// Standard: C++20
#include <iostream>
#include <thread>

void worker(std::stop_token st) {
    while (!st.stop_requested()) {
        std::cout << "working...\n";
    }
    std::cout << "stopped\n";
}

int main() {
    std::jthread t(worker); // 自动传入 stop_token
    // t 析构时自动 request_stop() + join()
} // 输出: working... stopped
```

## 嵌入式适用性：中

- RAII 自动 join 消除忘记 join 的隐患，提升代码健壮性
- `std::stop_token` 协作式取消机制比手工标志变量更规范
- 依赖操作系统线程支持，裸机 RTOS 场景需配合线程抽象层使用
- 需要 C++20 标准库支持，GCC 10+ 即可用，但 Clang/libc++ 支持较晚（17+）

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 10 | 17 | 19.28 |

## 另见

- [cppreference: std::jthread](https://en.cppreference.com/w/cpp/thread/jthread)
- [cppreference: std::stop_token](https://en.cppreference.com/w/cpp/thread/stop_token)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
