# 02-mmap-memory-mapping 配套实验

[02-mmap-memory-mapping.md](../../../../../documents/vol8-domains/systems-programming/linux/file-io/02-mmap-memory-mapping.md) 的实验代码与原始输出存档。源文件名与文章代码块标注一一对应(`e1.cpp`~`e5.cpp`、`fault_cost.cpp`、`sigbus.cpp`、`cow.cpp`、`bench.cpp`);`common/` 里是 `unique_fd`/`sys_call`/`errno_code`(`article.hpp`)与信号安全的 `write` 封装(`sigout.hpp`),即文章说的"沿用上一篇定义"的那三件工具。

## 目录与文章小节对照

目录按文章出现顺序编号;`eN` 前缀是施工批次的编号,与目录顺序不一致,以本表为准:

| 目录 | 文章小节 | 内容 |
|---|---|---|
| `01-fault-cost/` | 缺页 | 三趟"每页摸一个字节",minor fault 计数;`rerun1~3` 是复跑的波动记录(1.82/1.62/… ms) |
| `02-proc-maps/` | 用 /proc/self/maps 看见映射 | SHARED/PRIVATE 双视图 + ext4 对照 |
| `03-sigbus/` | SIGBUS | ftruncate 砍半后摸第二页,SA_SIGINFO 收尸 |
| `04-mprotect-guard/` | mprotect | 匿名三页 + guard page,`SEGV_ACCERR`/`SEGV_MAPERR` 五场景 |
| `05-mprotect-file/` | mprotect | 文件视图五步:降权、子进程越权、EINVAL、EACCES、MAP_PRIVATE 合法 |
| `06-shared-vis/` | MAP_SHARED | 子进程独立 open+mmap,写一字节,管道通知,全程无 msync |
| `07-dirty-msync/` | msync 与 madvise | 128 MiB 弄脏,Dirty 计数涨落;`rerun1~2` 是复跑记录 |
| `08-cow/` | MAP_PRIVATE | 私有映射写 XXXX,pread 问文件,共享映射看原件 |
| `09-bench/` | 512 MiB 顺序读 | read vs mmap vs populate;细节见该目录内 README |

## 构建

需要 Linux(WSL2 也行)与支持 `std::print` 的 g++(≥ 13;实测 16.2.1)。两种方式:

```sh
# 单发(g++ 在仓库根的 code/volumn_codes/vol8/systems-programming/linux/file-io/02-mmap-memory-mapping 下)
g++ -std=c++23 -O2 -I common 04-mprotect-guard/e1.cpp -o /tmp/e1

# 或整套 CMake
cmake -S . -B build && cmake --build build
```

`e1.cpp` 请务必带 `-O2`:文章讲的 `volatile sig_atomic_t` 返工故事,就是在 `-O2` 下才现形的。

## 复跑注意

- **路径是烧死的**:源码按当时口径把数据文件写在绝对路径上(`/tmp/l02_exps/...` 与 `/home/charliechen/l02_scratch/...`)。为了和 `.out` 存档严格对应,源码未做改写。复跑前要么 `mkdir -p /tmp/l02_exps/<各子目录> ~/l02_scratch`,要么把各 `.cpp` 开头的 `path` 常量改成你自己的目录。
- **`09-bench/` 的 big.bin(512 MiB)不随仓库**:`dd if=/dev/urandom of=big.bin bs=1M count=512`,放在 tmpfs(/tmp)上才有文章的"排除磁盘变量"口径。
- **`07-dirty-msync/` 要真盘 ext4**:/tmp 是 tmpfs,写回永远等不来;数据文件放 ext4 上,跑前 `sync && sleep 2` 压基线。
- **地址每次都变**:输出里的映射基址、`si_addr` 都吃 ASLR,与你跑出来的不同是正常的;文章引的规律是"si_addr 等于出错页的页首",不是具体数值。

## 输出档案归属

全部 `.out` 都是笔记本(i7-13700H,WSL2,内核 6.18.33.2-microsoft-standard-WSL2)那一轮的捕获,文章引用的笔记本数字与它们逐个对得上。台机(Ryzen 7 9700X)的输出(缺页一节的 7.20 ms 一组、SIGBUS 的地址、基准一节的那组数字)只存于文章文字,当时没有留档——这是文章环境段交代过的口径。
