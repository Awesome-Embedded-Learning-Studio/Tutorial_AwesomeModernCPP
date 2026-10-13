# e6_bench_laptop —— 512 MiB 顺序读基准(笔记本轮)

机器:i7-13700H,WSL2,内核 6.18.33.2-microsoft-standard-WSL2,g++ 16.2.1(与文章环境段交代一致)。

bench.cpp 是按文章「512 MiB 顺序读」一节的 bench.cpp 节选复原的完整程序
(节选注明「求和循环 sum_bytes 与计时打印略」,这三处按节选注释与文中输出口径补齐):
  - read:1 MiB 缓冲循环 read,lseek 回开头;
  - mmap:MAP_PRIVATE 整段求和;populate 额外给 MAP_POPULATE,并单独计时 mmap 本身;
  - minor faults 读 /proc/self/stat 第 10 字段(打印的是工作结束后的进程累计值,
    与台机历史数字同口径:read 的 413 里含程序启动与 1 MiB 缓冲本身的缺页)。

文件:big.bin 512 MiB,dd if=/dev/urandom 生成,躺在 /tmp(tmpfs)。
运行口径与台机历史一致:for i in 1 2 3; do ./bench read big.bin; ./bench mmap big.bin; done
再补一次 ./bench populate big.bin。全部输出在 bench_laptop.out。
