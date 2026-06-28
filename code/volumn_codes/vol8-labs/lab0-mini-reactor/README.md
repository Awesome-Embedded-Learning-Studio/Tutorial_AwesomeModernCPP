# Lab 0 · mini Reactor echo server

配套文章：`documents/vol8-domains/networking/lab0-mini-reactor.md`

在 epoll 上实现一个最小的 Reactor（事件循环 + handler 注册表），做成能扛并发的 echo server。MS1-4 四个 milestone，每个的验收都是**对抗性的**（并发不崩、ET 大 burst 不丢数据、stop 不挂、TSan 无 race）。

## 怎么做这个 Lab

1. 读 `include/net/reactor.hpp`（接口）。
2. 把 `src/reactor.cpp` 清空（现在是**参考答案**，留着对照思路），对照接口自己写一遍 `Reactor`。
3. 构建 + 跑测试，让 `tests/lab0_tests.cpp` 的 4 个用例（MS1-4）逐个变绿。

## 构建

```bash
cd code/volumn_codes/vol8-labs/lab0-mini-reactor
cmake -S . -B build
cmake --build build -j
```

首次配置会从 github 拉 Catch2（FetchContent）。**若网络受限拉不下来**，手动浅克隆一份、用 `-DFETCHCONTENT_SOURCE_DIR_CATCH2` 指过去：

```bash
git clone --depth 1 -b v3.5.0 https://github.com/catchorg/Catch2.git /tmp/catch2
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_CATCH2=/tmp/catch2
cmake --build build -j
```

## 跑测试

```bash
./build/lab0_tests         # 普通测试:MS1-4
./build/lab0_tests_tsan    # TSan 版:同样的用例,编译期开 -fsanitize=thread
```

预期：`All tests passed (6 assertions in 4 test cases)`，且 TSan 版**无任何 warning**。

## MS1-4 验收速查

| MS | 验收（对抗性） | 抓的坑 |
|----|------|------|
| MS1 | 单连接 + 顺序多连接 echo 全对 | 事件循环转起来 |
| MS2 | 16 并发 echo 全对，**TSan 无 race** | handler 注册表 / 隐藏 data race |
| MS3 | ET + 100KB burst，echo **正好 100000** 字节 | ET 不循环读到 EAGAIN → 丢数据（[02 篇](../../../../documents/vol8-domains/networking/02-epoll-io-multiplexing.md)那个坑） |
| MS4 | `stop()` 后 `run()` 2s 内返回 | epoll_wait 阻塞、关闭挂死 |

参考实现（`src/reactor.cpp`）一个值得注意的点：`run()` 调 handler 前先 `Handler h = it->second;` 拷一份——因为 handler 在 EOF 时会 `remove` 自己，直接调 map 里那个正在执行的 `std::function` 是 use-after-free（TSan 会抓）。这是 reactor 的经典自删除坑。
