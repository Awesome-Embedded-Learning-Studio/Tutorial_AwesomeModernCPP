# 01-maps —— /proc/self/maps 全图解剖(E1)

环境:WSL2 内核 6.18.33.2,glibc 2.44,g++ 16.2.1 `-std=c++20 -Wall -Wextra -Wpedantic -O2`(零警告)。程序运行中读自己的 `/proc/self/maps`,先整表打印原文,再逐段给出归类与段大小,最后统计权限组合与总段数。

## 结论(对照 `01-maps.out`,共 41 段)

单个 C++ 程序一次运行的全部地址空间,按地址从低到高分六类:

| 大类 | 段数 | 内容 |
|---|---|---|
| 本程序 | 5 | `r--p`(ELF 头+链接器元数据)→ `r-xp`(.text)→ `r--p`(.rodata)→ `r--p`(GNU_RELRO)→ `rw-p`(.data+.bss 前半) |
| [heap] | 1 | brk 管辖的堆,204 KB(启动时 iostream 已分配过) |
| 共享库 | 26 | libc/libm/libstdc++/libgcc_s/ld.so 各 5 段 + 各自的 .bss 接续匿名页 + `/etc/ld.so.cache` 一段 |
| 匿名 | 5 | 无文件背书的 rw 段(含各库 .bss 接续页) |
| 栈 | 1 | [stack] 136 KB |
| vdso 族 | 3 | [vvar]、[vvar_vclock]、[vdso] |

各观察点:

- **权限组合统计**:`r--p`×21、`r-xp`×7、`rw-p`×13,总 41 段。只读段是主体——地址空间里大部分 VMA 是各种元数据与 rodata,可执行段只有 7 个(程序+5 个库+vdso)。权限第 4 位 `p`/`s` 是私有(COW)/共享,本例全部为 `p`。
- **每个文件映射模块出现两个 `r--p` 段**:偏移居中的是 .rodata;紧贴 `rw-p` 段的那个是 **GNU_RELRO**——本来在 rw LOAD 里(.got/.dynamic),动态链接器做完重定位后 mprotect 成只读,maps 里因此裂成两段。
- **rw 段后紧跟的匿名 rw 段 = .bss 接续页**:ELF 的 rw LOAD 段 memsz 超出 filesz 的部分(.bss)由内核用匿名页补齐,如 libc 的 `74f99621b000-74f996223000`(32KB)。文件 rw 段尾页里也能藏 .bss 开头几个字节(02-var-locations 实测)。
- **库映射顺序与链接顺序一致**(libc→libm→libstdc++→libgcc_s),`ld.so.cache` 也映射进来,随后是 ld.so 自己。
- **本机内核的 vvar 家族多一个 `[vvar_vclock]`**(老内核只有 [vvar],拆分引入的版本未考);`[vsyscall]` 在 WSL2 上不存在(内核配置 LEGACY_VSYSCALL_NONE),x86_64 老资料里的 `--xp ffffffff...600000 vsyscall` 段看不到。

## 复现

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 01-maps.cpp -o 01-maps
./01-maps | tee 01-maps.out   # .out 是 2026-10-03 在本仓库目录内运行的原样捕获
```

每次运行地址不同(ASLR,见 05-aslr),段数与结构不变。
