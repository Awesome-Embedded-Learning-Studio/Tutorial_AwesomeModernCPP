# 02-var-locations —— 变量落位验证(E2)

环境:同总 README(WSL2 内核 6.18.33.2,g++ 16.2.1 `-std=c++20 -Wall -Wextra -Wpedantic -O2`)。程序先取 18 类变量的地址,再读同一运行里的 `/proc/self/maps` 逐个对表——ASLR 只影响不同运行之间,同一次运行内自洽。

## 结论(对照 `02-var-locations.out`,18 项全部一致)

| 变量 | 落在 | 要点 |
|---|---|---|
| `g_init` / `g_data_buf`(64KB 已初始化) | 本程序 rw 文件段 | .data 在文件里占实打实的 64KB |
| `&g_bss_buf[0]`(1MB 未初始化起点) | 本程序 rw 文件段**尾页** | .bss 开头挤在文件 rw 段最后一页里 |
| `&g_bss_buf[512K]` | 匿名 rw 段(紧贴程序 rw 段) | .bss 跨过文件尾后进匿名接续页 |
| `g_bss_small` / `s_bss`(小 bss 变量) | 本程序 rw 文件段 | 小 bss 与 .data 同段混居,不进匿名页 |
| `&g_const` / 字符串字面量 | 本程序 .rodata(r-- 文件段) | 值经指针实读,证明真有这块内存 |
| `s_init`(static 局部已初始化) | 本程序 rw 文件段 | static 只是可见性,落位同全局 |
| 栈变量 / 栈数组 | [stack] | — |
| `malloc(64)` / `new int` | [heap] | 小请求走 brk |
| `new char[4MB]` / `mmap(2MB)` | 匿名 mmap 区(库下方) | 大请求与 mmap 直配同居匿名区 |
| `&probe_target` / `&probe_anchor2` | 本程序 .text(r-x) | 函数指针指向的代码段 |
| `&printf` | libc .text | 库函数地址落在 libc 的可执行段 |

- **.bss 的两副面孔是本实验主发现**:教科书说「.bss 不占文件、运行时清零」,但只有超出文件 rw 段最后一页的部分才单独成匿名 VMA;开头若填进尾页剩余空间,maps 上与 .data 无法区分。
- 源码里 `g_bss_buf[512*1024]` 取中段,保证必然落在匿名接续段(起点会随链接布局漂移,故起点一项的预期写成两可,实测两种落位都合法)。

## 复现

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 02-var-locations.cpp -o 02-var-locations
./02-var-locations | tee 02-var-locations.out
```

输出三节:变量地址清单、对表(地址→段→预期→判定)、同次运行 maps 原文。
