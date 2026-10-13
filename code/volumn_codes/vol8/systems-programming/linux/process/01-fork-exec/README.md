# 01-fork-exec 配套实验

《进程创建与生命周期:fork/exec/posix_spawn》(vol8 systems-programming/linux/process L01)的实验代码与原始输出存档。核心问题一句话:**fork 之后到底发生了什么,进程死的时候到底分几步**。

与前章的分工:L01(file-io 补课)讲过 dup/CLOEXEC 在 fork+exec 下的生死与 strace 工具链;本篇讲进程本身——fork 的语义账本、exec 家族、僵尸与收尸、孤儿与收养、posix_spawn、以及一个 RAII 的 child_process 封装。工具引用不重做。

## 环境口径(所有 .out 都来自这一轮)

| 项 | 值 |
|---|---|
| 机器 | 台机,AMD Ryzen 7 9700X 8-Core(16 线程) |
| 系统 | WSL2,内核 6.18.33.2-microsoft-standard-WSL2 |
| PID 1 | **systemd**(/sbin/init)——本机开了 systemd;但孤儿实测不归它收,见 e4 |
| 编译器 | g++ (GCC) 16.2.1,`-std=c++20 -Wall -Wextra -O2` |
| 内存 | MemTotal 55522932 kB |
| 工具 | strace 7.2 |
| scratch | `~/lp01_scratch`(沿用 l02/l03 先例,e1d/e2b/e2c/e5b 的数据文件路径烧死在这里,复跑先 `mkdir -p`) |
| 计时口径 | CLOCK_MONOTONIC;WSL2 单 NUMA、无 perf,计时是数量级参考 |

`.out` 为原始 stdout(strace 相关两文件除外),未加工;进程号、计时数值每轮必变,复现看数量级与结论。

## 目录与结论对照

| 目录 | 实验 | 一句话结论 |
|---|---|---|
| `e1-fork-ledger/` | E1 fork 语义账本 | fork 一次调用两次返回(父拿子 pid,子拿 0);64 MiB COW 实证:地址全程相同、两边 Pss 各半→子写满后 Pss 翻倍(物理复制≈128 MiB);1 GiB 父进程 fork 要约 31.6 ms(页表成本,136 倍于小进程),vfork/posix_spawn 恒定几十 µs;父子共享同一打开文件描述,偏移接力推进 |
| `e2-exec-family/` | E2 exec 家族 | 五变体(l/v/e/p 命名规律)一条链上五次变身,pid 全程不变;不带 CLOEXEC 的 fd 穿过 exec 且偏移延续,带的被关(/proc/self/fd 实拍);失败返回 -1 原程序继续跑;strace:全部变体落 execve,fork 落 clone,posix_spawn 落 clone3(CLONE_VM\|CLONE_VFORK) |
| `e3-zombie-reap/` | E3 僵尸与收尸 | 双阶段:exit 后 State: Z(`<defunct>`)+退出码残骸留存,waitpid 后 ENOENT 彻底消失;三种收尸姿势(阻塞 wait/WNOHANG 轮询/SIGCHLD+SA_RESTART)时序对比,EINTR 对照 2 次;退出码只有低 8 位(0x1234→0x34),abort 与 kill -9 靠 WIFSIGNALED 区分 |
| `e4-orphan/` | E4 孤儿与收养 | 本机 PID 1 是 systemd,但孤儿实测被 **pid 249(WSL 会话级 /init,comm=Relay(252))** 收养——内核过继给最近的 subreaper 祖先,PID 1 只是兜底;收养者负责 wait,孤儿没机会变僵尸;孙辈想收尸会被 ECHILD 拒 |
| `e5-posix-spawn/` | E5 posix_spawn 对照 | 同任务两写法行为等价(printenv 带环境/退出码 7 都拿到);file_actions 让重定向长在孩子身上(addopen/adddup2/addclose 三场景);vfork 危险只引 man 原文:共享栈、父挂起、子改数据即 UB |
| `e6-raii-child/` | E6 child_process 封装 | 构造=spawn、析构=先礼后兵再收尸、move-only;五个演示对账:作用域收尸零僵尸、提前 kill、三个孩子进 vector 3/3 清干净、正常退出码、detach 的代价(孩子死在前面就变 Z,实测) |

## E1c 计时表(1 GiB 已触碰父进程,p50,n=30,µs)

| 创建方式 | 小父进程 | 1 GiB 父进程 |
|---|---:|---:|
| fork 仅创建 | 232.6 | 31585.5 |
| vfork 仅创建 | 71.2 | 74.8 |
| fork+exec /bin/true | — | 32117.8 |
| posix_spawn /bin/true | 486.2 | 480.7 |

fork 贵在页表不在页(页靠 COW);posix_spawn 与父进程大小无关,且已含 exec+动态链接成本。

## 复现

```sh
# 一键:编译全部 + 运行 + 把原始输出写回各 .out(会覆盖存档)
./run_all.sh

# 或 CMake
cmake -S . -B build && cmake --build build -j
./build/e3a_zombie_lifecycle   # 任一 target,输出重定向自行采
```

各子目录 README 有单实验的复现命令与逐项解读。
