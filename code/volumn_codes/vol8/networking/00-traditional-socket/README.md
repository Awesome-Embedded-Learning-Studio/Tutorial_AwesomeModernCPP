# 00 · 传统 socket echo(server + client)

配套文章：`documents/vol8-domains/networking/00-traditional-socket-basics.md`

单线程、C 风格 BSD socket echo，演示服务器五步：`socket → bind → listen → accept → read/write`。不掺并发（并发与 C10K 在 01）。

## 编译

**方式 A：一行 gcc（最直接，文章里就是这套）**

```bash
gcc -O2 -Wall -Wextra classic_server.c -o echo_server
gcc -O2 -Wall -Wextra classic_client.c -o echo_client
```

**方式 B：CMake（项目统一构建）**

```bash
cmake -S . -B build && cmake --build build
# 产物：build/echo_server  build/echo_client
```

## 运行与核查

终端 1 起服务（端口 13013 > 1024，无需 sudo）：

```bash
./echo_server
# classic echo server on 0.0.0.0:13013 (pid ...)
```

终端 2 跑客户端：

```bash
./echo_client "hello"
# echo <- 'hello'
```

**核查监听 socket**——用 `ss` 把内核里的 LISTEN socket 拍出来。第二列 `64` 就是 `listen(lfd, 64)` 设的 Accept 队列上限（backlog），`fd=3` 是 stdin/stdout/stderr 之后的第一个 fd：

```bash
ss -tlnp | grep 13013
# LISTEN 0  64  0.0.0.0:13013  0.0.0.0:*  users:(("echo_server",pid=...,fd=3))
```

第一列 `0` 是当前 Accept 队列里积压的连接数（刚被取走，所以是 0）。开第二个客户端连上但不发数据，这列会涨——可以亲手验证 backlog 在管"已完成握手但还没 accept 的连接"。
