# E5 posix_spawn 对照:代码量、行为等价、file_actions、vfork 的危险

环境与总口径见[上级 README](../README.md)。

## e5a_spawn_compare —— 同一任务,两种写法

任务一(带自定义环境跑 `printenv E5A_MARK`)与任务二(`sh -c "exit 7"` 取退出码)各跑两遍:`.out` 里 fork+execve 与 posix_spawn 行为完全一致(printenv 都打出了 `hello-spawn-env`,退出码都拿到 7)。差别在手感:

- fork+execve 三件套:fork 分叉点 + execve 失败路径(子进程里失败只能 `_exit(127)`,习惯法 127=找不到、126=没权限)+ 父进程 waitpid
- posix_spawn 一发完成:错误码从**返回值**拿(不是 errno 约定),file_actions 与 attr 随调用一起给(见 e5b)

glibc 的 posix_spawn 内部走 `clone3(CLONE_VM|CLONE_VFORK)`+execve(e2d 的 strace 铁证),等于 vfork 级的速度但不劳你冒 vfork 的险。

## e5b_spawn_actions —— 重定向长在孩子身上

file_actions 是一张「孩子出生后、exec 前替它做的事」清单,父进程一个 fd 都不用改:

- **addopen**:子进程里 open(out1, O_WRONLY|O_CREAT|O_TRUNC) 并装到 fd 1——echo 的 stdout 整个进了文件,`.out` 事后回读文件内容验证
- **adddup2**:父进程先 open(out2),清单写 dup2(out2,1)+addclose(原 fd)——父进程的 fd 3 全程没动
- **addclose**:对照实验。两个坑替读者踩过:(1) 不能拿 `sh -c` 包一层,sh 启动发现 std fd 缺失会自动补开到 /dev/null,把演示自愈掉;(2) 直接跑 `ls -l /proc/self/fd` 时,addclose(0) 空出的 0 号被 ls 自己的目录句柄按「最低空闲号」复用——`.out` 对照二里 `0 -> /proc/<ls>/fd`,链接目标暴露了它不是继承来的 stdin

## vfork 的危险(只引文档,不实测危险行为)

本组实测了 vfork 的**快**(e1c:1 GiB 父进程下 74.8 µs,与 posix_spawn 同通道),危险部分按任务约定只引 Linux man-pages(man 2 vfork,man7.org 原文):

> "the calling thread is suspended until the child terminates"

> "Until that point, the child shares all memory with its parent, including the stack."

> "the behavior is undefined if the process created by vfork() either modifies any data" (except a variable of type pid_t used to store the return value)

> 4.2BSD 手册原话:"This system call will be eliminated when proper system sharing mechanisms are implemented."

共享栈、父进程被挂起、子进程改任何数据即未定义——这就是"危险半句"的全部:它在 exec 前的窗口里不是独立进程。POSIX.1-2008 删除了 vfork 规格;应用代码要么 fork(要隔离)要么 posix_spawn(要快),自己裸调 vfork 的场景几乎不存在。

复现(在本目录):

```sh
mkdir -p ~/lp01_scratch   # e5b 的输出文件路径烧死在这里
g++ -std=c++20 -Wall -Wextra -O2 -o /tmp/e5a e5a_spawn_compare.cpp && /tmp/e5a
g++ -std=c++20 -Wall -Wextra -O2 -o /tmp/e5b e5b_spawn_actions.cpp && /tmp/e5b
```
