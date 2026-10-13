# 04-abstraction-design 配套实验

《平台抽象层设计:从 #ifdef 到 concepts》(vol8 systems-programming ch07 第 01 篇,收卷篇)的实验代码与原始输出存档。核心问题一句话:**平台差异这层活,#ifdef 散在函数体里、concepts 立在类型边界上,各自的边界行为是什么,分派成本又差多少**。

目录命名:跟随 cross-platform/ 站内的序号(03 是 ch04 的收章篇,04/05 是 ch07 的两篇)。若成文时文档 slug 变了,这里跟着改名即可,内容不依赖目录名。

## 与相邻篇的分工

- [03-cross-async-io](../03-cross-async-io/) 是本篇的引子:那边把 concepts 的思路用在异步后端这一种资源上(AsyncBackend,五行约束、负例诊断、`request.target` 拿 uintptr_t 糙着承载),本篇把同一思路扩到全部资源(平台检测、字节源、句柄统一承载、错误模型),并把 uintptr_t 的口收进 e3。
- 思维基石两篇(unique_fd/unique_handle/sys_call 的唯一定义处)是工具底座,本篇不重新教 RAII 与错误装箱,只在 e4 借 `sys_call` 的口径做基准锚。
- Asio/libuv 的借鉴在正文只到「接口形状」层:io_context 的 backend 概念(运行期可换后端+编译期类型检查的混合)、libuv 的跨平台句柄封装(数据+统一生命周期),内部实现不展开。

## 环境口径

| 侧 | 系统 | 编译器 | 命令 |
|---|---|---|---|
| Linux | WSL2 6.18.33.2-microsoft-standard-WSL2 | g++ (GCC) 16.2.1 | `g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2` |
| Windows | Win11 26200(WSL interop 调起) | MSYS2 UCRT64 g++ (Rev5) 16.1.0 | `/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -O2 -Wall -Wextra` |

全部输出捕获自 2026-10-05 的同一轮。两侧编译零警告(唯一例外是 e4 的 -O0 对照版,那版本来就不做优化)。

## 文件与结论对照

| 实验 | 文件 | 一句话结论 |
|---|---|---|
| e1 平台检测三路 | `e1_platform_probe.cpp` + `e1_linux.out` / `e1_windows.out`;`e1_cmake_probe/`(含 `logs/cmake_probe_scenes.txt`) | 预定义宏回答「为哪个目标编译」(Linux 侧 `__linux`/`__GLIBC__` 定义、`_WIN32` 缺席;Windows 侧 `_UCRT`/`__MINGW64__` 定义、`__GLIBC__` 缺席——同一家 GCC,C 库不同宏就不同);`__has_include` 查头不查能力(`<liburing.h>` 在,说明的是用户态库装了,内核支不支持它不管);CMake 配置期知道的最多,但**裸 `CXX=g++.exe` 覆盖时 CMAKE_SYSTEM_NAME 被认成 Linux、WIN32 空、UNIX=1,而 check_include_file 的 try_compile 反而全对**——平台身份与编译能力是两条线,交叉场景要靠 toolchain 文件声明目标(完整修法在 05-syskit 的 e3) |
| e2 甲场:#ifdef 分发 | `e2_ifdef_reader.cpp` + 两侧 `.out` | 同一个函数两个世界,两侧各编一份各跑通,读回内容与缺失文件的错误值(两侧恰好都是 2)对上 |
| e2 乙场:死分支藏错 | `e2_ifdef_dead_branch.cpp` + `e2_dead_linux.out` / `e2_dead_windows.err` | Windows 分支藏三处错误(拼错的 ReaddFile/DWORDD、参数个数错的 CloseHandle),**Linux 侧编译零警告零错误通过**——预处理裁掉的代码不编译,错误可以沉睡到有人把代码带到那个平台;Windows 侧编译三处全现形,诊断逐字在档 |
| e2 丙场:concepts 静默多态 | `e2_concept_reader.cpp` + 两侧 `.out` | 四行一条约束的 ByteSource + MemSource/NativeSource 两后端,#ifdef 只活在「选后端」的边界(一处 using),实现内部零分支;受约束的 drain 一份代码静默适配全部后端;static_assert 让每个后端无论用不用都被检查 |
| e2 丁场:约束负例 | `e2_concept_negative.cpp` + 两侧 `.err` | 缺 read_some 的 ShortSource:static_assert(!ByteSource)编过(concept 如实答不满足),递给受约束函数则编译失败,两侧诊断都精确到 `the required expression 's.read_some(buf, n)' is invalid`——与死分支的「零诊断」对照,拦截点是确定的、点名的 |
| e3 native_handle 桥接 | `e3_native_handle_bridge.cpp` + `e3_linux.out` / `e3_windows.out` | 收 03 篇留下的 uintptr_t 的口:intptr_t 统一承载两侧原生句柄往返无损(Linux fd=3 往返+经 void* 中转还原后真能读;Windows HANDLE=0xb0 往返+**CRT 真桥 `_open_osfhandle` 把 HANDLE 配成 fd=3、`_read` 读通、`_get_osfhandle` 还原判等**);fd 是 4 字节 int 装进 8 字节 intptr_t 高位为零,截断还原无损 |
| e4 虚表 vs 模板基准 | `e4_vtable_vs_template.cpp` + `e4_linux_o2.out` / `e4_linux_o0.out` / `e4_windows.out` / `e4_disasm_snippet.txt` | 防去虚化设计(两个派生类轮流指+noinline 消费函数):间接虚调用**最好档约 1ns(分支预测命中,与普通调用打平)、最坏档约 5ns(mt19937 乱序,预测失效)**,对一次真系统调用(140-157ns)占比 1%-4%——分派成本不该是选型主因;汇编证据显示 GCC 16 -O2 对封闭层级做了**投机去虚化**(先比对两个候选地址直接内联,猜不中才走 `jmp *%rax` 兜底),这解释了最好档为什么打平 |

## 独家发现(写手可以直接展开的)

1. **e1 的「身份与能力」两线分离**:裸 `CXX=g++.exe` 配置下 CMake 把平台身份全认错(UNIX=1),但 `check_include_file` 全对(HAVE_WINDOWS_H=1)——因为 try_compile 用的是真编译器。平台的「身份变量」跟宿主走,「能力探测」跟编译器走,两条线在交叉场景里会分叉。
2. **e1 的返工教学点**:实验文件两侧同一份的初心,被 `<sys/utsname.h>` 在 Windows 上的缺席顶了一次回,修法是三行 `#ifdef` 分流运行期取信息的实现——这正是本篇的立场:#ifdef 不是有罪,是该退到边界(贴着平台分流实现),而不是铺满业务逻辑。另实测 MSYS2 的 libstdc++ 在 os_defines.h 里已预定义 NOMINMAX,再手动定义会触发 redefined 警告,要加 #ifndef 守卫。
3. **e4 的 Windows 侧 getpid 假锚**:UCRT 的 `getpid()` 计时只有 1.3ns(纯用户态,疑从 PEB 读缓存的 pid,官方文档未明说此行为),拿它当「系统调用成本锚」会量出假零;换必进内核的 `GetProcessHandleCount` 才回到 157ns,与 Linux 侧 getpid 的 136-144ns 同量级。ch00 总纲讲过 glibc 2.3.4-2.24 的 getpid 缓存历史,这里是 Windows 侧的同型现场(且至今如此)——**跨平台抽象连「基准锚」都要按平台换手**。
4. **e4 的投机去虚化**:consume_virtual 的汇编里,GCC 先取 vtable 槽、与两个 final 派生类的函数地址逐一比对,猜中就直接内联该实现返回,都没猜中才 `jmp *%rax`。热点路径根本不走间接跳转——「虚函数慢」这句话在封闭层级+现代 GCC 下要改写成「预测命中时与直接调用打平,预测失效时约 5ns,且都远小于一次系统调用」。

## 复现

```bash
# Linux 侧(e1-e4 通用)
g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 e1_platform_probe.cpp -o e1 && ./e1
g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 e2_ifdef_reader.cpp -o a && ./a
g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 e2_ifdef_dead_branch.cpp -o b && ./b   # 预期通过(错误在死分支里沉睡)
g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 e2_concept_reader.cpp -o c && ./c
g++ -std=c++20 -Wall -Wextra -c e2_concept_negative.cpp    # 预期编译失败,看诊断
g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 e3_native_handle_bridge.cpp -o d && ./d
g++ -std=c++20 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 e4_vtable_vs_template.cpp -o e && ./e
# e1 第 3 路(CMake)
cmake -S e1_cmake_probe -B e1_cmake_probe/build-linux && cmake --build e1_cmake_probe/build-linux && ./e1_cmake_probe/build-linux/probe_main

# Windows 侧(WSL interop;产物要 chmod +x 再跑)
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -O2 -Wall -Wextra e1_platform_probe.cpp -o e1.exe && ./e1.exe
# 其余同款替换;e2_ifdef_dead_branch 预期编译失败,诊断在 e2_dead_windows.err
```
