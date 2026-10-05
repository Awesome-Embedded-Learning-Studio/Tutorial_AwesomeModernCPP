---
title: "结构化异常:SEH 与 VEH"
description: "Windows 侧访问出错的完整机制链。从上一篇写只读视图收到的 0xC0000005 接起:异常记录的实测口径 info[0] 0=读/1=写/8=DEP 执行(8 不是写)、info[1] 精确到字节;VEH 头插链序与修现场后 CONTINUE_EXECUTION 同一条写指令重放成功、不修现场则原地打转;MinGW 没有 __try/__except 关键字,唯一活路是 excpt.h 的 __try1/__except1 宏,独家实测 UCRT 变体 filter ABI(第 1 参是 EXCEPTION_POINTERS 形状)、析构混用编译期零检查、-O2 代码生成抽奖;没人处理时退出码就是异常码本身;IN_PAGE_ERROR 的剧本被 ERROR_USER_MAPPED_FILE 整个拦下、越文件尾读到的是零填充(Linux 同场景是 SIGBUS),本地复现不了这件事本身就是答案;MinGW 的 throw 走 0x20474343 不是 0xE06D7363,VEH 看得见 throw,SEH 作用域截得住穿越的 C++ 异常"
chapter: 8
order: 3
platform: host
difficulty: advanced
cpp_standard: [20]
reading_time_minutes: 24
prerequisites:
  - "文件映射:CreateFileMapping 与 MapViewOfFile"
related:
  - "错误处理范式:从 errno 到 expected"
  - "mmap 内存映射:把文件贴进地址空间"
tags:
  - host
  - cpp-modern
  - advanced
  - 系统编程
  - Win32
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# 结构化异常:SEH 与 VEH

上一篇的收尾处，咱们对着 `FILE_MAP_READ` 的视图硬写了四个字节，VEH 只来得及替咱们记下一笔 `0xC0000005`，进程就当场收场了。当时咱们只说了半句:没人接住的结构化异常，会拿异常码本身当进程的退出码。这一篇就把欠下的机制链一次走完，这个码是谁定的、记录里还写了什么、谁排在前面谁能修现场、修完从哪儿继续、没人接的时候进程怎么个死法，咱们挨个交代清楚。Linux 侧的镜像问题也一并回答掉:同样的野指针与越界读，那边送来的是 SIGSEGV 与 SIGBUS，这边送来的却是异常码加分发链。

这一篇的主角是两位。主角里的头一位是 SEH(Structured Exception Handling，中文的名字叫结构化异常处理):CPU 的页错误、除零，加上软件自己用 `RaiseException` 主动抛出的错，在内核里汇成的，是叫异常记录(EXCEPTION_RECORD)的同一种东西，再由内核的分发器送回用户态，沿着咱们登记过的处理者一站一站找下家。`__try`/`__except` 是它在语言层面的脸面，MSVC 伺候了它三十年。另一位主角是咱们已经打过照面的 VEH(Vectored Exception Handling，中文的名字叫向量化异常处理)，注册函数 `AddVectoredExceptionHandler` 上一篇咱们就调过:一条进程级的链，排在所有 SEH 作用域的前面，咱们想最早看到异常，把函数挂上去就可以了。它俩加上收尾的 UEH(未处理异常过滤器，`SetUnhandledExceptionFilter` 装上的最后一站)，凑成的就是 Windows 侧访问出错这件事的全部出场顺序。

编号的口径咱们交代在前面，免得您翻档案的时候对不上号。本篇的实验按 e1 到 e6 编号，小写的 e 跟着存档的六个子目录走，收在 `code/volumn_codes/vol8/systems-programming/windows/file-io/03-seh-veh/` 的下面，`e1_av_decoding.cpp` 这样的文件名就是编号本身。thinking 篇的 Windows 实验同样用小写 e 续排，那边的编号与本章文件 I/O 的编号互不相干，您认文件名就不会认错人。环境还是咱们熟悉的老一套:Win11 26200 的本机，MSYS2 UCRT64 的 g++ 16.1.0(跨系统调起它的 interop 链路，W01 讲过了)，编译命令是统一的一条:`g++ -std=c++20 -Wall -Wextra e*.cpp -o e*.exe`。主实验全按无优化的口径跑，`-O2` 的角色只在编译器矩阵里当对照组，理由到了讲矩阵的小节您就明白了。实验登场的次序咱们也交代一下:正文里出场的是 e1、e3、e4、e2、e5、e6，编号认的是存档、不是出场的次序，与 thinking 篇的处理是同一个做法。读输出块的时候还有一处小区别:矩阵 m 系的捕获连命令行都是当时的原文，e 系的原始捕获不带命令行，后文 e 系块头上的 `$` 命令行，是咱们按存档 README 的复现命令补写的回显。输出的捕获日期是 2026-10-02，地址类的数字全吃 ASLR，您复跑时长得不一样才是正常的。

## MinGW 没有 __try，只有一条内部宏的活路

咱们要动手读异常记录以前，手头得摆着一个 SEH 的作用域，麻烦偏偏出在这里。您要是照着 MSVC 的文章把 `__try { ... } __except (...) { ... }` 复制过来，GCC 当场就把话堵死了:

```text
$ g++ -std=c++20 ... m1_keywords.cpp(__try/__except 关键字)
probe_try.cpp: In function 'int main()':
probe_try.cpp:7:5: error: '__try' was not declared in this scope
    7 |     __try {
      |     ^~~~~
probe_try.cpp:9:7: error: '__except' was not declared in this scope
    9 |     } __except (EXCEPTION_EXECUTE_HANDLER) {
      |       ^~~~~~~~
compile exit=0
```

(存档的原文就是这样，探针当时的源文件叫 probe_try.cpp，命令行里的 flags 记成了省略号，末行的 compile exit=0 是捕获脚本自己的退出码，不是编译器的。)这套语法里的三个关键字，GCC 到 16.1.0 的实现里根本没有，`-fms-extensions` 的开关也帮不上忙，认得它们的只有 MSVC 与 Clang。剩下的活路只有一条，藏在 mingw-w64 的 `<excpt.h>` 里:一对叫 `__try1(filter)` 与 `__except1` 的内部宏。它用内联汇编在当前函数里拼出 `.seh_handler __C_specific_handler` 加一张单条目的作用域表，filter(过滤函数)就是咱们传给 `__try1` 的那个函数，异常落进作用域的时候，头一个表态的就是它，它的返回值决定接下来是执行处理块还是继续上找。宏的身份是内部货色，带来的约束还真不少，咱们把实测出的用法边界留到后面矩阵一节统一过，这里您只需要认下一件事:本篇所有 SEH 实验走的都是这对宏。

接下来是笔者要专门多写一段的地方，因为这里踩中的，是一处几乎没人记载的 ABI 分岔。经典的资料(比如 nynaeve.net 那套逐篇分析 SEH 的文章)都告诉您，`__C_specific_handler` 调 filter 的时候，第一个参数是裸的 `PEXCEPTION_RECORD`。咱们在本机实测的结果不是这样:那个符号在 UCRT64 的链接里，被解析到了 **ucrtbase.dll 的导出**(ntdll 其实也导出同名函数，可链接器挑了前者)。而 ucrtbase 自己的那套实现调 filter 时，往 x64 的第一个参数寄存器 RCX 里放的，是一个 `EXCEPTION_POINTERS` 形状的栈结构，`[0]` 装的是记录指针、`[1]` 装的是上下文指针。取证是两步走的:filter 的返回地址落在 ucrtbase.dll 的地界(取证来自 `01-try-except/matrix/` 的 m0b 探针，它的输出收在 `m0_filter_abi.out` 的后半段，探针的住处矩阵一节还会正式交代)，反汇编 ucrtbase 的对应位置，看到的正是 `lea 0x30(%rsp),%rcx` 紧挨着 `call *%rax`，它是在栈上拼好了那个结构以后，才把地址放进 RCX 的。

四个参数咱们逐一对过:RCX 装的是这个结构，RDX 给的是 EstablisherFrame(咱们这个 filter 所在栈帧的基址，分发器递进来的登记信息，咱们用不上)，R8 给的也是 PCONTEXT(内容与 `[1]` 相同)，R9 给的是 DISPATCHER_CONTEXT(分发器自己的上下文记录，咱们同样用不上)。所以本篇所有 filter 的原型都写成 `void*` 起头，进去以后把它转成 `PEXCEPTION_POINTERS` 再用就行了。您要是按经典写法把第一参当 `PEXCEPTION_RECORD` 用，当头读到的所谓 ExceptionCode，其实是记录指针的低 32 位，从头一个成员起就全歪了。

## 异常记录 0xC0000005:info[0] 的 8 是 DEP

现场的材料都在 `EXCEPTION_RECORD` 里，字段咱们按用处挑着说。`ExceptionCode` 装的是异常码，`0xC0000005` 就是 `STATUS_ACCESS_VIOLATION`(访问违例)的号码，它属于内核的 NTSTATUS 编号体系，与 GetLastError 那套 Win32 错误码是两套不相干的数字，1224 是 Win32 那边的编号，0xC0000006 是 NTSTATUS 这边的号码，您可别把它们抄进同一张表。`ExceptionAddress` 是故障指令的地址。`ExceptionInformation`(下面简称 info)是附带的参数槽，在访问违例的名下，Microsoft Learn 的 EXCEPTION_RECORD 页写得明白:头一个元素给的 0 是读、1 是写、8 是 DEP 违例，第二个元素给出不可访问数据的虚拟地址。值得您多看一眼的是那个 8:它标记的是 DEP 违例(Data Execution Prevention，中文的名字是数据执行保护)，而不是写这个动作。咱们起手跑一遍，四种现场全拍下来(节选自 e1 的 `e1_av_decoding.cpp`，main 里排的就是四次 `probe` 调用，目标分别是一页 `PAGE_NOACCESS`、它加 0x1234 的偏移、空指针、还有一页可读写但不可执行的数据页):

```cpp
// UCRT64 实测:filter 由 ucrtbase.dll 的 __C_specific_handler 调用,
// 第 1 参不是裸的 PEXCEPTION_RECORD,而是 EXCEPTION_POINTERS 形状 {record, context}
extern "C" __attribute__((used)) EXCEPTION_DISPOSITION av_filter(void* ep, void* frame,
                                  PCONTEXT ctx, void* disp) {
    PEXCEPTION_POINTERS pointers = (PEXCEPTION_POINTERS)ep;
    PEXCEPTION_RECORD r = pointers->ExceptionRecord;
    printf("  [SEH filter] code=0x%08lX flags=0x%08lX fault-at=%p\n", ...);
    printf("    info[0]=%llu (%s)  info[1]=0x%llX\n", ...);
    return (EXCEPTION_DISPOSITION)1;   // 1 = EXCEPTION_EXECUTE_HANDLER:执行处理块
}

// 受保护区拆进独立函数:外层函数可以照常有析构对象(对应 MSVC C2712 的解法)
__attribute__((noinline)) void probe(int round, volatile int* target, int* result) {
    *result = 0;
    __try1(av_filter)
        if (round == 0) { *result = *target; }        // 读 NOACCESS 页
        else if (round == 1) { *target = round; }     // 写 NOACCESS 页
        else { using F = void(*)(); ((F)target)(); }  // 跳去执行数据页,DEP 违例
    __except1
        *result = -1;                                  // 接住后从处理块继续
}
```

```text
$ ./e1.exe
page=00000128a6bc0000 (PAGE_NOACCESS, size=0x1000)
== round 0: read page
  [SEH filter] code=0xC0000005 flags=0x00000000 fault-at=00007ff7a09815a3
    info[0]=0 (READ)  info[1]=0x128A6BC0000
-> result=-1
== round 1: write page
  [SEH filter] code=0xC0000005 flags=0x00000000 fault-at=00007ff7a09815ba
    info[0]=1 (WRITE)  info[1]=0x128A6BC1234
-> result=-1
== round 2: read NULL
  [SEH filter] code=0xC0000005 flags=0x00000000 fault-at=00007ff7a09815a3
    info[0]=0 (READ)  info[1]=0x0
-> result=-1
== round 3: call into PAGE_READWRITE page (DEP)
  [SEH filter] code=0xC0000005 flags=0x00000000 fault-at=00000128a6bd0000
    info[0]=8 (EXEC/DEP)  info[1]=0x128A6BD0000
-> result=-1
done
```

四轮的输出，咱们挨个看过去。round 0 与 round 1 这两轮的输出，把读与写分家了:info[0] 一轮给的是 0，一轮给的是 1。round 1 的 info[1] 给的是 `0x128A6BC1234`，页基址加上了 0x1234，精确到了**字节**这一级。咱们写的时候特意挑了个页内偏移，为的就是把这一点拍出来。对照 Linux 侧的时候，两边材料的分工几乎能一一对应上:info[1] 对应 siginfo 的 `si_addr`，fault-at 对应故障指令的 rip，而 Linux 用 `si_code` 分病因(`SEGV_ACCERR` 说的是权限不许、`SEGV_MAPERR` 说的是压根没映射，mmap 篇实测过的两码事)。Windows 这边没有平行的编码，权限错与未映射错共用的是 `0xC0000005` 一个码，咱们只能靠 info[1] 自己去对页表。

round 2 读的是空指针，info[1] 给的也是 0，这里没什么可说的。round 3 是 DEP 的现场:咱们把一页 `PAGE_READWRITE` 的数据页当函数调过去，CPU 取指的时候踩了不可执行的页，info[0] 给的是 8。这一轮还有一个旁证值得您留意:fault-at 打出来的是 `00000128a6bd0000`，它正好是数据页本身的地址，前三轮它给的都是咱们代码里故障指令的地址，这一轮却成了跳转的目标。取指违例里的故障指令，恰恰就是取不回来的指令本身。

异常码的表上还有别的来路，咱们顺手一并看了(节选自 `e1_custom_codes.cpp` 的三连发):

```text
$ ./e1c.exe
== int3
  [filter] code=0x80000003 flags=0x00000000 STATUS_BREAKPOINT (int3)  fault/raise-at=00007ff7d9661600 nparams=1
-> -1
== RaiseException
  [filter] code=0xE0001234 flags=0x00000080   fault/raise-at=00007fff83a341ca nparams=4
    info[0]=0x00000000AAAA1111
    info[1]=0x00000000AAAA2222
    info[2]=0x00000000AAAA3333
    info[3]=0x00000000AAAA4444
-> -1
== divide by zero
  [filter] code=0xC0000094 flags=0x00000000 STATUS_INTEGER_DIVIDE_BY_ZERO  fault/raise-at=00007ff7d9661665 nparams=0
-> -1
```

三行说的是各自的事情。`int3` 指令送来的是 `0x80000003`(STATUS_BREAKPOINT)，它是调试器的老朋友，nparams 给的是 1。整数除零走的是硬件异常，码给的是 `0xC0000094`，挂的名字是 STATUS_INTEGER_DIVIDE_BY_ZERO。中间的一行最有意思:咱们用 `RaiseException(0xE0001234, 0, 4, args)` 自己造了一个异常，自定的码原样进了记录，四个参数槽也是原样地穿透了，nparams 如实给的是 4，flags 里还多出了一个 `0x80`，那是 RaiseException 替软件来源打的标记，winnt.h 里甚至没有收录它的常量名(nynaeve.net 那边有考证)。raise-at 落在的是 `00007fff83a341ca`，不在咱们的代码里，因为软件异常的现场就在系统 DLL 里 RaiseException 的内部，这一点到了后面讲重放的时候，还会回来找咱们的麻烦。

## __try1 的用法边界:一张实测矩阵

宏的约束，笔者逐一跑实，全收进了下面的一张表(探针都挂在 e1 存档目录下的 `01-try-except/matrix/` 子目录里，攒了 11 个 .cpp，编号是跳着排的，跳过去的号没有归档):

| 场景 | 实测结果 |
| --- | --- |
| `__try`/`__except`/`__finally` 关键字，任意 flags | 编译不过，关键字不存在 |
| `__try1` 写在普通函数里，`-O0` 与 `-O2` | 正常，filter 进得去，处理块落地 |
| `__try1` 写在 `main` 里，加 `-O2` | 汇编报错:GCC 把 main 放进 `.text.startup`，宏里写死的 `.text` 切不回去 |
| 同一函数里 `__try1` 加带析构的 C++ 对象，`-O0` | 编译过，运行翻车:filter 一次都不跑，进程以自己 raise 的 0xE0001111 收场(WSL 的 $? 读到 17) |
| 同上，`-O2` | 汇编报错(`.text` 与 `.text.startup` 相减) |
| 受保护区拆进独立的 `noinline` 函数(外层照常有析构)，`-O0` | 正常，MSVC C2712 的官方解法在这里同样成立 |
| 同上，`-O2` | 代码生成抽奖:filter 跑了，落地即崩 |
| 一个翻译单元用两次 `__try1` | 汇编报错，`.l_startw` 重复定义 |
| 受保护体内写 `return`/`goto` 跳出 | `__except1` 的汇编被判不可达删掉，链接期 `.l_endw` 未定义 |
| `-fno-exceptions` 或 `-static` | `__try1` 照常工作 |

最伤人的是表里的第四行。在同一个函数的身体里，咱们既放了 `__try1` 又放了带析构的对象，GCC 这边倒是一句抱怨都没有:

```text
$ g++ -std=c++20 -Wno-unused-parameter m2_mixed.cpp -o m2 && ./m2
s=a string with dtor semantics
  [body] raising
exit=17
```

咱们的 filter 一次都没跑，进程就直接收了场，收场的码是它自己 raise 的 `0xE0001111`:WSL 的 `$?` 读到 17，正是这个码的低 8 位，cmd 里的 ERRORLEVEL 给的是 -536866543，同一个码的有符号写法，死法与后面 e4 的裸崩是同一套机制。存档注记当初把这个收场归给了 0xC0000409(STATUS_STACK_BUFFER_OVERRUN)，复验跑遍了各种形态都没等到它，咱们按复验的口径写。MSVC 对应的场景是编译期 C2712 当场拒绝(官方的解法就是把受保护区拆进别的函数)，GCC 这边编译期什么都不查，放行了事，真正的判决要等跑起来。拆函数的解法咱们已经在 e1 里用上了:`probe` 挂着 `noinline` 单独成了一个函数，析构的对象留给外层，`-O0` 的档位下是稳的。

表里 `-O2` 的几格里，拆函数加 `-O2` 的组合是抽奖，值得咱们单独看一眼:

```text
$ ./m10 -O0(受保护区拆进 noinline 函数:外层带析构也活)
  [filter] code=0xC0000005
guarded_probe -> -1 faulted=1 (expect -1/1)
s still alive: outer has dtors, fine
done
exit=0

$ ./m10 -O2(同样代码,-O2 代码生成抽奖:filter 跑了,落地翻车)
  [filter] code=0xC0000005
exit=5 (0xC0000005 截断)

$ ./m10b -O2(处理块里加一句 printf 改变代码生成,又活了)
  [filter] code=0xC0000005
  [handler] in-block
guarded_probe -> -1 faulted=1 (expect -1/1)
s still alive: outer has dtors, fine
done
exit=0
```

机制倒是不神秘:`__except1` 靠汇编标签落地，而 GCC 的优化器并不知道控制流可以空降到那个标签上，它在两个汇编边界的附近怎么排布代码都不算违约，排出来的指令流要是恰好把落地路径弄断了，它也是无从得知的。所以落笔的分寸是，受保护区拆进 `noinline` 函数、体内不写 return 用 faulted 标志分岔(每个实验源码里的惯用法)、优化级别压在 `-O0` 的档上，想要更稳的话，那就别用 `__try1` 了，咱们改用 VEH。

## VEH:排在所有 SEH 前面的链

`AddVectoredExceptionHandler(First, Handler)` 的 First 参数管插队:给非 0 插的是链头，给 0 排的是链尾，插链头的效果是后注册的排得更靠前。咱们拿三个 handler 把这句话跑实(节选自 `e3_chain.cpp`，filter 与 UEH 的定义从略，打印的语句压成了注释):

```cpp
AddVectoredExceptionHandler(1, veh1);
AddVectoredExceptionHandler(1, veh2);   // 插到 veh1 前面
AddVectoredExceptionHandler(0, veh3);   // 排到链尾

// 场景 A:受保护区里炸,SEH 接住
__attribute__((noinline)) void scene_a(volatile int* p, int* result) {
    int faulted = 1;
    __try1(chain_filter)
        *result = *p;
        faulted = 0;
    __except1
        if (faulted) *result = -1;
}

// 场景 B:没有 SEH,一路裸奔到 UEH
__attribute__((noinline)) void scene_b(volatile int* p, int* result) {
    *result = *p;
}
```

```text
$ ./e3_chain.exe
== 场景 A:AV + SEH __try1 在场 ==
  #1 VEH-veh2(后注册,First=1,插到 veh1 前面)
  #2 VEH-veh1(先注册,First=1)
     veh1 看到 code=0xC0000005
  #3 VEH-veh3(First=0,排链尾)
     SEH filter 看到 code=0xC0000005
  #4 SEH __try1 的 filter
  -> result=-1(SEH 处理块跑完)
== 场景 B:AV + 无 SEH,只有 UEH ==
  #5 VEH-veh2(后注册,First=1,插到 veh1 前面)
  #6 VEH-veh1(先注册,First=1)
     veh1 看到 code=0xC0000005
  #7 VEH-veh3(First=0,排链尾)
  #8 UEH(SetUnhandledExceptionFilter)
     UEH 看到 code=0xC0000005,返回 EXECUTE_HANDLER
```

(veh2、veh3 的抬头与场景 A 的一模一样，咱们看到的原文就是这个样子，删节时咱们一个字都没改。)咱们把两幕合起来看，完整的出场顺序就齐了:VEH 们从头到尾都在 SEH 之前，链序按的是注册方式，后面有 SEH 作用域的话，走的路线就是 filter 进处理块，没有 SEH 的话就轮到 UEH 收尾。VEH handler 的返回值只有两个选择:`EXCEPTION_CONTINUE_SEARCH` 是放行找下家，`EXCEPTION_CONTINUE_EXECUTION` 干的是把现场顶回去重放。文档对 handler 里的动作也有劝告，原话说的是 handler `should not call functions that acquire synchronization objects or allocate memory`，跟 Linux 信号 handler 的异步信号安全是同一类的限制，咱们上一篇在 VEH 里只做 `WriteFile` 直写控制台，守的就是它。

## 修现场，还是原地打转

`EXCEPTION_CONTINUE_EXECUTION` 这个返回值的分量，值得咱们单独做一场对照实验，因为它承诺的东西很重:回到故障指令的位置，原样地重放一遍。实验的代码节选自 `e3_continue.cpp`(节选时把几条打印压成了注释):

```cpp
static LONG WINAPI repair_veh(PEXCEPTION_POINTERS ep) {
    PEXCEPTION_RECORD r = ep->ExceptionRecord;
    if (r->ExceptionCode != 0xC0000005 || r->ExceptionInformation[0] != 1
        || (void*)r->ExceptionInformation[1] != expect_page) {
        return EXCEPTION_CONTINUE_SEARCH;   // 不是等的那一笔,放行
    }
    if (mode == 0) {
        DWORD old = 0;
        VirtualProtect(expect_page, 4096, PAGE_READWRITE, &old);
        return EXCEPTION_CONTINUE_EXECUTION;   // 修好现场,原写指令重放
    }
    if (hits < 3) { return EXCEPTION_CONTINUE_EXECUTION; }  // 不修,硬顶回去
    return EXCEPTION_CONTINUE_SEARCH;       // 打转 3 次了,放行让它崩
}

char* page = (char*)VirtualAlloc(NULL, si.dwPageSize, MEM_COMMIT, PAGE_READWRITE);
VirtualProtect(page, si.dwPageSize, PAGE_READONLY, &old);   // 改成只读
*(volatile int*)page = 0x1234;   // 写只读页:异常在此爆发
```

剧本分成了两种模式。模式 0 里 VEH 认出这是等的那笔写，`VirtualProtect` 把页改回了可写，接着返回的就是 CONTINUE_EXECUTION，模式 1 什么都不修，返回的还是 CONTINUE_EXECUTION。咱们看两边的输出:

```text
$ ./e3_continue.exe
page=00000261d33f0000 当前 PAGE_READONLY
== 模式 0:VEH 修好现场再 CONTINUE_EXECUTION ==
  [VEH hit 1] code=0xC0000005 info[0]=1(WRITE) info[1]=0x261D33F0000
     修现场:VirtualProtect -> PAGE_READWRITE,返回 CONTINUE_EXECUTION
  写指令重放成功:*(int*)page = 0x1234
== 模式 1:不修现场硬 CONTINUE_EXECUTION(原地打转,3 次后放行)==
  (这行之后进程应当死掉)
  [VEH hit 1] code=0xC0000005 info[0]=1(WRITE) info[1]=0x261D33F0000
     不修现场,硬返回 CONTINUE_EXECUTION(同一个错会再来)
  [VEH hit 2] ...(与 hit 1 相同的两行,删节)
  [VEH hit 3] ...(同上,删节)
     打转 3 次了,放行让它崩
```

模式 0 的最后一行是全场的答案:同一条写指令重放成功，咱们读回来的是 0x1234，进程也还活得好好的，接着往下跑了。Windows 侧修现场的机制，全在这里了:分发器把 CONTEXT 里的指令指针指回故障点，现场要是修好了，重放就是一次正常的执行。模式 1 是它的反面教材:现场的一个字节都没动过，顶回去的结果就是同一个错再来一遍，于是 VEH 在原地一遍遍地打转，直到咱们数满三次撒手放行。所以 CONTINUE_EXECUTION 从来不负责解决问题，它管的只有重放一件事，修不修现场的这件事，是您自己的事。

前面讲异常记录的小节还欠着一个观察没交代:软件异常的重放，恢复点并不是您代码里的调用处。记录里的 raise-at 是系统 DLL 里 trampoline 的返回地址(trampoline 就是库入口里那一小段转发用的跳板代码，真正干活的函数住在更深处)，filter 把重放顶了回去之后，效果是让 `RaiseException` 正常地返回，回到调用它的下一句。矩阵里 `m9b` 探针验证的就是这个:对 `RaiseException` 顶回去的实验里，程序里没有重抛的现象，而是平平常常地从调用点接着走了。

## 没人接住:退出码就是异常码本身

UEH 收尾了之后，故事还剩最后一段:进程的死法与痕迹。咱们准备了三个小程序，一个裸崩、一个装了 UEH 且返回 `EXCEPTION_EXECUTE_HANDLER`、还有一个压掉了错误框，退出码全部用 cmd 的 ERRORLEVEL 读(`e4_exitcodes` 的原始捕获):

```text
$ cmd /v:on /c "e4_bare.exe < nul & echo ERRORLEVEL=!ERRORLEVEL!"
bare: about to write to NULL
ERRORLEVEL=-1073741819

$ cmd /v:on /c "e4_ueh.exe < nul & echo ERRORLEVEL=!ERRORLEVEL!"
ueh: about to write to NULL
  [UEH] code=0xC0000005 at rip=00007ff79b721547, returning EXECUTE_HANDLER
ERRORLEVEL=-1073741819

$ cmd /v:on /c "e4_errmode.exe < nul & echo ERRORLEVEL=!ERRORLEVEL!"
errmode: about to write to NULL (no GPF box)
ERRORLEVEL=-1073741819
```

三发的结果全是 -1073741819，它就是 `0xC0000005` 的有符号 32 位写法。裸崩的进程，退出码给的正是异常码本身，这一点在 Linux 侧对不上号:那边默认的处理是信号杀进程，shell 里看到的是 128 加信号号，这边是把 32 位的异常码整个交了出去。咱们看第二发，它最有意思:UEH 跑了、日志也打了，可它返回了 EXECUTE_HANDLER 之后，进程照样以 `0xC0000005` 的码收场，崩溃点后面本该接着执行的 main 尾声，咱们一行都没见到。文档对 EXECUTE_HANDLER 的措辞是 `usually results in process termination`，实测在本机是铁的:UEH 是给收尸留的观察位，不是把进程救活的机会。第三发压掉的是 `SEM_NOGPFAULTERRORBOX` 的错误框，退出码倒是纹丝没动。至于 WER(Windows Error Reporting)的默认行为，本机的注册表没有动过，控制台进程在 stdio 重定向的管线下裸崩，咱们没有观察到弹窗，进程即刻就退出了，跑批时的 `< nul` 就是防它把自动化卡死用的。

读退出码的两个门道，笔者也都替您踩过了。cmd 的复合命令里 `%ERRORLEVEL%` 在解析期就展开了，读崩掉的进程要用 `/v:on` 加 `!ERRORLEVEL!` 的延迟展开，上面的命令行就是这么写的。在 WSL 里直跑 exe 的话，`$?` 拿到的是 wait status 的低 8 位，`0xC0000005` 会截成低 8 位的 5，矩阵里 m10 那个 exit=5 就是这么来的。上一篇里同一个场景的 bash，报的却是 139，旁边还配了一句 Segmentation fault——同一个 `$?`，报法随 shell 对 wait status 的翻译而变。全码您只能在 cmd 的 ERRORLEVEL 里读。

## IN_PAGE_ERROR:拍不成的剧本与零填充的答案

轮到本篇最想对上的那场对照了。Linux 篇里最惊悚的实验是 SIGBUS:映射挂得好好的，文件在背后被 `ftruncate` 砍掉了一半，咱们再去摸越界区，进程当场就被信号带走了。Windows 侧的镜像异常是 `EXCEPTION_IN_PAGE_ERROR`(0xC0000006)，文档还专门嘱咐过咱们，映射视图的读写要用结构化异常保护。咱们把 Linux 的剧本原样搬过来试试(节选自 `e2c_truncate_api.cpp`，三条截短的路各走了一遍):

```text
$ ./e2c.exe
view=0000025519a70000 (3-page mapping active)
SetEndToFile(4096)                 -> 0 gle=1224
FileEndOfFileInfo(4096)             -> 0 gle=1224
FileAllocationInfo(4096)            -> 0 gle=1224
unmap + SetEndOfFile(4096)          -> 1 gle=1224
final size=4096
```

(头一行的 `SetEndToFile` 是实验程序自己打错的标签，咱们保留了原文的样子。第四行的 `-> 1` 是截短成功，它尾巴上挂的 gle=1224 是上一条失败留下的陈值，`SetEndOfFile` 成功的时候不清这个槽。)上一篇咱们已经见过 1224:`ERROR_USER_MAPPED_FILE`，`SetEndOfFile` 拦下了截短。这一轮咱们知道得更多了:`SetFileInformationByHandle` 的 `FileEndOfFileInfo` 与 `FileAllocationInfo` 两条路，同样也全被拦下了。半途形态——只解视图、留着句柄——e2c 的剧本里没有，咱们单独补了 `t_unmap_only.cpp`(也收在 `02-in-page-error/` 的存档里):映射还活着的时候 `SetEndOfFile -> 0 gle=1224`，只解视图、留着句柄的话再试，拿到的还是 `-> 0 gle=1224`，把句柄也关掉了，第三次才成了，文件落到了 4096。想要把文件砍短的话，就得把所有的视图与句柄清到一条不剩。Linux 的触发器要三个条件:映射活着、文件被砍、再去摸，而第二个条件在 Windows 上**根本凑不齐**，系统把事故拦在了发生以前。

那咱们换个思路，把顺序倒过来做:解除映射、砍短、再盖一个新的映射，让 3 页的映射盖在 1 页的文件上，去读文件里不存在的页(`e2_in_page_error.cpp` 的后半场):

```text
$ ./e2.exe(前半场输出略,完整原文在存档)
view=0000026a8fd20000(3 页映射盖在 1 页文件上)
== 读 view[5](第 0 页,文件里有)
-> 0x45
== 读 view[1*4096+5](第 1 页,文件里没有)
-> 0(0=零填充,无异常;Linux 这里是 SIGBUS)
== 读 view[2*4096](第 2 页,文件里没有)
-> 0
== 读 view[3*4096](越过映射边界本身)
  [filter 触发] code=0xC0000005 flags=0x00000000
    info[0]=0 info[1]=0x26A8FD23000
-> -1(-1=异常;这才是 ACCESS_VIOLATION 的地界)
done
```

咱们看到的答案是一排零。越出文件尾、但还在映射之内的页，读出来的是零填充，其实一个异常都没有，filter 待命了一路却一次都没响。Linux 在同一个位置上送的是 SIGBUS，Windows 送的是零。真正报 `0xC0000005` 的只有最后一笔，那是越过映射边界本身的访问，属于访问违例的地界，跟文件尾已经不相干了。写方向咱们单独跑了一场(节选自 `e2b_write_direction.cpp`):

```text
$ ./e2b.exe
file size = 4096(1 页,内容 0x5A)
view=0000020432e40000(3 页映射盖在 1 页文件上)
== 写 view[2*4096] = 0x77(第 2 页,文件里没有)
-> 0(0=写成功无异常)
== 读回 view[2*4096]
-> 0x77
== FlushViewOfFile(强制回写第 2 页)
FlushViewOfFile -> 1
file size after flush = 12288(文件被写胖了)
done
```

咱们把写方向的结果连起来看:写进零填充页也不报错，读回来的还是 0x77，咱们刷一次 `FlushViewOfFile`，文件自己长回了 3 页。零填充的页在写的那一刻就被认领了，回写的时候文件尾随之伸过去，一切安静得像什么都没发生过。

那 `0xC0000006` 到底什么时候来?文档的说法是，分页 I/O 真的失败了才来:网络断连、介质弹出、磁盘错误，页调度器拿不回数据的那些时刻，记录的 info[2] 里还会塞着那次失败的 NTSTATUS。这些现场咱们在本机一个都造不出来，本机没有可移动的介质，建回环共享又要的是管理员权限，笔者试过的路都在这里交代了。咱们在 e2 里没等到一次 `0xC0000006`，filter 只在越过映射边界的那一次响过，响的还是 `0xC0000005`，而**这个零就是实验交回来的答案**:在 Windows 上，文件截短的一整条路都被系统拦死，越文件尾的访问读到的是零、一个异常都不触发，IN_PAGE_ERROR 只留给真正的 I/O 故障。外部佐证笔者也找了一件:golang/go 的 issue #58457，外接磁盘在映射还存活的时候被弹出，之后读 mmap 视图收到的码是 `0xC0000006`，触发条件是介质真的没了，不是本地截短。

工程上的取舍也就清楚了。Linux 侧咱们必须伺候 SIGBUS，handler 只能做异步信号安全的事，防御靠的是 fstat 量准长度。Windows 侧的拦截点在文件系统那层，SEH 作用域真正要防的，是写只读视图的 `0xC0000005` 与真 I/O 故障的 `0xC0000006`，后者您在开发机上大概率一辈子见不着，见着的时候，多半是生产环境的盘出了事。

## C++ 的 throw:另一套码，同一台分发器

最后一组的实验，把 C++ 的异常也请进来。MSVC 的 x64 上，throw 的底层走的是 `_CxxThrowException`，异常码给的是 `0xE06D7363`(低三位字节拼出来的是 msc)，参数的布局是 MSVC 自家的另一套。那 MinGW 走的什么码?咱们挂一个只数数的 VEH，看每一次的分发(代码节选自 `e5_veh_throw.cpp`，观察 msc 码的分支略):

```cpp
static LONG WINAPI counting_veh(PEXCEPTION_POINTERS ep) {
    ++dispatch_count;
    printf("  [VEH dispatch #%d] code=0x%08lX\n",
           dispatch_count, (unsigned long)ep->ExceptionRecord->ExceptionCode);
    return EXCEPTION_CONTINUE_SEARCH;   // 只观察,不拦截
}

try { throw std::runtime_error("from e5"); }
catch (const std::exception& e) { printf("  caught: %s\n", e.what()); }
```

```text
$ ./e5_veh_throw.exe
VEH in place. dispatch_count=0
== throw std::runtime_error,外面有 catch ==
  [VEH dispatch #1] code=0x20474343
  caught: from e5 (dispatch_count=1,应为 1)
== throw 一个 int ==
  [VEH dispatch #2] code=0x20474343
  caught int 42 (dispatch_count=2,还是 1 —— catch 不再产生异常分发)
== 再 throw 一次(计数应到 2,每次 throw 恰好一次分发)==
  [VEH dispatch #3] code=0x20474343
  caught: second (dispatch_count=3)
done
```

咱们把三条分发的码放在一起看:全是 `0x20474343`，低三位字节拼出来的是 GCC，不是 MSVC 的 msc 码。MinGW 的 throw 也是走 SEH 的路数进分发器的，VEH 是看得见的，libgcc 的运行库里 `RaiseException` 与 `RtlUnwindEx` 紧挨着那个立即数，每次 throw 走的恰好是一次分发，catch 本身不再产生新的异常。msc 码在本机的存在感只剩一处静态证据:ucrtbase 那个 handler 里硬编码着 `cmpl $0xe06d7363` 的特判，可 MinGW 的 throw 根本不用它。参数的布局也不同:GCC 变体的记录只带 1 个参数，MSVC 那边带的是一个魔法数加一组指针，x64 上常见的是 3 到 4 个参数，咱们在本机一次都没观察到它的记录(MinGW 不产生 msc 码)，所以在 VEH 里想按 MSVC 那套布局解 throw 信息，在 MinGW 上是解不出来的。

反过来的方向咱们也测了:C++ 异常穿过 SEH 作用域，会发生什么(节选自 `e5_swallow.cpp`)?动手以前咱们有个交代:throw 直接写进受保护体内，与 return、goto 是同一类的下场。无条件的写法，后面的 `__except1` 汇编会被判成不可达删掉，链接期就报出 `.l_endw` 未定义的错。保住直落路径、能链接的条件写法，复验里异常是直接穿帧的，filter 连被咨询的份都没有，外层的 catch 照样接得到，进程也活得好好的。所以想让异常以穿越者的身份走进 filter 的视野，咱们把 throw 放进了被调函数:

```cpp
__attribute__((noinline)) void inner_throw(volatile int* cond) {
    if (*cond) throw std::runtime_error("crossing the SEH frame");
}

__attribute__((noinline)) void guarded_call(volatile int* cond, int* result) {
    __try1(swallow_filter)
        inner_throw(cond);
        *result = 0;
    __except1
        *result = -1;   // 吞掉后从这继续
}

try {
    Loud l;                 // 外层对象,析构应当有机会跑
    guarded_call(&fire, &result);
    printf("  guarded_call 正常返回(result=%d)\n", result);
} catch (const std::exception& e) {
    printf("  catch 跑到了:%s\n", e.what());
}
```

```text
$ ./e5_swallow.exe
== SEH 吞 C++ throw ==
  [SEH filter] code=0x20474343 flags=0x00000080 nparams=1
    info[0]=0x1769183DA80(GCC 变体只带 1 个参数)
  guarded_call 正常返回(result=-1)——注意:catch 没接到这一票
  [~Loud] 析构跑了
  try/catch 之后还活着,继续跑
done
```

(输出头一行的抬头是实验程序自己打的标签，咱们保留了原文。)filter 看到了 GCC 码的异常，返回的是 1，处理块落地，`guarded_call` 带着 -1 正常地返回了，而外面的 catch，永远等不到这一票了。SEH 作用域把穿越而过的 C++ 异常截住了，运行时按它自己的规则走完了展开，`Loud` 的析构也跑了，进程活得好好的，只是语言层的 catch 被整个绕了过去。MSVC 的文档明说不推荐这么混用，原话里还多了一句 `might not be what you expect`，同函数的形态更是 C2712 编译期直接拒绝，GCC 这边倒是连文档都没有，实测就是您看到的这样。落到笔头上就是一句交代:MinGW 下别让 SEH 作用域挡在 throw 与 catch 的路上，一个程序里的两套机制，各管各的地界。

## 另一侧怎么看

咱们把两侧的分野摆开收尾。同样是访问出错的场景，Linux 送的是信号，handler 拿的是 siginfo，`si_addr` 给地址、`si_code` 分病因，咱们想在 handler 里修现场，得从第三个参数的 ucontext 里摸上下文。Windows 送的是异常码，filter 与 VEH 拿的是 EXCEPTION_RECORD，info[0] 分读写与 DEP、info[1] 给地址，修现场有现成的 `EXCEPTION_CONTINUE_EXECUTION`，重放是分发器替您做的。文件被砍短了以后再摸越界，Linux 用的是 SIGBUS 追责，Windows 把截短拦在了 `SetEndOfFile` 那里，越界读换来的是一排零填充，IN_PAGE_ERROR 只留给真正的 I/O 故障。Ctrl+C 也是分岔的一处，咱们顺手验过(e6，输出删节了两行):VEH 与 `SetConsoleCtrlHandler` 同时在岗，控制台事件发了出去，VEH 的分发计数是零，ctrl handler 却在**另一个线程**里跑了，文档的原话是 `the system creates a new thread in the process to execute the function`，控制台事件走的是新线程，压根儿不进异常的分发器。C 与 Break 咱们都发过，稳定送达的是 CTRL_BREAK。CTRL_C 咱们在 WSL interop 链起的进程里是收不到的，这件事的根因，后来在 [控制台事件与 APC](../process/02-console-apc.md) 那一篇查清了，是启动链继承下来的忽略位，咱们用一句 `SetConsoleCtrlHandler(NULL, FALSE)` 把它解除之后，定向发送的 CTRL_C 就能实收，控制台窗口的真假不在因果里。MinGW 的 `signal(SIGINT)` 只是 CRT 在上面的模拟，正主的完整机制，咱们在那一篇里讲全了。

与咱们已有工具的关系也交代一句。Win32 调用的失败，走的是 GetLastError 加 error_code 的老路，工具与双出口的分寸定义在 [错误处理范式](../../thinking/02-error-paradigm.md) 里。本篇讲的异常，伺候的是另一类东西:调用没有返回失败，是执行本身出了故障。两条路是不相干的，一条管预料内的失败，另一条管预料外的崩溃，您写防御代码的时候，两边咱们都得想。Linux 侧的完整剧本，请您移步 [mmap 内存映射:把文件贴进地址空间](../../linux/file-io/02-mmap-memory-mapping.md) 的 SIGBUS 一节，拿它对读今天的零填充，分岔的地方一眼就能看清。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="EXCEPTION_RECORD structure"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-exception_record"
  />
  <ReferenceItem
    :id="2"
    title="AddVectoredExceptionHandler function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-addvectoredexceptionhandler"
  />
  <ReferenceItem
    :id="3"
    title="PVECTORED_EXCEPTION_HANDLER callback"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/winnt/nc-winnt-pvectored_exception_handler"
  />
  <ReferenceItem
    :id="4"
    title="SetUnhandledExceptionFilter function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-setunhandledexceptionfilter"
  />
  <ReferenceItem
    :id="5"
    title="RaiseException function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-raiseexception"
  />
  <ReferenceItem
    :id="6"
    title="SetEndOfFile function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setendoffile"
  />
  <ReferenceItem
    :id="7"
    title="SetErrorMode function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-seterrormode"
  />
  <ReferenceItem
    :id="8"
    title="issue #58457: mmap read fails with 0xC0000006 after ejecting disk"
    publisher="golang/go (GitHub)"
    url="https://github.com/golang/go/issues/58457"
  />
  <ReferenceItem
    :id="9"
    title="excpt.h (__try1/__except1 宏)"
    publisher="mingw-w64 (GitHub mirror)"
    url="https://github.com/mirror/mingw-w64/blob/master/mingw-w64-headers/crt/excpt.h"
  />
  <ReferenceItem
    :id="10"
    title="SEH 系列分析文章(?p=99 起)"
    publisher="nynaeve.net"
    url="https://www.nynaeve.net/?p=99"
  />
  <ReferenceItem
    :id="11"
    title="HandlerRoutine callback function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/console/handlerroutine"
  />
</ReferenceCard>
