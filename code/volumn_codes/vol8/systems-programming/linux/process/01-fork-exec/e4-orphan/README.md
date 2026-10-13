# E4 孤儿与收养:本机 PID 1 是谁,孤儿被谁收养

环境与总口径见[上级 README](../README.md)。

## 实测结论(本会话)

- `/proc/1/comm` = **systemd**,`/proc/1/cmdline` = `/sbin/init`——这台 WSL2 开了 systemd
- 但孤儿 C 的 ppid 实测变成 **249(comm=`Relay(252)`),不是 PID 1**。249 是 WSL 的会话级 `/init` 中继(root 所有,cmdline 为 `/init`,本会话所有 zsh 都挂在它名下),内核把它当作**最近的 subreaper 祖先**(prctl PR_SET_CHILD_SUBREAPER 标记的进程优先收养,PID 1 只是兜底)
- 换到没有中间 subreaper 的环境(直连 init 的会话),收养者才是 PID 1——程序按实测打印,两种措辞都写了分支
- 孤儿不变僵尸的机制:收养者负责 wait。C 退出后观察者 A 轮询 `/proc/C/status`,t+1.1s 直接 ENOENT,全程没有 Z 滞留——僵尸的定义是死了没人 wait,收养者永远会 wait
- 旁证:A 试图 `waitpid` 孙辈 C,返回 -1/ECHILD——血缘上隔一代就没资格收尸

## 结构

观察者 A → fork B(临时父)→ B fork C 后立刻退场 → C 成孤儿。C 的 pid 与实测收养者 pid 经两条管道报给 A(C 出生即报 pid,过继完成后报收养者);B 等 C 的平安信才退场,保证打印顺序。**管道端口持有权是这个实验踩过的最大的坑**:读端要在 A 手里留到第二次读完成,关早了 C 的 write 直接吃 SIGPIPE 暴毙(第一版就是这么死的,输出里收养行凭空消失)。

复现(在本目录):

```sh
g++ -std=c++20 -Wall -Wextra -O2 -o /tmp/e4 e4_orphan.cpp && /tmp/e4
# 全程约 1.5s;收养者 pid 因会话而异,WSL 下通常是会话 /init(Relay),不是 1
```
