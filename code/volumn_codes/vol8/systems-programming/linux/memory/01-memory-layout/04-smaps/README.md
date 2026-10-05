# 04-smaps —— smaps 字段解读与 pmap 对照(E4)

环境:同总 README(WSL2 内核 6.18.33.2,g++ 16.2.1 `-std=c++20 -Wall -Wextra -Wpedantic -O2`)。程序先制造可观察的内存状态(堆上 64×32KB 全写脏、栈上摸 256KB),再给四个代表段贴 maps+smaps 两份原文,抽关键字段做表,最后 spawn `pmap -x` 对同一进程再数一遍。

## 关键字段实测(对照 `04-smaps.out`,单位 kB)

| 段 | Size | Rss | Pss | Sh_Cln | Pr_Dirty | 说明 |
|---|---|---|---|---|---|---|
| 本程序 .text | 24 | 24 | 24 | 0 | 24 | 见下方「意外」:刚编译完是 Private_Dirty |
| [heap] | 2260 | 2076 | 2076 | 0 | 2076 | 匿名私有,写过的全算 Private_Dirty;Size>Rss(184KB 还没碰,以 .out 为准) |
| [stack] | 276 | 276 | 276 | 0 | 276 | 匿名私有 |
| libc .text | 1516 | 1004 | **7** | **1004** | 0 | Rss 1004 全是 Shared_Clean;Pss 按共享进程数摊派,只记账 7 |

- **Rss/Pss 差别看 libc .text 最直观**:这 1MB 代码几十个进程共享,Rss 全额 1004kB,Pss 摊派后只剩 7kB——Pss 才是「这支进程的真实 footprint」,把各段 Pss 相加即全进程 footprint(smaps_rollup 里内核直接给了总和)。
- **Shared/Private 按是否与其他进程共享分账,Clean/Dirty 按页是否被写过(私有)或页缓存是否干净(文件)分账**;匿名私有段(heap/stack)Rss=Pss=Private_Dirty,四项归一。
- **THP 口径**:本机 `/sys/kernel/mm/transparent_hugepage/enabled` 是 `madvise`,非 MADV_HUGEPAGE 的匿名段 `THPeligible=0`、`AnonHugePages=0 kB`。`KernelPageSize=MMUPageSize=4 kB`。VmFlags 行是内核旗标缩写:`rd ex mr mw me sd`(可读可执行、mayread/maywrite/mayexec、softdirty 等),[stack] 的多一个 `gd`(growsdown)。
- **pmap -x 对照**:Kbytes/RSS 列与 smaps 的 Size/Rss 同源(total 9516/6564);pmap 的 **Dirty 列是它按 /proc/pid/pagemap 自家口径算的**,与 smaps 的 Private_Dirty 不逐行相等,别拿两表硬对。
- **意外:刚编译完的二进制,.text 显示 Private_Dirty**。原因:smaps 对文件私有映射的 clean/dirty 看的是**页缓存回写状态**——新二进制的页还没写回磁盘,映射进来就是 dirty。验证:同一二进制 `sync; sleep 8` 后再跑,同一行变成 `Private_Clean 24 kB / Private_Dirty 0 kB`(两步都在下方复现命令里)。这也解释了为什么 strace/gdb 场景下看到的 Dirty 数字「不合直觉」。

## 复现

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 04-smaps.cpp -o 04-smaps
./04-smaps | tee 04-smaps.out          # 刚编完立刻跑:.text 是 Private_Dirty
grep -A22 'r-xp 00002000.*04-smaps' 04-smaps.out | grep -E 'Private_(Clean|Dirty)'
sync; sleep 8; ./04-smaps | grep -A22 'r-xp 00002000.*04-smaps' | grep -E 'Private_(Clean|Dirty)'
# ↑ 第二次:Private_Clean 24 / Private_Dirty 0(页缓存已回写)
```
