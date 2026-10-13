# 01-posix-file-io-supplement —— 《POSIX 文件 I/O》补课段实验

对应文章:`documents/vol8-domains/systems-programming/linux/file-io/01-posix-file-io.md` 的补课项(EINTR 重试 / fcntl 族 / dup3 与 close-on-exec / SEEK_DATA/SEEK_HOLE)。正文的 open/read/write/SEEK_SET 基本款实验不在此重复。

编号对照:正文实验号 exp9 / exp10 / exp11 = 存档标号补课段 E1 / E2 / E3(即 01-fcntl / 02-dup / 03-seek 三目录);产物与数据目录用短名 e1 / e2 / e3,与本卷姊妹篇自己的 e/E 编号体系互不相干。

| 目录 | 补课项 | 主结论 |
|---|---|---|
| `01-fcntl/` | F_GETFD/F_SETFD 与 F_GETFL/F_SETFL 两族 | fd 标志住 fd 表项(每 fd 一份,不随 dup),状态标志住打开文件描述(dup 共享);F_SETFL 整体覆盖、改不动访问模式;F_SETFL 加 O_NONBLOCK 后 read 立刻 EAGAIN |
| `02-dup/` | dup/dup2/dup3 全家 + CLOEXEC 生死 | dup2 原子重定向 printf 落文件;dup3 带 O_CLOEXEC、oldfd==newfd 报 EINVAL;fork+exec 下裸 fd 活过 execve、CLOEXEC fd 在 exec 前被内核关闭、dup 件因不带 fd 标志又活了 |
| `03-seek/` | SEEK_CUR/SEEK_END 负偏移、ESPIPE、SEEK_DATA/SEEK_HOLE | 稀疏文件 data/hole 交替区间全探测;ENXIO 两种脸;管道上五种 whence 全 ESPIPE;探测精度是文件系统块 |

EINTR 重试不在本目录:`linux/thinking/02-error-paradigm/02-eintr-retry/` 已有完整实验(sys_call 异常版/expected 版 + SA_RESTART 对比 + strace 取证),文章引用那边即可。

公共骨架 `common/article.hpp` 的三件公共工具(errno_code / sys_call / unique_fd)沿用思维基石两篇的定义(唯一出处:`thinking/01-raii-paradigm.md` 与 `thinking/02-error-paradigm.md`),与 `05-file-lock/common/article.hpp` 同源。

## 编译与复现

统一口径:g++ 16.2.1 `-std=c++20 -Wall -Wextra -Wpedantic -O2`(实测零警告);数据文件路径烧死在源码里,统一放 `~/l01b_scratch/eN`(ext4)。各目录 README 有逐条复现命令,也可以:

```sh
cmake -S . -B /tmp/build-l01b && cmake --build /tmp/build-l01b
mkdir -p ~/l01b_scratch/{e1,e2,e3}
/tmp/build-l01b/e1_fcntl | tee 01-fcntl/fcntl_flags.out
```

`.out` 文件均为 2026-10-02 WSL2(内核 6.18.33.2)那轮的真实捕获。
