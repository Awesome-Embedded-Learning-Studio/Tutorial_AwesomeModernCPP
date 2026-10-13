# 03-boundaries —— 段边界动态观察(E3)

环境:同总 README(WSL2 内核 6.18.33.2,glibc 2.44,g++ 16.2.1 `-std=c++20 -Wall -Wextra -Wpedantic -O2`)。三组观察:malloc 分水岭、栈增长、free 归还。每笔分配后回读 `/proc/self/maps` 判定指针落点。

## E3-1 malloc 分水岭:实测 131050 字节(对照 `03-boundaries.out`)

尺寸从 1KB 一路加到 64MB,在 128KiB 附近细扫:

| 请求(字节) | 来源 |
|---|---|
| …131048 / 131049 | brk([heap]) |
| **131050** 起(含 131072/1MB/4MB/64MB) | mmap(匿名) |

- 教科书口径「请求 ≥128KiB 走 mmap」不精确,机制是两层:堆顶有富余时请求直接从 arena 切,连系统调用都没有(本存档输出里 131049 的分配零 syscall);堆顶不够才走 sysmalloc,判据是**内部 chunk 尺寸** `nb = roundup(req+8, 16)` **大于等于** `mmap_threshold(131072)` 才走 mmap(req=131049 与 131050 的 nb 都是 131072)。单独进程只 malloc 一笔的落点随启动堆顶富余漂移(复跑 20+ 次可全落 [heap]),持有序列下的 **131049/131050 分水岭**才是可复现口径。
- 走 brk 的请求让 [heap] 按 128KB 档往上顶(+188416/+237568/+258048/+262144 字节,含 `M_TOP_PAD` 余量);走 mmap 后 [heap] 不再动。
- **相邻匿名 mmap 会被内核合并成一个 VMA**:131050 那笔的「所在映射」显示 152KB,除自身按页取整的 132KB 外,多出的 20KB 是启动期 12KiB+8KiB 两笔匿名 mmap 并进了同一条 VMA。maps 行数 ≠ mmap/malloc 次数,读 maps 时要意识到合并。

## E3-2 栈增长(同一次运行)

- `alloca` 每轮 4KB、200 轮:缓冲地址从 `…938910` 一路降到 `…8a1fb0`,每 50 轮降约 200KB——**栈向低地址生长,alloca 不留痕**(函数返回即让出)。
- 深递归 12000 帧:帧地址每 3000 层报一次,单调递减;实测每帧 ~488B(源码里数组只有 192B,其余是 -O2 下的保存寄存器与对齐)。
- [stack] 从 `7fff4b91a000-7fff4b93c000`(136KB)变为 `7fff4b5ff000-7fff4b93c000`:**顶端(高地址端)不动,低地址端下探 3180KB**。RLIMIT_STACK=8192KB 是上限,撞线即 SIGSEGV。

## E3-3 free 之后地址空间还不还

| 操作 | 结果 |
|---|---|
| free(64MB mmap 块) | 所在映射**立即消失**(munmap 立刻归还地址空间) |
| 256×48KB 喂大 [heap] 后全 free | [heap] 1128KB→13504KB→**1260KB,brk 收缩**(glibc trim;只回收堆顶以上,留 M_TOP_PAD;已写脏的 RSS 页随地址一起还给内核) |
| free(4MB mmap 块)后 malloc(3MB) | **回到 [heap]**(end 恰好 +0x300000) |
| 随后 malloc(5MB) | 仍走 mmap |

第三行是 glibc 的**动态 mmap 阈值**:free 一块大于当前阈值的 mmap 块,阈值抬到该块大小(上限 32MiB),此后小于新阈值的请求回 brk。注意:一旦用 `mallopt(M_MMAP_THRESHOLD, …)` 显式设过,动态调整永久关闭,所以本实验全程不碰 mallopt。

## 复现

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 03-boundaries.cpp -o 03-boundaries
./03-boundaries | tee 03-boundaries.out
```

全程峰值占用 ~90MB 虚拟内存,约 1 秒跑完。
