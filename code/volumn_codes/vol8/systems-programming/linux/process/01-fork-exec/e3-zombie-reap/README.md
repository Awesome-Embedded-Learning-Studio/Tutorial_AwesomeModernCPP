# E3 僵尸与收尸(重头戏):双阶段退出、三种收尸姿势、退出码解码

环境与总口径见[上级 README](../README.md)。

## e3a_zombie_lifecycle —— 双阶段退出的完整时序

子进程 `_exit(42)` 之后父进程故意不 wait,`.out` 给出三段式时序:

| 时刻 | 观察 |
|---|---|
| t1 子还活着 | `/proc/<pid>/status`: `State:\tS (sleeping)` |
| t2 子已退、未收尸 | `State:\tZ (zombie)`;ps 眼里 `STAT=Z`、`COMMAND=e3a_zombie_life <defunct>` |
| t3 父 waitpid 后 | `WIFEXITED=1 WEXITSTATUS=42`;再读 /proc/<pid>/status:ENoENT,进程彻底消失 |

exit(或 _exit)只是第一阶段:内存释放、fd 关闭,但 task_struct 和退出码留下等父进程取——这个残骸就是僵尸。wait 才是第二阶段。ps 的 `<defunct>` 与 /proc 的 Z 是同一件事的两种写法。

## e3b_reap_strategies —— 三种收尸姿势 + 附送

三个子进程错峰退出(200/400/600ms)的同一场景:

1. **阻塞 wait()**:父进程全程干等,子死一个醒一次,`.out` 里按 200/401/601ms 顺序收齐
2. **waitpid(WNOHANG) 轮询**:不死等,但 20ms 间隔轮询 33 次才收齐 3 个,其中 30 次空手——空转与延迟的折中
3. **SIGCHLD 处理器**:父进程去 read 管道干正事,处理器里 `while (waitpid(-1, …, WNOHANG) > 0)` 一网打尽:
   - 加 SA_RESTART:被 SIGCHLD 打断的 read 自动续上,全程 EINTR 0 次
   - 不加 SA_RESTART:read 返回 -1/errno=EINTR,自己重试,实测发生 2 次
   - 处理器里只能用异步信号安全函数:write/waitpid/clock_gettime 可以,printf/snprintf 不行(用户态缓冲+锁),`.cpp` 里是手工拼数字
4. **附送 SIGCHLD=SIG_IGN**:明确设成 SIG_IGN(不是默认忽略)后内核自动收尸,不 wait 也没僵尸;代价是 waitpid 返回 -1/ECHILD,退出码永远拿不到(另注意 SA_NOCLDWAIT 同效果)

## e3c_exit_status —— 四种死法与 8 位截断

| 死法 | 解码结果 |
|---|---|
| `_exit(42)` | WIFEXITED=1,WEXITSTATUS=42 (0x2A) |
| `_exit(0x1234)` | WEXITSTATUS=52 (0x34)——**退出码只有低 8 位,传 0x1234 到手只剩 0x34** |
| `abort()` | WIFSIGNALED=1,WTERMSIG=6 (SIGABRT) |
| 父进程 `kill(pid, SIGKILL)` | WIFSIGNALED=1,WTERMSIG=9 (SIGKILL) |

waitpid 的原始状态字被退出码与信号复用:不先查 WIFSIGNALED 直接 WEXITSTATUS,被信号杀死会被读成古怪值。shell 的 `$?` 同样只有低 8 位(exit 256 与 exit 0 不可分)。

复现(在本目录,逐个):

```sh
g++ -std=c++20 -Wall -Wextra -O2 -o /tmp/e3a e3a_zombie_lifecycle.cpp && /tmp/e3a
# e3b 全程约 3.5s(含四段);e3c 秒回
```
