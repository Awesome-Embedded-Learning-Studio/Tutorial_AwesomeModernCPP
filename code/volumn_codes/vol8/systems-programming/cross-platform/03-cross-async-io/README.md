# 03-cross-async-io 配套实验

《跨平台异步 I/O 抽象》(vol8 systems-programming ch04 第 03 篇,收章篇)的实验代码与原始输出存档。核心问题一句话:**四个平台后端(epoll/kqueue/IOCP/io_uring)差异摆开之后,C++ 的 concepts 能不能在编译期把"一个异步后端"约束出来,让同一份用户代码在两侧都跑通**。

目录命名说明:linux/ windows/ 两侧各按自己的章目录归档,本篇跨两侧,按任务约定立在 `cross-platform/` 下,目录名跟 todo 里 ch04 第 03 篇的槽位(`03-cross-async-io`)。若成文时文档 slug 变了,这里跟着改名即可,内容不依赖目录名。

与相邻篇的分工:
- 两侧五篇已实测的数字(01-select-poll-epoll 的扫描量与成本曲线、02-timerfd-eventfd 的醒法与精度、03-io-uring 的批量与超时、01-overlapped 的 64 墙、02-iocp 的完成序与并发值)是本篇差异矩阵的数据源,**引用不重测**。
- vol5 ch06 已讲事件循环架构与协程衔接,本篇不重开事件循环的课,concept 只收后端这一层。
- ch07 平台抽象章管"错误、句柄、缓冲"的统一封装,本篇只管异步 I/O 的后端抽象,ch07 收更宽的资源。

## 环境口径(两侧各自的编译运行环境)

| 侧 | 系统 | 编译器 | 命令 |
|---|---|---|---|
| Linux | WSL2 6.18.33.2-microsoft-standard-WSL2 | g++ (GCC) 16.2.1 | `g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2` |
| Windows | Win11 26200(WSL interop 调起) | MSYS2 UCRT64 g++ (Rev5) 16.1.0 | `/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra` |

**同一份 `e1_backend_concept.cpp` 两侧编译均零警告、退出码 0,输出只差 `backend=` 一列**(epoll/iocp),捕获日期 2026-10-04。

## 文件与结论对照

| 文件 | 实验 | 一句话结论 |
|---|---|---|
| `e1_backend_concept.cpp` + `e1_linux.out` / `e1_windows.out` | concept 后端骨架双侧编译 | 一个最小的完成式 concept(AsyncBackend:submit/wait/take 三件套,嵌套 completion/request 两个类型名),EpollBackend 与 IocpBackend 两个骨架都满足它,同一份受约束的泛型驱动 `drive_one_round` 两侧一字不改,各自跑通两轮真实读(Linux 管道 5+5 字节;Windows 带 FILE_FLAG_OVERLAPPED 的文件偏移 0 与 6 各读 5 字节),完成事件的 id/err/bytes/data 全部对上 |
| `e2_negative.cpp` + `e2_negative_linux.err` / `e2_negative_windows.err` | 负例:缺 take 的残缺后端 | `static_assert(!AsyncBackend<LazyBackend>)` 编过(concept 如实答"不满足");把 LazyBackend 递给受约束函数则编译失败,两侧 GCC 16 的诊断都精确指到 `the required expression 'b.take()' is invalid`,编译器原文逐字在档 |

## 设计取舍(写手可以直接展开的三点)

1. **concept 定成完成式(Proactor 形状),不定成就绪式**。epoll/kqueue 是就绪式,IOCP/io_uring 是完成式;完成式是四家里唯一的公共交集的形状——就绪式可以被适配成完成式(就绪到来时适配层亲手搬运),反向不行。这正是 libuv/Asio 的选型理由,本骨架把它做成了可编译的证据。
2. **EpollBackend 是适配层**:`submit` 武装 EPOLLIN\|EPOLLONESHOT(一次注册对齐一次完成,重投走 MOD),`wait` 在 epoll_wait 就绪后亲手 read,把"可读了"翻译成"读到了";`bytes` 是适配层自己搬出来的,不是内核给的。**IocpBackend 没有翻译**:submit 直接 ReadFile+OVERLAPPED,wait 就是 GQCS,`bytes` 内核填好。同一个 concept,一边要垫一层,一边直录——这层"垫"就是跨平台抽象的真实成本。
3. **身份与生命周期**:统一 completion 的 `id` 字段,epoll 侧是适配层自派编号,IOCP 侧藏在 OVERLAPPED 扩展结构里随完成包回来(句柄级的 completion key 分不出一发一发,得靠 pov);OVERLAPPED 的生命周期必须覆盖在途全程,取走完成之后才可回收——骨架里用 `std::list<ovx>` 保地址稳定。io_uring 若补第三个后端,`id` 就是 SQE 的 user_data 原样回来,`bytes` 就是 CQE 的 res。

## 过程里的一处返工(诚实记录)

后端名最初写成 `static constexpr std::string_view`,过 printf 的 `%s` 是未定义行为——Linux 侧当场段错误(退出码 139),编译期只有 -Wformat 警告。改成 `static constexpr char name[]` 后两侧干净。教训归一句话:**varargs 接口只认字符指针,string_view 的第一个成员"碰巧"是指针,碰巧不是保证**。

## kqueue 列的口径(本机无 BSD/macOS,全文档口径未实测)

差异矩阵的 kqueue 列来自 FreeBSD kqueue(2) 手册页(man.freebsd.org,FreeBSD 15.1-RELEASE 版本,查阅日 2026-10-04),要点:kevent 一次调用同时收 changelist 与 eventlist(注册与收割合一,man 原话 "All changes contained in the changelist are applied before any pending events are read from the queue");EV_CLEAR 是边沿式复位、默认是水平式,EV_ONESHOT 语义与 epoll 的 ONESHOT 同名同义,EV_DISPATCH 送出即禁用;EVFILT_READ 对管道在 data 里直接带回可读字节数、对 vnode(普通文件)也可挂(文件指针不在 EOF 即报,与 epoll 对普通文件 EPERM 拒收相反);EVFILT_TIMER 是过滤器不是 fd(timerfd 对照);EVFILT_AIO 走 POSIX AIO 的 sigevent 注册。**以上未在本机验证,成文时照此标注**。

## 复现

```bash
# Linux 侧
g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 e1_backend_concept.cpp -o e1 && ./e1
# Windows 侧(WSL interop,cwd 在 WSL 文件系统上)
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e1_backend_concept.cpp -o e1.exe && ./e1.exe
# 负例(预期编译失败,看诊断)
g++ -std=c++20 -Wall -Wextra -c e2_negative.cpp
```

Windows 侧可执行文件经 interop 生成的默认没有执行位,`chmod +x` 一下再跑。
