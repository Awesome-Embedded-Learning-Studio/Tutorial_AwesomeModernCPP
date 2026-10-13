# E6 招牌封装:child_process——构造即 spawn,析构即收尸,move-only

环境与总口径见[上级 README](../README.md)。

## 设计条款(与 `.cpp` 文件头一致)

1. 构造 = posix_spawn(E5 已证一发完成),失败抛 `std::system_error`
2. 析构三路径:还活着→SIGTERM 礼貌→100ms 不退→SIGKILL 强杀→waitpid 收尸;已死未收→只收尸;已收/detached→什么都不做
3. 拷贝删除、移动允许——进程句柄是独占资源,与 unique_ptr 同理,`static_assert` 三条编译期钉死
4. `wait()` 阻塞收尸返回解码后的 `exit_status{signaled, code}`;`try_wait()` 是 WNOHANG 版
5. `detach()` 只解除「我负责收尸」的承诺(POSIX_SPAWN_SETPGROUP 进新进程组)

## 五个演示,`.out` 全程对账

| 演示 | 验证 |
|---|---|
| 1 作用域结束自动收尸 | 析构日志走 SIGTERM→收尸两步;出作用域后 `/proc/<pid>` 已消失,僵尸零残留 |
| 2 提前 kill | 主动 SIGKILL 后 `wait()` 解码出「被信号 9 杀死」;析构发现 pid 已收,不再动 |
| 3 move-only 进容器 | 三个孩子 move 进 `vector`,原对象 `pid()==-1`;vector 析构逐个 SIGTERM+收尸,3/3 从 /proc 消失 |
| 4 正常完成 | echo 的输出直接进父进程 stdout,`wait()` 拿到退出码 0 |
| 5 detach 的代价 | detached 的 sleep 在作用域外还活着;它自然退出后 `/proc` 里状态 **Z**——detach 只是解除承诺,没人 wait 就有僵尸;要免责得全局 `signal(SIGCHLD, SIG_IGN)`(见 e3b 附送)或保证它比我们活得久 |

## 两个工程细节值得点名

- 构造函数里 `fflush(stdout)`:孩子继承 stdout 且直写 fd,不冲缓冲,子进程的输出会反超父进程缓冲里的标签行(演示 4 调试时实测踩到)
- 析构不等不抛:noexcept,kill/waitpid 失败也只记日志;真·daemon 还要 setsid+重定向 stdio,超出本篇

复现(在本目录):

```sh
g++ -std=c++20 -Wall -Wextra -O2 -o /tmp/e6 e6_child_process.cpp && /tmp/e6
# 全程约 3.5s(演示 5 要等 detached 孩子自然退出)
```
