# 03-seek —— lseek 的 whence 家族:负偏移、ESPIPE、稀疏文件 data/hole 探测(E3)

环境:WSL2 内核 6.18.33.2,g++ 16.2.1 `-std=c++20 -Wall -Wextra -Wpedantic -O2`,数据文件在 ext4(`~/l01b_scratch/e3/`,路径烧死在源码里)。SEEK_SET 的基本用法 L01 正文已讲,这里只做增量。

## 结论(对照 `seek_family.out`)

| 场景 | 结果 |
|---|---|
| a) 稀疏文件构造 | ftruncate 拉 1 MiB + 三段 pwrite;`st_size`=1048576 而 `st_blocks`×512=12288——磁盘实占 12 KiB,账面 1 MiB |
| b) data/hole 全区间探测 | 1 MiB 文件走出 7 段:hole[0,4096) / data[4096,8192) / hole[8192,69632) / data[69632,73728) / hole[73728,1040384) / data[1040384,1044480) / 尾部 hole(SEEK_DATA 报 ENXIO 收尾);洞里 pread 出全零页 |
| c) ENXIO 两种脸 | 全洞文件 `lseek(0,SEEK_DATA)`=-1/ENXIO;从文件尾出发同样 ENXIO |
| d) 负偏移 | `lseek(-4,SEEK_END)`=6 → read "6789";读 2 字节后 `lseek(-1,SEEK_CUR)`=1 → 重读 "12" |
| e) 边界 | `lseek(-1,SEEK_SET)`=-1/EINVAL(负绝对位置);`lseek(+100,SEEK_END)`=110 合法但 st_size 仍 10——洞已定位,write 才落盘 |
| f) ESPIPE | 管道读端上五种 whence(SET/CUR/END/DATA/HOLE)全部 -1,errno=29(ESPIPE) |

两个值得进文章的精度问题:

1. **块粒度**:偏移 4096 处只 pwrite 了 8 字节,报告却是 4096 字节的 data;尾部 256 字节的 Z 段同样被撑到块边界。SEEK_DATA/SEEK_HOLE 的精度是文件系统块(本机 ext4 4 KiB),小于一个块的洞不存在。想演示干净的洞,区间要么对齐块边界,要么留足一个块。
2. **EOF 即隐式洞尾**:数据段结尾再无数据时,SEEK_HOLE 返回 st_size;walk 循环以「SEEK_DATA 报 ENXIO」收尾是通用写法。

## 复现

```sh
mkdir -p ~/l01b_scratch/e3
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 -I ../common seek_family.cpp -o /tmp/e3
/tmp/e3 | tee seek_family.out   # .out 是 2026-10-02 WSL2 ext4 上那轮的捕获
```

秒级跑完,无网络无等待。
