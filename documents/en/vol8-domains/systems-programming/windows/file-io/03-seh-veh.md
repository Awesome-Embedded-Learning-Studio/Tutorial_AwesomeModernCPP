---
title: "Structured Exceptions: SEH and VEH"
description: "The complete mechanism chain for access errors on the Windows side. Picking up from the 0xC0000005 the previous chapter earned by writing to a read-only view: the exception record's measured readings — info[0] 0=read/1=write/8=DEP execute (8 is not write), info[1] precise down to the byte. VEH's head-insertion chain order, the same write instruction replaying successfully after the scene is repaired, and spinning in place when it is not. MinGW has no __try/__except keywords, so the only way out is the __try1/__except1 macros from excpt.h, with first-hand measurement of the UCRT-variant filter ABI (the first parameter arrives as an EXCEPTION_POINTERS-shaped structure), zero compile-time checking when destructors are mixed in, and -O2 code generation as a lottery. When nobody handles the exception, the exit code is the exception code itself. The IN_PAGE_ERROR script is blocked wholesale by ERROR_USER_MAPPED_FILE, reads past end-of-file yield zero fill (Linux sends SIGBUS in the same scenario), and the very inability to reproduce it locally is itself the answer. MinGW's throw travels as 0x20474343 rather than 0xE06D7363, VEH can see the throw, and SEH scopes can intercept C++ exceptions passing through."
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
translation:
  source: documents/vol8-domains/systems-programming/windows/file-io/03-seh-veh.md
  source_hash: b4d6189352eaf7748c6601c0356813412a40a51b128c5fd9978a867ec36a4643
  translated_at: '2026-11-22T15:43:09+00:00'
  engine: glm-5.3
  token_count: 24931
---
# Structured Exceptions: SEH and VEH

At the end of the previous chapter we forced four bytes of writing onto a `FILE_MAP_READ` view. The VEH barely managed to log one `0xC0000005` for us before the process then and there wrapped up. At the time we only said half a sentence: an unhandled structured exception takes the exception code itself as the process exit code. This chapter walks the owed mechanism chain to completion in one go — who assigns this code, what else is written in the record, who ranks ahead of whom, who may repair the scene, where execution resumes after the repair, and how the process dies when nobody catches it. We will answer the mirrored questions from the Linux side as well: the same wild pointer and out-of-bounds read that deliver SIGSEGV and SIGBUS over there deliver an exception code plus a dispatch chain over here.

Two protagonists carry this chapter. The first is SEH (Structured Exception Handling): CPU page faults, division by zero, and errors that software raises on its own initiative with `RaiseException` all converge in the kernel into the same kind of thing, called an exception record (EXCEPTION_RECORD), which the kernel's dispatcher then hands back to user mode, hunting station by station through the handlers we have registered. `__try`/`__except` is its face at the language level, and MSVC has served it for thirty years. The other protagonist is VEH (Vectored Exception Handling), whom we already met: its registration function `AddVectoredExceptionHandler` we called in the previous chapter. It is a process-wide chain that ranks ahead of all SEH scopes — when we want the earliest possible look at an exception, hanging a function on it is all it takes. These two, plus the closing act UEH (the unhandled exception filter, the last stop installed by `SetUnhandledExceptionFilter`), make up the full cast order for access errors on the Windows side.

The numbering convention, stated up front so you can match it against the archive: this chapter's experiments are numbered e1 through e6, the lowercase e following the six archive subdirectories collected under `code/volumn_codes/vol8/systems-programming/windows/file-io/03-seh-veh/`, and a file name like `e1_av_decoding.cpp` is the number itself. The thinking chapters' Windows experiments likewise continue with lowercase e, their numbering independent of this file-I/O chapter's. Go by the file names and you will not mistake anyone. The environment is the familiar old set: the local Win11 26200 machine, MSYS2 UCRT64 g++ 16.1.0 (the interop chain that launches it across systems was covered in W01), and one uniform compile command: `g++ -std=c++20 -Wall -Wextra e*.cpp -o e*.exe`. The main experiments all run unoptimized, and `-O2` plays a role only as the control group in the compiler matrix — the reason becomes clear when we reach the matrix section. The order of appearance deserves a word too: what steps on stage in the main text is e1, e3, e4, e2, e5, e6. The numbers belong to the archive, not to the order of appearance, the same policy as the thinking chapters. One small difference when reading the output blocks: the matrix m-series captures include even the command line as it was, while the e-series raw captures carry no command line — the `$` command lines atop the e-series blocks below are echoes we re-created from the reproduction commands in the archive README. The outputs were captured on 2026-10-02. Address-like numbers all eat ASLR, so different-looking values on your re-run are exactly what healthy looks like.

## MinGW Has No __try, Only a Way Out Through an Internal Macro

Before we can read an exception record hands-on, we need an SEH scope on the table, and the trouble lives precisely there. If you copy `__try { ... } __except (...) { ... }` over from an MSVC article, GCC shuts the door on the spot:

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

(The archive's original text is exactly this. The probe's source file at the time was probe_try.cpp, the flags in the command line were recorded as an ellipsis, and the final line's compile exit=0 is the capture script's own exit code, not the compiler's.) Of the three keywords in that syntax, GCC 16.1.0's implementation has none at all, the `-fms-extensions` switch is no help either, and only MSVC and Clang recognize them. The only way left is hidden in mingw-w64's `<excpt.h>`: a pair of internal macros named `__try1(filter)` and `__except1`. With inline assembly they assemble, inside the current function, a `.seh_handler __C_specific_handler` plus a single-entry scope table. The filter is the function we pass to `__try1` — when an exception lands in the scope, it is the first to speak up, and its return value decides whether the handler block runs or the search continues upward. The macros' status as internal goods brings real constraints. We save the measured usage boundaries for the matrix section and cover them together there. For now you only need to acknowledge one fact: every SEH experiment in this chapter walks through this pair of macros.

Next comes the spot where I owe a dedicated paragraph, because what it steps on is an ABI fork that almost nobody has documented. The classic references (nynaeve.net's article series dissecting SEH piece by piece, for instance) all tell you that when `__C_specific_handler` calls the filter, the first parameter is a bare `PEXCEPTION_RECORD`. What we measured on this machine says otherwise: under UCRT64 linking that symbol resolves to **an export of ucrtbase.dll** (ntdll actually exports a function of the same name, but the linker picked the former). And ucrtbase's own implementation, when calling the filter, places into x64's first argument register RCX a stack structure shaped like `EXCEPTION_POINTERS`, with `[0]` holding the record pointer and `[1]` holding the context pointer. The forensics took two steps: the filter's return address lands inside ucrtbase.dll's territory (the evidence comes from the m0b probe in `01-try-except/matrix/`, whose output is collected in the latter half of `m0_filter_abi.out` — the probe's residence gets formally introduced in the matrix section), and disassembling the corresponding spot in ucrtbase shows exactly `lea 0x30(%rsp),%rcx` right next to `call *%rax` — it assembles that structure on the stack first, and only then puts the address into RCX.

We checked the four parameters one by one: RCX carries this structure, RDX carries the EstablisherFrame (the base of the stack frame our filter lives in — registration information handed down by the dispatcher, of no use to us), R8 also carries a PCONTEXT (contents identical to `[1]`), and R9 carries the DISPATCHER_CONTEXT (the dispatcher's own context record, likewise of no use to us). So every filter in this chapter is prototyped with `void*` up front, cast to `PEXCEPTION_POINTERS` once inside, and used from there. If you follow the classic spelling and treat the first parameter as a `PEXCEPTION_RECORD`, the ExceptionCode you read off the top is actually the low 32 bits of the record pointer, and everything is skewed from the very first member on.

## Exception Record 0xC0000005: info[0]'s 8 Means DEP

The materials of the crime scene all sit in `EXCEPTION_RECORD`, and we pick the fields by their use. `ExceptionCode` carries the exception code — `0xC0000005` is the number of `STATUS_ACCESS_VIOLATION` (access violation). It belongs to the kernel's NTSTATUS numbering system, two unrelated sets of digits from GetLastError's Win32 error codes: 1224 is Win32's number for that error, 0xC0000006 is NTSTATUS's — do not copy them into the same table. `ExceptionAddress` is the address of the faulting instruction. `ExceptionInformation` (info for short below) is the attached parameter slot array. Under the name of access violation, Microsoft Learn's EXCEPTION_RECORD page is clear: the first element gives 0 for read, 1 for write, and 8 for a DEP violation, while the second element gives the virtual address of the inaccessible data. The one worth your extra glance is that 8: it marks a DEP violation (Data Execution Prevention), not the act of writing. Let us run it once from the top and film all four kinds of scene (excerpted from e1's `e1_av_decoding.cpp` — what main holds is four `probe` calls, targeting a `PAGE_NOACCESS` page, that page plus a 0x1234 offset, the null pointer, and a readable-writable-but-not-executable data page):

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

Take the four rounds of output one at a time. Rounds 0 and 1 split read from write: info[0] gives 0 in one round and 1 in the other. Round 1's info[1] gives `0x128A6BC1234` — the page base plus 0x1234, precise down to the **byte**. When we wrote it we deliberately picked an in-page offset just to put this on film. Set against the Linux side, the division of materials maps over almost one to one: info[1] corresponds to siginfo's `si_addr`, fault-at corresponds to the faulting instruction's rip, and Linux splits the cause with `si_code` (`SEGV_ACCERR` says permission denied, `SEGV_MAPERR` says never mapped at all — the two different things the mmap chapter measured). Windows has no parallel encoding here: permission errors and unmapped errors share the single code `0xC0000005`, and all we can do is take info[1] to the page table ourselves.

Round 2 reads the null pointer, and info[1] also gives 0 — nothing much to say there. Round 3 is DEP's scene: we called a `PAGE_READWRITE` data page as if it were a function, the CPU stepped on a non-executable page while fetching the instruction, and info[0] gives 8. One corroborating detail in this round deserves your attention: fault-at printed `00000128a6bd0000`, which is exactly the data page's own address. In the first three rounds it always gave the address of the faulting instruction in our code, but in this round it became the jump target. In an instruction-fetch violation, the faulting instruction is precisely the instruction that cannot be fetched back.

Other origins sit on the exception-code table, and we look at them in passing (excerpted from the three shots of `e1_custom_codes.cpp`):

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

The three lines each tell their own story. The `int3` instruction delivers `0x80000003` (STATUS_BREAKPOINT), an old friend of debuggers, with nparams giving 1. Integer division by zero is a hardware exception, its code `0xC0000094`, wearing the name STATUS_INTEGER_DIVIDE_BY_ZERO. The middle line is the most interesting: with `RaiseException(0xE0001234, 0, 4, args)` we manufactured an exception ourselves — the custom code entered the record as-is, the four parameter slots passed through untouched, nparams honestly reports 4, and the flags carry an extra `0x80`, the marker RaiseException stamps for a software origin. winnt.h does not even collect a constant name for it (nynaeve.net has the philology). raise-at landed at `00007fff83a341ca`, not in our code, because the scene of a software exception is inside RaiseException in a system DLL — a point that will come back to trouble us when we talk about replay.

## The Usage Boundaries of __try1: A Measured Matrix

The macro's constraints I ran down one by one, all collected into the table below (the probes hang in the `01-try-except/matrix/` subdirectory under the e1 archive directory, eleven .cpp files accumulated, numbered with gaps — the skipped numbers were not archived):

| Scenario | Measured result |
| --- | --- |
| `__try`/`__except`/`__finally` keywords, any flags | does not compile, the keywords do not exist |
| `__try1` in an ordinary function, `-O0` and `-O2` | fine — the filter is entered, the handler block lands |
| `__try1` in `main`, with `-O2` | assembler error: GCC puts main into `.text.startup`, and the hard-coded `.text` in the macro cannot switch back |
| `__try1` plus an object with a destructor in the same function, `-O0` | compiles, dies at run time: the filter never runs once, and the process ends with the 0xE0001111 it raised itself (WSL's $? reads 17) |
| same as above, `-O2` | assembler error (`.text` minus `.text.startup`) |
| protected region split into a separate `noinline` function (outer layer keeps its destructors as usual), `-O0` | fine — MSVC C2712's official remedy holds here too |
| same as above, `-O2` | code-generation lottery: the filter runs, and landing crashes on arrival |
| `__try1` used twice in one translation unit | assembler error, `.l_startw` multiply defined |
| `return`/`goto` jumping out of the protected body | `__except1`'s assembly gets judged unreachable and deleted, `.l_endw` undefined at link time |
| `-fno-exceptions` or `-static` | `__try1` works as usual |

The most wounding is the table's fourth row. Inside one function's body we placed both `__try1` and an object with a destructor, and GCC does not utter a single complaint:

```text
$ g++ -std=c++20 -Wno-unused-parameter m2_mixed.cpp -o m2 && ./m2
s=a string with dtor semantics
  [body] raising
exit=17
```

Our filter never ran once, and the process wrapped up on its own, the closing code being the `0xE0001111` it raised itself: WSL's `$?` read 17 — exactly that code's low 8 bits — and cmd's ERRORLEVEL gave -536866543, the same code's signed spelling. The manner of death is the same mechanism as e4's bare crash later on. The archive note originally attributed this ending to 0xC0000409 (STATUS_STACK_BUFFER_OVERRUN), but the re-verification run through every shape without ever meeting it, so we write it per the re-verification. The corresponding MSVC scenario is a compile-time C2712 refused on the spot (the official remedy being to split the protected region into another function). GCC here checks nothing at compile time and lets it pass — the real verdict waits for the run. We already put the function-splitting remedy to work in e1: `probe` hangs `noinline` and stands as a function of its own, the destructing object stays with the outer layer, and at the `-O0` setting it is stable.

Among the `-O2` cells, the split-function-plus-`-O2` combination is a lottery and deserves its own look:

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

The mechanism is not mysterious: `__except1` lands by way of assembly labels, while GCC's optimizer has no idea that control flow can parachute onto that label. However it arranges code near the two assembly boundaries it is not in breach, and if the instruction stream it emits happens to sever the landing path, it has no way of knowing. So the rule for putting pen to paper is: split the protected region into a `noinline` function, write no return in its body and branch on a faulted flag instead (the idiom in every experiment's source), keep the optimization level pinned at `-O0` — and if you want it sturdier still, do not use `__try1` at all. We switch to VEH.

## VEH: The Chain Ranked Ahead of All SEH

`AddVectoredExceptionHandler(First, Handler)`'s First parameter governs queue-jumping: a non-zero value inserts at the head of the chain, 0 queues at the tail, and the effect of head insertion is that whoever registers later ranks further forward. We ran that sentence to ground with three handlers (excerpted from `e3_chain.cpp` — the filter and UEH definitions omitted, the printing statements compressed into comments):

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

(The headers of veh2 and veh3 are identical to scenario A's. What we saw in the original is exactly this shape, and we changed not one word in trimming.) Combine the two scenes and the complete order of appearance is assembled: the VEHs sit ahead of SEH from head to tail, the chain order following the registration style. If an SEH scope waits behind, the route runs filter into handler block. Without SEH, it is UEH's turn to close the show. A VEH handler's return value has exactly two choices: `EXCEPTION_CONTINUE_SEARCH` lets go and hunts for the next in line, while `EXCEPTION_CONTINUE_EXECUTION` pushes the scene back in and replays. The documentation also has admonitions about what a handler may do — its own words say a handler `should not call functions that acquire synchronization objects or allocate memory`, the same family of restriction as async-signal safety for Linux signal handlers. That is why in the previous chapter our VEH did nothing but `WriteFile` straight to the console.

## Repair the Scene, or Spin in Place

The weight of the `EXCEPTION_CONTINUE_EXECUTION` return value deserves a dedicated comparison experiment, because what it promises is heavy: go back to the faulting instruction's position and replay it as it was. The experiment's code is excerpted from `e3_continue.cpp` (a few prints compressed into comments in the excerpt):

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

The script splits into two modes. In mode 0 the VEH recognizes this as the write it was waiting for, `VirtualProtect` flips the page back to writable, and what it returns is CONTINUE_EXECUTION. In mode 1 nothing is repaired, and what it returns is still CONTINUE_EXECUTION. Look at the two outputs:

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


Mode 0's last line is the whole answer: the same write instruction replayed successfully, what we read back was 0x1234, and the process is still alive and well and kept running onward. The Windows-side mechanism for repairing the scene is entirely here: the dispatcher points the CONTEXT's instruction pointer back at the fault point, and if the scene has been repaired, the replay is one ordinary execution. Mode 1 is its counterexample: not one byte of the scene was touched, so pushing it back yields the same error once more, and the VEH spins in place round after round until we count three and let go. So CONTINUE_EXECUTION never takes responsibility for solving the problem. It manages the replay alone — whether the scene gets repaired is your own business.

An observation owed from the exception-record section still awaits delivery: with a software exception, the replay's resumption point is not the call site in your code. The record's raise-at is the return address of a trampoline inside a system DLL (a trampoline being that short stretch of forwarding jump code at a library's entrance, while the function that really does the work lives deeper in). After the filter pushes the replay back, the effect is to let `RaiseException` return normally, to the statement after the one that called it. The `m9b` probe in the matrix verified exactly this: in the experiment that pushed back on `RaiseException`, the program showed no sign of re-raising, and simply carried on from the call point as if nothing had happened.

## Nobody Catches It: The Exit Code Is the Exception Code Itself

After UEH closes the show, one last stretch of story remains: the process's manner of death and the traces it leaves. We prepared three small programs — one bare crash, one with a UEH installed returning `EXCEPTION_EXECUTE_HANDLER`, and one with the error box suppressed — reading every exit code through cmd's ERRORLEVEL (`e4_exitcodes`' raw capture):

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

All three shots came back -1073741819, which is `0xC0000005`'s signed 32-bit spelling. A process that crashes bare-handed exits with exactly the exception code itself, a point that does not line up on the Linux side: there the default handling is death by signal, and the shell sees 128 plus the signal number, while here the full 32-bit exception code is handed over. Look at the second shot, the most interesting one: the UEH ran and its log was written, yet after it returned EXECUTE_HANDLER the process still closed with the code `0xC0000005`, and not one line of main's epilogue — the code that should have run after the crash point — was ever seen. The documentation's wording for EXECUTE_HANDLER is `usually results in process termination`, and measured on this machine it is iron: UEH is an observation post reserved for closing out the corpse, not a chance to bring the process back to life. The third shot suppressed the `SEM_NOGPFAULTERRORBOX` error box, and the exit code did not move by a hair. As for WER's (Windows Error Reporting's) default behavior: this machine's registry has never been touched, a console process crashing bare under a stdio-redirected pipeline showed no popup in our observation — the process exits on the spot — and the `< nul` in the batch runs is there precisely to keep it from wedging the automation.

Both tricks for reading exit codes, I have walked for you in advance. Inside cmd's compound commands, `%ERRORLEVEL%` expands at parse time, so reading a crashed process calls for `/v:on` plus `!ERRORLEVEL!`'s delayed expansion — the command lines above are written exactly so. Running an exe directly under WSL, `$?` receives the wait status's low 8 bits, so `0xC0000005` gets truncated to a low-8-bit 5, which is where the matrix's m10 exit=5 came from. In the previous chapter, the same scenario's bash reported 139 with a Segmentation fault caption alongside — the same `$?`, its phrasing shifting with the shell's translation of the wait status. The full code you can only read in cmd's ERRORLEVEL.

## IN_PAGE_ERROR: The Script That Cannot Be Filmed, and the Answer of Zero Fill

Now for the confrontation this chapter most wants to stage. The Linux chapter's most chilling experiment was SIGBUS: the mapping sits there in fine health, the file gets halved behind its back by `ftruncate`, and the moment we touch the out-of-range region the process is carried off by the signal on the spot. The Windows-side mirror exception is `EXCEPTION_IN_PAGE_ERROR` (0xC0000006), about which the documentation even gave us special instruction to protect mapped-view reads and writes with structured exceptions. Let us move the Linux script over wholesale and try (excerpted from `e2c_truncate_api.cpp`, each of the three truncation routes walked once):

```text
$ ./e2c.exe
view=0000025519a70000 (3-page mapping active)
SetEndToFile(4096)                 -> 0 gle=1224
FileEndOfFileInfo(4096)             -> 0 gle=1224
FileAllocationInfo(4096)            -> 0 gle=1224
unmap + SetEndOfFile(4096)          -> 1 gle=1224
final size=4096
```

(The first line's `SetEndToFile` is a label the experiment program itself mistyped, and we have kept the original's appearance. The fourth line's `-> 1` means the truncation succeeded — the gle=1224 hanging on its tail is a stale value left by the previous failure, since `SetEndOfFile` does not clear that slot on success.) The previous chapter already showed us 1224: `ERROR_USER_MAPPED_FILE`, with `SetEndOfFile` blocking the truncation. This round we know more: the `FileEndOfFileInfo` and `FileAllocationInfo` routes of `SetFileInformationByHandle` were blocked just as thoroughly. The halfway shape — unmap the view only, keep the handle — was not in e2c's script, so we made up `t_unmap_only.cpp` separately (also archived under `02-in-page-error/`): while the mapping lives, `SetEndOfFile -> 0 gle=1224`. Unmap the view but keep the handle and try again, and what comes back is still `-> 0 gle=1224`. Close the handle too, and the third try finally succeeded, the file settling at 4096. To shorten the file, every view and every handle must be cleared to none remaining. Linux's trigger needs three conditions: the mapping alive, the file cut, and a touch that follows. On Windows the second condition simply **cannot be assembled** — the system blocks the accident before it happens.

So let us flip the thinking and reverse the order: unmap, truncate, then lay a fresh mapping over the file, letting a 3-page mapping sit atop a 1-page file, and read the page that does not exist in the file (the second half of `e2_in_page_error.cpp`):

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

The answer we saw is a row of zeros. Pages beyond the file's end but still inside the mapping read back zero fill — in fact not a single exception occurred, and the filter stood by the whole way without sounding once. Linux delivers SIGBUS at this very spot. Windows delivers zeros. The only entry that actually raised `0xC0000005` is the last one, an access across the mapping boundary itself, which belongs to access-violation territory and no longer concerns the file's end. We ran the write direction separately (excerpted from `e2b_write_direction.cpp`):

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

String the write direction's results together: writing into a zero-filled page raises no error either, reading back still gives 0x77, and after one `FlushViewOfFile` flush the file grew back to 3 pages on its own. The zero-filled page was adopted the moment it was written, and the file's end stretched along with it at write-back — everything as quiet as if nothing had happened.

So when does `0xC0000006` actually come? The documentation's account: only when paging I/O genuinely fails — a network disconnect, a medium ejected, a disk error, the moments when the pager cannot bring the data back, with the record's info[2] still stuffing in the NTSTATUS of that failure. Not one of these scenes can we manufacture on this machine: there is no removable medium, and setting up a loopback share demands administrator privileges. Every path I tried is laid out here. We never met a single `0xC0000006` in e2 — the filter sounded only once, on the crossing beyond the mapping boundary, and what it sounded was still `0xC0000005` — and **this zero is itself the answer the experiment handed back**: on Windows, the entire route of file truncation is blocked dead by the system, reads past the file's end yield zeros without triggering a single exception, and IN_PAGE_ERROR is reserved for genuine I/O failure. I found one piece of external corroboration as well: golang/go issue #58457, where an external disk was ejected while a mapping still lived, and the code received on subsequently reading the mmap view was `0xC0000006` — the trigger being that the medium was truly gone, not a local truncation.

The engineering trade-offs are clear as well. On the Linux side we must tend to SIGBUS, the handler may do only async-signal-safe things, and the defense leans on fstat measuring the length accurately. On the Windows side the interception point sits at the file-system layer, and what an SEH scope genuinely needs to guard against is the write-to-read-only-view `0xC0000005` and the true-I/O-failure `0xC0000006`. The latter you will most likely never see in your lifetime on a dev machine, and when you do see it, the odds are that a disk in production has had an accident.

## C++'s throw: Another Set of Codes, the Same Dispatcher

For the last group of experiments we invite C++ exceptions onto the stage as well. On MSVC's x64, throw's underlayer goes through `_CxxThrowException`, the exception code is `0xE06D7363` (the low three bytes spell msc), and the parameter layout is another set of MSVC's own. So what code does MinGW travel on? We hang a counting-only VEH and watch every dispatch (code excerpted from `e5_veh_throw.cpp` — the branch watching for the msc code omitted):

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

Put the three dispatch codes side by side: all are `0x20474343`, whose low three bytes spell GCC, not MSVC's msc. MinGW's throw also enters the dispatcher by the SEH route, VEH sees it, and in libgcc's runtime `RaiseException` and `RtlUnwindEx` sit right next to that immediate — every throw walks exactly one dispatch, and catch itself generates no new exception. The msc code's presence on this machine is down to one piece of static evidence: ucrtbase's handler hard-codes a `cmpl $0xe06d7363` special case, but MinGW's throw never uses it. The parameter layout differs too: the GCC variant's record carries exactly 1 parameter, while MSVC's carries a magic number plus a set of pointers — commonly 3 to 4 parameters on x64. We never once observed its record on this machine (MinGW produces no msc code), so trying to decode throw information in a VEH per MSVC's layout simply does not decode on MinGW.

We tested the reverse direction as well: what happens when a C++ exception passes through an SEH scope (excerpted from `e5_swallow.cpp`)? One explanation before the hands-on: a throw written directly inside the protected body meets the same fate as return and goto. In the unconditional spelling, the `__except1` assembly behind it gets judged unreachable and deleted, and link time reports the `.l_endw` undefined error. In the conditional spelling that preserves the fall-through path and links, the re-verification showed the exception passing straight through the frame — the filter was never even consulted, the outer catch still caught it, and the process stayed alive and well. So to let an exception enter the filter's field of view in the guise of a passerby, we placed the throw inside a called function:

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

(The header of the output's first line is a label the experiment program printed itself, and we have kept the original.) The filter saw the GCC-coded exception, returned 1, the handler block landed, `guarded_call` returned normally with -1 — and the catch outside will never get this vote. The SEH scope intercepted the C++ exception passing through, the runtime walked its own unwinding rules to the end, `Loud`'s destructor ran too, and the process stayed alive and well. Only the language-level catch was bypassed wholesale. MSVC's documentation explicitly discourages this kind of mixing — the original words add `might not be what you expect` — and the same-function shape is a C2712 compile-time refusal outright. GCC here has not even a document, and what we measured is what you see. Committed to paper it comes to one instruction: under MinGW, do not let an SEH scope stand in the road between throw and catch. The two mechanisms in one program each mind their own territory.

## How the Other Side Sees It

We close by laying the two sides' dividing lines open. In the same access-error scenario, Linux delivers a signal, the handler receives siginfo, `si_addr` gives the address and `si_code` splits the cause, and to repair the scene inside the handler we must fish the context out of the third parameter's ucontext. Windows delivers an exception code, the filter and VEH receive the EXCEPTION_RECORD, info[0] splits read, write, and DEP, info[1] gives the address, scene repair has the ready-made `EXCEPTION_CONTINUE_EXECUTION`, and the replay is done for you by the dispatcher. After a file is cut short, touching beyond the end gets SIGBUS's reckoning on Linux, while Windows blocks the cut at `SetEndOfFile`, out-of-bounds reads fetch a row of zero fill, and IN_PAGE_ERROR is reserved for genuine I/O failure. Ctrl+C is another fork in the road, and we verified it in passing (e6, with two output lines trimmed): with VEH and `SetConsoleCtrlHandler` both on duty, the console event went out, VEH's dispatch count was zero, and the ctrl handler ran on **another thread** — the documentation's own words are `the system creates a new thread in the process to execute the function`. Console events travel by a new thread and never enter the exception dispatcher at all. We sent both C and Break, and the one delivered stably was CTRL_BREAK. CTRL_C we could not receive in processes launched through the WSL interop chain, and that matter's root cause was later run to ground in [Console Events and APC](../process/02-console-apc.md): it was the ignore bit inherited down the launch chain, and after we released it with one line of `SetConsoleCtrlHandler(NULL, FALSE)`, the directed CTRL_C was received for real. Whether the console window is genuine plays no part in the causality. MinGW's `signal(SIGINT)` is only the CRT's simulation layered on top, and the full mechanism of the real thing we covered completely in that chapter.

One word on the relationship with the tools we already have. Win32 call failures travel the old road of GetLastError plus error_code, with the tools and the two-exit discipline defined in [The Error-Handling Paradigm](../../thinking/02-error-paradigm.md). The exceptions this chapter covers tend to another kind of thing: the call did not return a failure — the execution itself broke down. The two roads do not concern each other. One minds foreseeable failures, the other unforeseeable crashes, and when you write defensive code, we must think of both. For the Linux side's complete script, please proceed to the SIGBUS section of [mmap Memory Mapping: Attaching a File to the Address Space](../../linux/file-io/02-mmap-memory-mapping.md), set it against today's zero fill, and the fork in the road will be clear at a glance.

<ReferenceCard title="References">
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
    title="excpt.h (the __try1/__except1 macros)"
    publisher="mingw-w64 (GitHub mirror)"
    url="https://github.com/mirror/mingw-w64/blob/master/mingw-w64-headers/crt/excpt.h"
  />
  <ReferenceItem
    :id="10"
    title="SEH analysis series (starting at ?p=99)"
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
