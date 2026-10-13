# 05-aslr —— ASLR 随机化观察(E5)

环境:同总 README。程序每次运行打印一行:`exe`(PIE 加载基址)/`heap`([heap] 起点)/ `stack`(起止)/ `libc`(基址)/ `vvar` / `vdso`。捕获口径:普通连跑 5 次 + `setarch -R`(ADDR_NO_RANDOMIZE)连跑 5 次,全部在 `05-aslr.out`。

## 结论(对照 `05-aslr.out`)

**ASLR 开(普通 5 次)**——每次全变:

| 字段 | 5 次观测跨度(本档 .out) | 理论上限 |
|---|---|---|
| exe 基址 | ~13.9 TiB(0x558f…–0x6371…) | 32 位页熵 ×4KiB = 16 TiB |
| heap 起点 | ~13.9 TiB(跟着 exe 走,间隔随机约 62MB–1GB) | brk 随机化叠加在 exe 之上 |
| stack 起点 | 7.09 GiB(顶端在 0x7ffc…–0x7ffe… 之间) | 22 位页熵 ×4KiB = 16 GiB |
| libc 基址 | ~15.5 TiB(0x7075…–0x7fe7…) | mmap 区域 32 位页熵 = 16 TiB |

vvar/vdso 跟随 mmap 区基址,与 libc 同进退。

**ASLR 关(setarch -R 5 次)**——五次逐字节全同,且落在「教科书地址」上:

```
exe=555555554000 heap=55555555b000 stack=7ffffffdd000-7ffffffff000 libc=7ffff7800000 vvar=7ffff7fb6000 vdso=7ffff7fbc000
```

- exe 回到 PIE 默认基址 0x555555554000,**heap 紧贴程序映像**(间隔 0x7000,无随机 gap);开着 ASLR 时 heap 与 exe 之间隔几百 MB 随机量。
- 栈顶固定 0x7ffffffff000,libc 固定在 0x7ffff7800000——这正是 gdb 默认看到的世界(gdb 默认关闭随机化),也是各种教程里地址「长得整整齐齐」的原因。
- `[stack]` 在 maps 里的对比:开 ASLR 时起点每次不同(见上表跨度),关 ASLR 时五次同为 `7ffffffdd000`。

## 复现

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 05-aslr.cpp -o 05-aslr
{ for i in 1 2 3 4 5; do echo "== ASLR 开 · 第 $i 次 =="; ./05-aslr; done
  for i in 1 2 3 4 5; do echo "== setarch -R(ASLR 关)· 第 $i 次 =="; setarch -R ./05-aslr; done
} | tee 05-aslr.out
```
