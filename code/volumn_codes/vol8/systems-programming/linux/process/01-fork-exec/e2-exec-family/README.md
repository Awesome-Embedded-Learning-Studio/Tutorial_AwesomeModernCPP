# E2 exec 家族:五个变体、fd 的生死、失败路径、strace 取证

环境与总口径见[上级 README](../README.md)。

## 五变体参数形态表

| 变体 | 原型 | 路径 | 参数 | 环境 |
|---|---|---|---|---|
| execl | `execl(path, arg0, arg1, …, NULL)` | 完整路径 | 逐个列,哨兵 NULL | 继承 environ |
| execv | `execv(path, argv)` | 完整路径 | `char* const argv[]` 数组 | 继承 environ |
| execve | `execve(path, argv, envp)` | 完整路径 | 数组 | **自己给 envp**;系统调用本尊 |
| execlp | `execlp(name, arg0, …, NULL)` | 文件名,搜 PATH | 逐个列 | 继承 environ |
| execvp | `execvp(name, argv)` | 文件名,搜 PATH | 数组 | 继承 environ |

命名规律:结尾 `l`=list 逐个列、`v`=vector 数组、`e`=自带环境、`p`=PATH 搜索。另有 execle/execvpe 凑齐全家福。

## e2a_exec_variants —— 链上自我变身五次

一个进程依次经 execl→execv→execve→execlp→execvp 换了五次程序映像,`.out` 里每棒打印 pid,全程 26434 没变。出发前先 probe:PATH 不含本目录时 `execlp` 确实 ENOENT,程序再把自己所在目录 prepend 进 PATH,第 4/5 棒的 execlp/execvp 才搜得到——`p` 系列只认 PATH,不认当前目录。execve 那棒塞的自定义 envp(`E02A_VIA=execve`)一路带到终点。

## e2b_exec_fd —— exec 前后 /proc/self/fd 实拍

同一个数据文件开两个 fd:fd 3(普通)与 fd 4(O_CLOEXEC)。exec 后:

- pid 不变(26436 → 26436)
- fd 3 还在,且接着 exec 前的偏移继续读——继承的是同一份打开文件描述(与 E1d 呼应)
- fd 4 消失:同一个文件,一个穿过 exec 一个被关

运行时补 CLOEXEC 用 `fcntl(fd, F_SETFD, FD_CLOEXEC)`(fd 标志,不是 open 的 O_CLOEXEC 状态标志);`L01 已讲 FD_CLOEXEC 的语义,这里给的是 exec 前后的实拍对照。清单里那个指向 `/proc/self/fd` 的条目是列举动作自己打开的目录 fd。

## e2c_exec_fail —— 失败不换命

三种死法各返回 -1:路径不存在(ENOENT=2)、文件在但无 x 权限(EACCES=13)、PATH 翻不到(ENOENT=2)。三次失败后进程继续跑——exec 只有成功才有去无回,失败就是普通函数返回,所以 `exec` 后面跟 perror+exit 是失败路径,不是摆设。

## e2d_strace_target —— 谁是真系统调用

`e2d.out` 是程序输出,`e2d.strace` 是 `strace -f -e trace=execve,execveat,clone,clone3` 的原始日志。四个铁证:

1. 代码写 `execl`,内核只见 `execve("/bin/echo", …) = 0`——l/v/e/p 全是 libc 包装,系统调用只有 execve(及 execveat)
2. execlp/execvp 的 PATH 搜索是**用户态循环试 execve**:.strace 里连续 8 个 `execve("<PATH 各目录>/echo") = -1 ENOENT` 后命中 `/usr/sbin/echo`(本机 PATH 里 /usr/sbin 排在 /usr/bin 前,且 /bin 不直接在 PATH 里)
3. `fork()` 在系统调用层是 `clone(CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD)`
4. `posix_spawn` 是 `clone3(CLONE_VM|CLONE_VFORK|CLONE_CLEAR_SIGHAND)` + execve——借的是 vfork 语义的快通道,这正是 e1c 里它对 1 GiB 父进程依然 ~500 µs 的原因

复现(在本目录):

```sh
strace -f -e trace=execve,execveat,clone,clone3 -o e2d.strace \
    ./e2d_strace_target > e2d.out 2>&1
```
