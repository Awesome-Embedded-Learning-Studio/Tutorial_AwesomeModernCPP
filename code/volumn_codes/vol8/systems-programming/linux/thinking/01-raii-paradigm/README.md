# 01-raii-paradigm 配套实验

《OS 资源的 RAII 范式》(Linux 侧)的实验代码与原始输出存档,四组实验 E1–E4 对应四个子目录。`common/raii.hpp` 是完整版骨架:`unique_fd`(析构 close、move ctor/assign、release/reset/get、swap、explicit operator bool、禁拷贝)与 `mapped_region`(析构 munmap、页取整的 len 记账、release/reset、swap),外加与 file-io/01 篇同款的 `sys_call`/`errno_code`。close 失败(EBADF/EINTR)在析构里怎么处理的结论,写在 `unique_fd` 的类注释里,依据是 man 2 close(man-pages 6.19)NOTES 节。`common/procfd.hpp` 是读 `/proc/self/fd` 的小工具(E2/E4 用)。

## 目录与实验对照

| 目录 | 实验 | 结论 |
|---|---|---|
| `01-strace/` | E1 骨架 + strace | 每段作用域一个 `openat` 配一个 `close`,提前 return 的路径也由析构收尾;`mapped_region` 请求 16 B,strace 里 `munmap` 归还 4096 B,页取整在 syscall 层可见 |
| `02-leak/` | E2 异常路径泄漏对照 | N=1000 循环 open+throw:裸 fd 版 fd 数 3→1003(正好漏 1000 个,编号 3..1002);unique_fd 版 3→3,一个不漏 |
| `03-release/` | E3 归还语义三件套 | `release()` 后该 fd 全程只有调用方手动那次 close,析构没碰;`reset(new)` 先 `close(3)` 再接管 4;`swap()` 后 fd 跟所有权走,析构顺序与声明相反(y 的 3 先 close,x 的 4 后 close) |
| `04-vector/` | E4 进容器 | 不 reserve 扩容搬迁 7 次(=1+2+4),`reserve(8)` 后 0 次;`shuffle` 走 swap 零次移动构造;`sort` 7 次移动构造、fd 集合不变;noexcept 与非 noexcept 两版移动计数逐字段一致 |

## 实验环境

- CPU:`lscpu | grep "Model name"` → AMD Ryzen 7 9700X 8-Core Processor
- WSL2,内核 6.18.33.2-microsoft-standard-WSL2
- g++ 16.2.1(GCC)20260810;编译口径 `g++ -std=c++20 -Wall -Wextra -O2 -I common`(零警告)
- strace 7.2(`strace --version`)
- `ulimit -n`(RLIMIT_NOFILE):soft=hard=1048576,E2 的 N=1000 离额度很远,不会 EMFILE
- 捕获日期:2026-10-02

## 构建与复现

```sh
# E1
g++ -std=c++20 -Wall -Wextra -O2 -I common 01-strace/strace_demo.cpp -o strace_demo
./strace_demo                                   # 输出存档:strace_demo.out
strace -f -e trace=openat,close -o strace_fd.out ./strace_demo
strace -f -e trace=mmap,munmap -o strace_mmap.out ./strace_demo

# E2(先小批量试跑再上 1000,纪律见 leak_raw.out 首段)
g++ -std=c++20 -Wall -Wextra -O2 -I common 02-leak/leak_raw.cpp -o leak_raw
g++ -std=c++20 -Wall -Wextra -O2 -I common 02-leak/leak_raii.cpp -o leak_raii
./leak_raw 8 && ./leak_raii 8 && ./leak_raw 1000 && ./leak_raii 1000

# E3
g++ -std=c++20 -Wall -Wextra -O2 -I common 03-release/release_reset_swap.cpp -o release_reset_swap
./release_reset_swap                            # 输出存档:release_reset_swap.out
strace -f -e trace=openat,close ./release_reset_swap 2>&1  # 存档:strace_release.out(程序输出与 trace 合流)

# E4(两版构建,输出分别为 vector_noexcept.out / vector_throwing.out)
g++ -std=c++20 -Wall -Wextra -O2 -I common 04-vector/vector_demo.cpp -o vector_noexcept
g++ -std=c++20 -Wall -Wextra -O2 -DRAII_MOVE_MAY_THROW -I common 04-vector/vector_demo.cpp -o vector_throwing
./vector_noexcept && ./vector_throwing

# 或整套 CMake
cmake -S . -B build && cmake --build build
```

## 复跑注意

- **路径是烧死的**:源码按捕获时口径把数据文件写在 `/tmp/raii_lab/01-strace/note.txt` 与 `/tmp/raii_lab/02-leak/probe.bin`(E3/E4 用 `/dev/null`)。为与 `.out` 存档严格对应,源码未做改写。复跑前 `mkdir -p /tmp/raii_lab/01-strace /tmp/raii_lab/02-leak`,或改源码开头的 `kPath` 常量。
- **strace 输出里的 pid 前缀与映射地址每次都变**(ASLR),与存档不同是正常的;规律性的东西是「一个 openat 配一个 close」「munmap 长度按页取整」。
- **strace 前几对 openat/close 是动态链接器开的 .so**,全带 `O_CLOEXEC`,与 file-io/01 篇的口径一致,不是实验本身的。
- **E2 换 shell 可能撞不同额度**:fd 从 3 起步,`ulimit -n` 为 1024 的环境里,可用槽位是 3..1023 共 1021 个,N=1000 最高只把 fd 开到 1002,够不着 1024,**不会**触发 EMFILE;把 N 提到 1022 或更高,第 1022 次 open 才会在 fd 表已满时撞上 EMFILE(errno=24;不算继承 fd,继承得多还要更早),那正是 file-io/01 篇 exp5 演的现场。本档捕获时额度是 1048576,所以 1000 次全开成功。
- **E4 两版输出只有第一行 banner 不同**,移动构造计数逐字段一致——move-only 类型扩容时无论 move 是否 noexcept 都走 move,`std::move_if_noexcept` 只在「可拷贝」时才退回拷贝;非 noexcept 损失的是 vector 的强异常保证,不是搬迁方式。这是文章要纠正的常见说法的实测依据。
