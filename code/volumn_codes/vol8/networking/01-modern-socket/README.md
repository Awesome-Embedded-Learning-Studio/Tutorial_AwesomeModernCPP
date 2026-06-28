# 01 · 现代 socket 封装(RAII + std::expected + 每连接一线程)

配套文章：`documents/vol8-domains/networking/01-modern-socket-wrapping.md`

把 00 的传统 C 风格 echo 现代化：`UniqueFd`(RAII，不可能漏 `close`)+ `std::expected`(错误带上下文)+ 每连接一线程(能扛并发客户端)。结尾实测"每连接一线程"在 2000 并发下把虚拟内存吃到 ~24GB，引出 epoll。

## 编译

```bash
cd code/volumn_codes/vol8/networking/01-modern-socket
cmake -S . -B build && cmake --build build
# 产物：build/echo_server  build/echo_client  build/hold_clients
```

（需要 C++23：GCC 14+ / Clang 17+ / MSVC 19.34+。本机 GCC 16.1.1。）

## 运行

```bash
./build/echo_server        # 终端 1
./build/echo_client "hi"   # 终端 2 → echo <- 'hi'
```

## C10K 实测

趁 server 跑着，开 2000 条空闲连接，看 server 的虚拟内存/线程数怎么涨：

```bash
# 终端 1
./build/echo_server &
SRV=$!
grep -E '^(VmSize|VmRSS|Threads):' /proc/$SRV/status     # idle 基线

# 终端 2：开 2000 空闲连接并 hold
./build/hold_clients 2000 &
sleep 3
grep -E '^(VmSize|VmRSS|Threads):' /proc/$SRV/status     # 2000 连接下

# 释放后
wait
grep -E '^(VmSize|VmRSS|Threads):' /proc/$SRV/status
```

你会看到 `VmSize` 从 ~83MB 飙到 ~24GB、`Threads` 涨到 ~2001——每连接一个线程，每个线程默认 8MB 栈。这就是"每连接一线程"扛不住 C10K 的实证，也是下一篇 epoll 的动机。
