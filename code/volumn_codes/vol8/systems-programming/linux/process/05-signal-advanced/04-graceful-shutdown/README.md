# E4 优雅关闭:signalfd 事件循环版 vs handler 设 flag 朴素版

## graceful_signalfd.cpp(招牌整合)

真的 mini prefork echo 服务器:父进程 TCP listen,poll {listen, signalfd, worker 通道};accept 到的连接经 **SCM_RIGHTS** 派发给 3 个 worker(轮询);worker 读请求、干 250ms 活、回一行。SIGTERM/SIGINT 先阻塞再挂 signalfd(E2 的纪律),supervisor 子进程 t=300ms 发 SIGTERM,client 子进程 t=50/150 各下一单、t≈500 再试图连一条。

状态机时序(对照 graceful_signalfd.out):

```
t=  1 RUNNING  就绪
t= 52/152      两条连接进来,派给 w0/w1(250ms 在途)
t=301          signalfd 读到 SIGTERM —— 信号是事件循环里的一个事件,不是 handler
t=301 DRAINING close(listen_fd) 停止接新连接 → 向 3 个 worker 发 drain
t=301          w2 空手,立即退场(父进程 pidfd 可读,waitid(P_PIDFD) 收尸)
t=302          w0 恰好干完在途单,回复后随 drain 退场
t=402          w1 的在途单是 SIGTERM 之后才做完的 —— drain 语义:干完手头,不接新活
t=403 REAPING  3 个 worker 全部经 pidfd 收尸完毕 → EXIT
t=503          client 试图新建连接:Connection refused(门是真关了)
```

要点:信号进入事件循环后,关停就是一个普通的状态迁移——`g_state` 一个变量、poll 表跟着状态变(RUNNING 挂 listen,DRAINING 挂 worker pidfd),没有任何 handler 约束下的代码。

## naive_flag.cpp(对照组)

同一个骨架的朴素版:主循环阻塞在 `accept()`,SIGTERM handler 只做两件安全的事(`g_stop=1` + `write`)。剧本:两条连接(t=50/t=400),SIGTERM t=200。

- **面孔一(SA_RESTART)**:accept 被打断后内核自动重启,主循环没机会看旗——handler 在 t=200 就跑了,服务器却一路睡到 t=503 下一条连接来,把不该接的连接接了、干完、回完,才看见旗子退场。关停延迟 300ms + 误接一条连接。
- **面孔二(不开 SA_RESTART)**:accept 返回 -1/EINTR → 当场看旗退场(t≈704 信号到、同刻关门),t=956 的连接被拒。
- 但面孔二仍要 handler+flag+**每个阻塞点都查旗**三件套配对(accept/read/recv 各一处);signalfd 版把信号变成 poll 表里的一个 fd,这套配对全省。

## 开发中真实踩到的两个坑(本身就是教学点,记录在案)

1. **fork 出的子进程不关继承的 listen fd → 父进程 `close(listen)` 关不掉门**。listen socket 属于打开文件描述,worker/supervisor/client 都继承了引用;父进程 close 只是减引用,监听还活着,「关停后」的 connect 照样成功——然后没人 accept,客户端挂死在 read。修法:每个子进程进门先 `close(继承的 lfd)`(graceful_signalfd.cpp 里的 `g_lfd` 注释即此)。
2. **客户端写已断开的连接吃 SIGPIPE 静默死**:关停后 connect 失败,客户端照样 write → SIGPIPE 默认动作直接杀进程,后面的日志一行都没了。修法:client `signal(SIGPIPE, SIG_IGN)`(上篇 E7 讲过默认动作,这里是它在工程里咬人的实例)。

## 复现

```sh
g++ -std=c++20 -Wall -Wextra -O2 graceful_signalfd.cpp -o /tmp/e4a && /tmp/e4a
g++ -std=c++20 -Wall -Wextra -O2 naive_flag.cpp     -o /tmp/e4b && /tmp/e4b
```

各约 1s;端口每次随机(`bind :0`),时间戳数值会抖,时序结构与状态迁移是稳定结论。
