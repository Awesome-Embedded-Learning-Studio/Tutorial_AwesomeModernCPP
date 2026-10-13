# 02-dup —— dup / dup2 / dup3 全家:重定向、共享偏移、CLOEXEC 的生死(E2)

环境:WSL2 内核 6.18.33.2,g++ 16.2.1 `-std=c++20 -Wall -Wextra -Wpedantic -O2`,数据文件在 ext4(`~/l01b_scratch/e2/`,路径烧死在源码里)。子进程经 `/proc/self/exe` 重新 exec 自己,fd 号走 argv 传递。

## 结论(对照 `dup_family.out`)

| 场景 | 结果 |
|---|---|
| a) dup 与 FD_CLOEXEC | open 的 fd F_GETFD=1(带 CLOEXEC),dup 出来的 F_GETFD=0——fd 标志不随复制走 |
| b) 共享偏移 | fd 读 4 字节("0123",偏移 4),dup 件接着 read 得 "4567"(偏移 8);重新 open 的 fd 却从头读出 "0123" |
| c) dup2 原子重定向 | `dup2(rf, 1)` 后 printf 落进 redirect.txt(读回验证);不 fflush 的那条在换回 stdout 后才冲出来——flush 落点跟着「此刻的 fd 1」走,不跟着 printf 调用时刻走 |
| d) close(1)+dup 的空窗 | close(1) 后下一次 open 返回 fd 1——1 号槽被无关 open 抢走;dup2 一条系统调用收口,没有竞态窗口 |
| e) dup3 两个差异 | `dup3(fd,11,O_CLOEXEC)` 的 F_GETFD=1 而 dup2 的=0;`dup3(fd,fd,0)`=-1/EINVAL,dup2(fd,fd) 无害返回 fd |
| f) fork+exec 下 CLOEXEC 生死 | 见下 |

### f) 的证据链(本实验主菜)

父进程备三个 fd:3(裸 open)、4(open 后 `F_SETFD FD_CLOEXEC`)、20(从 4 `F_DUPFD` 复制)。fork 后 exec 自己:

```
[child exec 前] /proc/self/fd: 0 1 2 3 4 20
[child exec 后] /proc/self/fd: 0 1 2 3 20     <- 4 没了
read(3, ...)      = 10:"0123456789"           <- 裸 fd 活过 execve
fcntl(4, F_GETFD) = -1, errno=9(EBADF)        <- 带 CLOEXEC 的 fd 在 exec 前被内核关闭
read(20, ...)     = 10:"0123456789"           <- 从 4 复制来的,fd 标志不随 dup 走,又活了
```

strace 侧(`dup_cloexec_strace.txt`,节选自 `strace -f -qq -e trace=execve,fcntl,close,dup,dup2,dup3,openat,read,getdents64`)有两个值得贴文章的点:

1. execve 之前**没有** `close(4)`——CLOEXEC 的关闭发生在 execve 内核路径里,不产生 close(2) 系统调用,strace 看不见;
2. execve 之后动态链接器做的第一件事就是 `openat("/etc/ld.so.cache")= 4`——被释放的 4 号槽立刻被复用,加载完又因它自带的 O_CLOEXEC 关掉。所以「exec 后 fd 4 还在不在」这个问题,答案取决于你看的时刻;行为验证比看 fd 号更可靠的是拿它 read/fcntl。

## 复现

```sh
mkdir -p ~/l01b_scratch/e2
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 -I ../common dup_family.cpp -o /tmp/e2
/tmp/e2 | tee dup_family.out
strace -f -qq -e trace=execve,fcntl,close,dup,dup2,dup3,openat,read,getdents64 \
       -o /tmp/e2_strace.txt /tmp/e2 > /dev/null   # 再从 /tmp/e2_strace.txt 里节选
```

秒级跑完。§c 的「pending」演示对缓冲模式不挑:管道(tee)、重定向到文件、真终端(script 模拟)三种口径实测输出一致——pending 行不带换行,在哪种缓冲模式下都会留到换回 stdout 之后才被冲出去。
