# 03-seh-veh 配套实验

《结构化异常:SEH 与 VEH》的实验代码与原始输出存档。源文件与文章代码块一一对应(`eN_*.cpp`),`matrix/` 里是编译器矩阵与 ABI 取证用的探针,`.out` 全部是当轮机器的原始捕获(含命令行回显,`$` 开头的行是当时敲的命令)。

## 环境

- Windows 11 26200(26H2 线),NTFS,系统盘 C:
- MSYS2 UCRT64 g++(Rev 5)16.1.0,x86_64-w64-mingw32,动态链接(依赖 ucrtbase.dll / libgcc_s_seh-1.dll / libstdc++-6.dll)
- 编译:`g++ -std=c++20 -Wall -Wextra [文件] -o xxx.exe`(个别加 `-O2`/`-static`/`-fno-exceptions`,见矩阵表)
- 崩溃退出码必须用 `cmd /v:on /c "xxx.exe < nul & echo !ERRORLEVEL!"` 读:WSL 直跑 `$?` 是 wait status 低 8 位,0xC0000005 截成 5,读不出全码

## 前提事实:MinGW g++ 没有 __try/__except

GCC 至今没有实现 MS 扩展关键字(`__try`/`__except`/`__finally`),`-fms-extensions` 也不解锁(`matrix/m1_keywords.out`)。本目录所有 SEH 实验走的是 mingw-w64 `<excpt.h>` 自带的**内部宏** `__try1(filter)` / `__except1`:它用内联汇编在当前函数里拼出 `.seh_handler __C_specific_handler` 加一张单条目作用域表。由此带来的用法约束全部实测过,见下表(m0b 探针没有独立的 .out,输出寄居在 `m0_filter_abi.out` 的后半段)。

### 编译器矩阵(全部实测,结论按本机)

| 场景 | 结果 |
|---|---|
| `__try`/`__except` 关键字,任意 flags | 编译不过:`'__try' was not declared in this scope` |
| `__try1` 宏,普通函数,-O0/-O2 | 正常(filter 进、处理块落地)|
| `__try1` 写在 `main` 里 + `-O2` | 汇编报错:`.seh_endproc used in segment '.text' instead of expected '.text.startup'`(GCC 把 main 放进 `.text.startup`,宏里写死 `.text` 切不回去)|
| 同一函数里既有 `__try1` 又有带析构的 C++ 对象,-O0 | **编译通过但运行翻车**:filter 一次都不跑,进程以自己 raise 的 0xE0001111 收场(WSL `$?`=17 是它的低 8 位;cmd `ERRORLEVEL=-536866543` 也是它,2026-10-02 走查复验。早先注记的 0xC0000409 归属无法复现,已撤——`m2_mixed.out` 第 4 行括注是当时的误记,保留原文不改。MSVC 对应场景是编译期 C2712 拒绝;GCC 编译期不做任何检查)|
| 同上,-O2 | 汇编报错(`.text` 与 `.text.startup` 相减)|
| 受保护区拆进独立 `noinline` 函数(外层函数照常有析构),-O0 | 正常 —— MSVC C2712 的官方解法在 MinGW 同样成立 |
| 同上,-O2 | **代码生成抽奖**:filter 跑了但落地即崩(0xC0000005);处理块里随手加一句 `printf` 改变代码生成就又活了。机制是 GCC 不知道控制流可以空降到 `__except1` 标签,优化器在两个 asm 边界附近怎么排布代码都不违约。详见 `matrix/m10_wrapper.out` |
| 一个翻译单元用两次 `__try1` | 汇编报错:`symbol '.l_startw' is already defined`(宏里标签名写死,一个 TU 只能有一个受保护区)|
| 受保护体内 `return` / `goto` 跳出(无条件的 `throw` 同命) | 后面的 `__except1` asm 被判不可达直接删掉,链接期 `.l_endw` undefined。体内必须留得到直落路径,用 faulted 标志分岔(每个 e*.cpp 里的惯用法)|
| `-fno-exceptions` / `-static` | `__try1` 照常工作;`-static` 仍经 api-ms-win-crt-* 落到 ucrtbase,行为不变 |

## 独家发现:UCRT64 的 filter ABI 与经典写法不同

`.seh_handler __C_specific_handler` 里的 `__C_specific_handler` 符号,在 UCRT64 工具链上由链接器解析到 **ucrtbase.dll 的导出**(不是 ntdll 的),取证在 `matrix/m0_filter_abi.out` 的后半段(m0b):filter 的返回地址落在 ucrtbase.dll。反汇编 ucrtbase 该处(`0xB1D18`):

```asm
lea  0x30(%rsp),%rcx     ; 第 1 参不是裸的 PEXCEPTION_RECORD!
call *%rax               ; 调作用域表里的 filter
test %eax,%eax
js   ...                 ; 负数 → 恢复执行
jle  ...                 ; 0 → 继续上找
cmpl $0xe06d7363,(%rsi)  ; 正数 → 执行处理块(顺带特判 MSVC 的 C++ throw 码)
```

实测 filter 四参(`matrix/m0_filter_abi.out`,与 VEH 看到的指针逐一对上):

| 位置 | 内容 |
|---|---|
| RCX | `EXCEPTION_POINTERS` 形状的栈结构:`[0]`=PEXCEPTION_RECORD,`[1]`=PCONTEXT |
| RDX | EstablisherFrame |
| R8 | PCONTEXT(与 `[1]` 相同)|
| R9 | DISPATCHER_CONTEXT(ControlPc/ImageBase/FunctionEntry/... 逐字段验证过)|

filter 返回值:正数(1)= 执行处理块;0 = 继续上找(m11:无人接则裸崩);负数 = 恢复执行(m9b:回到 `ctx->Rip` 重放)。注意 m9b 里对 `RaiseException` 恢复执行的效果是"它正常返回了"——因为记录的故障地址是 kernel32 trampoline 里的返回地址,不是你代码里的 call 指令。

另:ucrtbase 的这个 handler 里硬编码了 `0xE06d7363`(MSVC 的 C++ throw 码)做特判——这是 MSVC 码在本机的静态证据;但 MinGW 的 throw 用的根本不是这个码,见 05。

## 目录与实验对照

| 目录 | 实验 | 一句话结论 |
|---|---|---|
| `01-try-except/` | E1a AV 解码 | `e1_av_decoding.out`:读 NOACCESS 页 info[0]=0、写 info[0]=1、跳去执行数据页 info[0]=8(DEP),info[1] 是**字节级**精确地址(不是页首);读 NULL 时 info[1]=0。注意 **info[0] 是 0=读/1=写/8=DEP 执行**,不是"8=写"(Microsoft Learn EXCEPTION_RECORD 页口径,实验对得上)|
| | E1c 异常码来源 | `int3` → 0x80000003(nparams=1);`RaiseException(0xE0001234,0,4,args)` 自定码+4 个参数槽原样穿透,flags=0x80(RaiseException 打的软件来源标记,winnt.h 没这个常量,nynaeve.net ?p=99 有记);除零 → 0xC0000094 |
| `02-in-page-error/` | E2 SIGBUS 分叉 | **Windows 侧的答案:本地造不出这个触发器。**映射活着时 `SetEndOfFile`/`FileEndOfFileInfo`/`FileAllocationInfo` 全部被拦,`ERROR_USER_MAPPED_FILE`(1224);`UnmapViewOfFile` 了但映射对象句柄没关,照样拦。解除映射+关句柄再截才成功。之后在 1 页文件上盖 3 页映射读第 1/2 页:**零填充,无异常**(Linux 这里是 SIGBUS);写方向(`e2b`)写进零填充页,FlushViewOfFile 后文件自己长回 3 页。0xC0000006 与 info[2] 的 NTSTATUS 只在分页 I/O 真失败时出现(网络断连/介质弹出/磁盘错误),本机无可移动介质、无管理员权限建回环共享,未复现——`e2_in_page_error.out` 里 filter 全程待命零触发,这个"零"就是结论 |
| `03-veh/` | E3 VEH | `AddVectoredExceptionHandler(1,h)` 头插:**后注册的先跑**(veh2→veh1→veh3(First=0 尾插));完整时序两幕:有 SEH 时 VEH×3→SEH filter→处理块;无 SEH 时 VEH×3→UEH→进程收场(UEH 后 main 里下一行不执行)。`e3_continue`:VEH 里 `VirtualProtect` 修回可写 + `EXCEPTION_CONTINUE_EXECUTION` → 同一条写指令重放成功(读回 0x1234);不修现场硬 CONTINUE_EXECUTION → 同一错原地打转(三次后放行去崩)|
| `04-unhandled/` | E4 未处理路径 | 裸 0xC0000005 退出码 = **异常码本身**(cmd 里 -1073741819 = 0xC0000005 的有符号写法);UEH 返回 EXECUTE_HANDLER 后进程**仍以 0xC0000005 收场**(handler 不是"处理完就翻篇");SEM_NOGPFAULTERRORBOX 只压错误框不改码。WER:本机 DontShowUI 无覆盖,但控制台进程 stdio 重定向下裸崩未观察到弹窗,即刻退出(`< nul` 防 WER 卡死,跑批见 `run_exitcodes.bat`)|
| `05-cpp-eh/` | E5 throw×SEH | MinGW 的 throw **走 SEH 码路径**(VEH 看得见,exe 带 .pdata/.xdata,libgcc_s_seh-**1**.dll 里 `mov $0x20474343` 紧挨 `call RaiseException`/`RtlUnwindEx`),但异常码是 **0x20474343('GCC')不是 0xE06D7363('msc')**,且只带 1 个参数(MSVC 是 3 个:构造副本/析构/类名)。每次 throw 恰好一次分发,catch 不再产生异常。`e5_swallow`:SEH 作用域把穿越的 C++ 异常吞了 → catch 等不到这一票、进程继续活;throw 直接写在 `__try1` 同函数受保护体内:无条件形态与 return/goto 同命(链接期 `.l_endw` undefined);可链接的条件形态(保住直落路径)实测异常直接穿帧、filter 不被咨询、外层 catch 正常接到(2026-10-02 走查复验;早先注记的 0xC0000409 fail-fast 说法无法复现,已撤,`e5_swallow.cpp` 第 32-34 行注释是当时的误记)| |
| `06-console-event/` | E6 Ctrl+C | Ctrl+C/Break 是**控制台事件**不是异常:VEH 零分发;ctrl handler 在一个**新线程**里跑(handler 日志 thread id 可证)。CTRL_BREAK_EVENT(=1)必达;CTRL_C_EVENT(=0)本机收不到,根因是 WSL interop 启动链继承的忽略位,`SetConsoleCtrlHandler(NULL,FALSE)` 解除后定向发送实收,与窗口真假、输入模式都无关(机制与实测见 `../../process/02-console-apc` 存档,`run_e6.bat` 用 `start /wait` 开新窗口)|

## 复现

```sh
# 单发(在 03-seh-veh 对应子目录里;g++ 是 /mnt/c/msys64/ucrt64/bin/g++.exe)
g++ -std=c++20 -Wall -Wextra e1_av_decoding.cpp -o e1.exe && ./e1.exe

# E4 退出码(必须走 cmd 读 ERRORLEVEL,见 04-unhandled/run_exitcodes.bat)
cmd /v:on /c "e4_bare.exe < nul & echo !ERRORLEVEL!"

# E6 要真控制台窗口
start /wait cmd /c "e6_ctrl_c.exe > e6_out.txt 2>&1"
```

## 复跑注意

- **路径是烧死的**:e4/e6 的可执行产物与日志按 `C:/msys64/tmp/` 写(`e6_ctrl_c.cpp` 里的 `LOG` 常量、两个 bat);e2 系列的数据文件写进 `%TEMP%`,跑完自删。
- **源码全部按 `-O0` 口径跑**(e*.cpp 主实验都是);`-O2` 只在 `matrix/` 里做对照,别拿 `-O2` 复跑主实验——见矩阵表里的"代码生成抽奖"。
- 输出里的地址(栈/模块/堆)全吃 ASLR,与你的机器不同是正常的;文章引的是规律(如 info[1] 的字节级精确、时序顺序),不是具体数值。
- `.out` 是本轮(2026-10-02)捕获,程序里都先 `setvbuf(stdout, NULL, _IONBF, 0)`:崩溃前的输出不经缓冲,该在的都在。

## 参考

- [EXCEPTION_RECORD(Microsoft Learn)](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-exception_record) —— info[0] 的 0/1/8、IN_PAGE_ERROR 的 info[2] NTSTATUS 口径
- [golang/go#58457](https://github.com/golang/go/issues/58457) —— mmap 视图上的 0xC0000006(触发条件:外部磁盘弹出后**读**,现场断网/断介质,不是本地截短)
- mingw-w64 `excpt.h` 源码(本机 `/ucrt64/include/excpt.h`)—— `__try1`/`__except1` 宏、x64 分支的 `.seh_handler` 序列
