---
title: "Stack Headroom, ASan, and Luck"
chapter: 4
order: 4
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "How the 8 MiB stack window gets stretched and then punched through, how ASan intercepts an out-of-bounds write the moment it happens, and a final table gathering every road by which the crash fails to arrive"
reading_time_minutes: 22
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/04-stack-overflow-why-no-crash/04-stack-headroom-asan-and-luck.md
  source_hash: 69949bc86397953645e0fae05eb40f02b444db31c0d39059f5c6b1c3c077a1c7
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Stack Headroom, ASan, and Luck

## Stack Headroom: The 8 MiB Window

One road to no crash is still left, and it is decided by headroom, not by where the write lands. The environment is the same one as in [the first piece](01-stack-frame-neighbors.md); follow it if you want to rerun anything. How big a window does the stack carve out for each process? We will settle it with three layers of evidence: the 8192 (KiB) that ulimit -s reports is one, the line in /proc/self/limits is another, and the [stack] range in /proc/self/maps is the third; we print the last two straight from inside a program. d4a declares a 7 MiB char array, touches both ends of it once, then prints the [stack] range it sees along with the limit:

```cpp
// d4a.cpp — a 7 MiB stack array, hugging the 8 MiB limit without crossing it
#include <cstdio>
#include <cstring>

static void print_stack_facts(const char* tag) {
    char line[512];
    FILE* fp = std::fopen("/proc/self/maps", "r");
    if (fp) {
        while (std::fgets(line, sizeof line, fp))
            if (std::strstr(line, "[stack]"))
                std::printf("%s [maps]   %s", tag, line);
        std::fclose(fp);
    }
    fp = std::fopen("/proc/self/limits", "r");
    if (fp) {
        while (std::fgets(line, sizeof line, fp))
            if (std::strstr(line, "Max stack size"))
                std::printf("%s [limits] %s", tag, line);
        std::fclose(fp);
    }
}

int main() {
    print_stack_facts("begin:");

    static const unsigned N = 7u << 20; // 7 MiB, below the 8 MiB limit
    char big[N];

    big[0]     = 1; // lowest address of the array, the leading edge as the stack grows downward
    big[N - 1] = 2; // highest address of the array, close to rbp

    std::printf("touched both ends of the %u MiB array, still alive\n", N >> 20);
    print_stack_facts("end:  ");
    return 0;
}
```

```text
$ g++ -O0 -g -o d4a d4a.cpp && ./d4a
begin: [maps]   7ffeca672000-7ffecad76000 rw-p 00000000 00:00 0                          [stack]
begin: [limits] Max stack size            8388608              unlimited            bytes
touched both ends of the 7 MiB array, still alive
end:   [maps]   7ffeca672000-7ffecad76000 rw-p 00000000 00:00 0                          [stack]
end:   [limits] Max stack size            8388608              unlimited            bytes
exit=0
```

The soft limit's 8388608 bytes is exactly 8 MiB, the hard limit says unlimited, both ends of the 7 MiB array got touched, and the program is alive and well. Now look at the two maps lines: the [stack] range printed at begin and at end is identical, a 0x704000-byte stretch, roughly 7 MiB. And here is something we did not expect: a fresh process's initial stack should not be 7 MiB. We wrote a minimal probe that prints nothing but its own [stack] line, and ran it five times in a row:

```text
$ for i in 1 2 3 4 5; do ./probe_maps; done
7ffc7707a000-7ffc7709c000 rw-p 00000000 00:00 0                          [stack]
7ffc8b4de000-7ffc8b500000 rw-p 00000000 00:00 0                          [stack]
7ffce3779000-7ffce379b000 rw-p 00000000 00:00 0                          [stack]
7ffd930ad000-7ffd930cf000 rw-p 00000000 00:00 0                          [stack]
7ffdaabc0000-7ffdaabe2000 rw-p 00000000 00:00 0                          [stack]
```

Every fresh process starts out with a [stack] of 0x22000. That works out to 136 KiB. So d4a's 7 MiB was stretched into place, and the stretching happened before the begin line ever printed. The answer sits in d4a's prologue (the arrow comments are ours; the raw assembly output does not have them):

```asm
0000000000001307 <main>:
    1307:	push   %rbp
    1308:	mov    %rsp,%rbp
    130b:	sub    $0x700010,%rsp        <- moves rsp down silently, touching no page
    ...
    132b:	call   1189 <print_stack_facts(char const*)>   <- this call pushes the return address at the bottom of the frame, the first touch
    1330:	movb   $0x1,-0x700010(%rbp)   <- big[0]
```

sub only moves registers and touches no memory, so rsp crossed the 7 MiB without a sound. What actually landed on a new page was the return-address push of `call print_stack_facts`, and it falls on the bottommost page of the 7 MiB frame. The kernel keeps track of the stack in units of the VMA (virtual memory area, the entry by which the kernel registers one address range for a process), and the stack's VMA is allowed to grow downward. This particular growth was granted all the way down to the frame's bottom page, and the [stack] range snapped from 136 KiB to roughly 7 MiB in an instant; by the time begin printed, the growth was already finished, which is why begin and end report the very same range. One more thing reads out of this listing: GCC 16 on this machine does not turn on -fstack-clash-protection by default, so the prologue carries no page-by-page probing, and a single big sub crosses the whole 7 MiB directly. Stack clash protection exists to treat exactly this scenario; a build with it on rewrites the prologue into a touch-each-page form, and you will meet it often in a distro's hardened builds.

What happens if we push the array to 8 MiB, flush against the limit, and try again? d4b changes nothing but the array size:

```cpp
// d4b.cpp — an 8 MiB stack array, exactly at the 8 MiB limit
// progress lines go to stderr (unbuffered), so nothing already printed is lost on the way down
#include <cstdio>

int main() {
    static const unsigned N = 8u << 20; // 8 MiB, equal to the ulimit -s limit
    char big[N];

    std::fprintf(stderr, "frame set up, array declared, nothing touched yet\n");

    big[N - 1] = 2; // highest address of the array, nearest to rbp
    std::fprintf(stderr, "high end (big[N-1]) touched, still alive\n");

    big[0] = 1; // lowest address of the array, an access past the 8 MiB boundary
    std::fprintf(stderr, "low end (big[0]) touched, still alive\n");
    return 0;
}
```

```text
$ g++ -O0 -g -o d4b d4b.cpp && ./d4b
exit=139
```

We never got a single line of output, not even the shadow of that first frame set up line, before the exit code came back 139. For the crash site we turn to the assembly and gdb (the assembly is an excerpt):

```asm
0000000000001149 <main>:
    1149:	push   %rbp
    114a:	mov    %rsp,%rbp
    114d:	sub    $0x800010,%rsp
    ...
    117e:	call   1040 <fwrite@plt>     <- fprintf got rewritten into fwrite; this is the first call
    11a7:	movb   $0x1,-0x800010(%rbp)  <- big[0], never executed
```

```text
$ gdb -batch -q -ex 'set debuginfod enabled off' -ex run \
      -ex 'info registers rip rsp rbp' -ex bt \
      --args ./d4b 2>&1 | grep -v 'libthread_db\|Thread debugging'
Program received signal SIGSEGV, Segmentation fault.
0x000055555555517e in main () at d4b.cpp:9
9	    std::fprintf(stderr, "frame set up, array declared, nothing touched yet\n");
rip            0x555555555517e      0x555555555517e <main()+53>
rsp            0x7fffff7fd0c0      0x7fffff7fd0c0
rbp            0x7fffffffd0d0      0x7fffffffd0d0
#0  0x000055555555517e in main () at d4b.cpp:9
```

Let's line the numbers up. We measured rbp = 0x7fffffffd0d0, and rsp after `sub $0x800010` came to rest at 0x7fffff7fd0c0. The floor of the 8 MiB limit sits at about 0x7fffff7fe000; take the stack top 0x7fffffffe000 minus 0x800000, and the difference is that address. main's first call is `call fwrite` (fprintf got rewritten into fwrite; the instruction marked main+53 in the listing is it), and it has to push the return address at rsp-8. That position is already past the floor, the kernel refuses to grow the stack any further down, and the SIGSEGV lands on this call. Not one array element was ever touched: the frame itself plus the overhead of the environment and argv (rbp sits 0xf30 below the stack top) had used up the 8 MiB limit, and what crashed was the first function call, queued right behind them.

One detail here runs against intuition, and it deserves an extra look: the crash point has nothing to do with big[0], the line that looks most suspicious in the source. Not a single array element was touched. Which instruction the crash lands on depends on which memory access crosses the stack region's boundary page first, and there is no fixed correspondence with which line of source the array subscript sits on. If our story were "the array wrote straight through the stack", that would be an assumption, and the d4b scene is sitting right there contradicting it.

At this point the collection is complete. Every road by which an out-of-bounds write fails to crash, five of them, each with the piece where we met it:

| Why the crash never came | Where we saw it |
|---|---|
| A data neighbor in the same frame | The first piece: arr[-1] turned scalar from 111 into 999, and the program exited as usual |
| A write that landed in padding and alignment gaps | The third piece: the 8 bytes of padding at idx 4 and 5 in the -O2 build of f |
| The compiler reordered the variables | The third piece moved the array to rsp+0 and the scalar into the instruction stream; the first piece saw the protection level reshuffle the layout |
| A value nobody reads again | The third piece's red zone, where 999 was read back exactly once |
| An out-of-bounds write that stayed inside the 8 MiB stack window | This piece: the 7 MiB array, both ends touched, still alive |

## The Second Line of Defense: ASan Stops the Write Where It Happens

The canary (a sentinel value the stack protector plants) performs one sentinel comparison before return, and it reports nothing about direction, distance, or victim. When you want the scene in full detail, bring in AddressSanitizer (ASan, a memory-error detector wired in at compile time with -fsanitize=address, the tool we used back in topic 02 to watch a recursive stack overflow). Its principle in one sentence is shadow memory: every 8 bytes of real memory get 1 byte of bookkeeping that records whether those 8 bytes may be touched, the instrumented instructions check that bookkeeping before every memory access, and on finding a forbidden one they stop the process on the spot and print a report. It rearranges the stack frame too: objects that might overflow (arrays and their kind) get poisoned padding on both sides, and a write that steps out of bounds lands on the poison and is caught in the act.

We cut the overflow down to a minimal version that steps out exactly one slot: g_index holds 4, one slot past the upper boundary of `int arr[4]`:

```cpp
// d6.cpp — the smallest overflow there is, one step past the end, caught by ASan on the spot
#include <cstdio>

volatile int g_index   = 4; // arr[4] is exactly one step past the upper boundary of int arr[4]
volatile int g_payload = 999;

__attribute__((noinline))
void f() {
    int arr[4] = {1, 2, 3, 4};
    int scalar = 111;

    std::printf("before: scalar=%d arr={%d,%d,%d,%d}\n",
                scalar, arr[0], arr[1], arr[2], arr[3]);

    arr[g_index] = g_payload; // the out-of-bounds write

    std::printf("after : scalar=%d arr[g_index]=%d\n",
                scalar, arr[g_index]);
}

int main() {
    f();
    std::printf("main: returned from f, program ends normally\n");
    return 0;
}
```

```text
$ g++ -O0 -g -fsanitize=address -o d6_asan d6.cpp && ./d6_asan
=================================================================
==181327==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x7921c1ef0030 at pc 0x579d0b86a3c7 bp 0x7ffc4f6d8050 sp 0x7ffc4f6d8040
WRITE of size 4 at 0x7921c1ef0030 thread T0
    #0 0x579d0b86a3c6 in f() /tmp/cpu-book-04/d6.cpp:15
    #1 0x579d0b86a496 in main /tmp/cpu-book-04/d6.cpp:22
    #2 0x7d21c4227780  (/usr/lib/libc.so.6+0x27780) (BuildId: 503200d7fda94a5dc6058d7e0694e5d1dcb2e372)
    #3 0x7d21c42278b8  __libc_start_main (/usr/lib/libc.so.6+0x278b8) (BuildId: 503200d7fda94a5dc6058d7e0694e5d1dcb2e372)
    #4 0x579d0b86a0f4  _start (/tmp/cpu-book-04/d6_asan+0x10f4) (BuildId: 0e05d556e4f2343277a9428ab3b6f4b9bb0349f3)

Address 0x7921c1ef0030 is located in stack of thread T0 at offset 48 in frame
    #0 0x579d0b86a1d8 in f() /tmp/cpu-book-04/d6.cpp:8

  This frame has 1 object(s):
    [32, 48) 'arr' (line 9) <== Memory access at offset 48 overflows this variable
HINT: this may be a false positive if your program uses some custom stack unwind mechanism, swapcontext or vfork
      (longjmp and C++ exceptions *are* supported)
SUMMARY: AddressSanitizer: stack-buffer-overflow /tmp/cpu-book-04/d6.cpp:15 in f()
Shadow bytes around the buggy address:
  0x7921c1eefd80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x7921c1eefe00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x7921c1eefe80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x7921c1eeff00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  0x7921c1eeff80: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
=>0x7921c1ef0000: f1 f1 f1 f1 00 00[f3]f3 00 00 00 00 00 00 00 00
  0x7921c1ef0080: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
  ...(remaining lines elided)
Shadow byte legend (one shadow byte represents 8 application bytes):
  Addressable:           00
  Partially addressable: 01 02 03 04 05 06 07
  Heap left redzone:       fa
  Freed heap region:       fd
  Stack left redzone:      f1
  Stack mid redzone:       f2
  Stack right redzone:     f3
  Stack after return:      f5
  Stack use after scope:   f8
  Global redzone:          f9
  Global init order:       f6
  Poisoned by user:        f7
  Container overflow:      fc
  Array cookie:            ac
  Intra object redzone:    bb
  ASan internal:           fe
  Left alloca redzone:     ca
  Right alloca redzone:    cb
==181327==ABORTING
exit=1
```

Let's read the report's fields one at a time. The frame object table reads `This frame has 1 object(s): [32, 48) 'arr'`, which means ASan rearranged the frame: arr was housed alone in the range from offset 32 to 48, with poison on both sides. Notice that the list contains only arr, no scalar. ASan puts redzones around the objects that can overflow, arrays and their kind; a plain scalar never makes the protected list. Its body still lives in the frame, it just does not appear in the list. So don't phrase it as "ASan fences in every local variable"; what it fences in is the class of objects capable of stepping out of bounds. The hit line reads `Memory access at offset 48 overflows this variable`: the write landed on the first step outside the right edge of [32,48), the size of the write (WRITE of size 4) is in the report too, and the location is pinned down to d6.cpp:15 in frame #0.

Now look at the line the => arrow marks in the shadow map, which is direct evidence of the redzone layout. One shadow byte stands for 8 bytes of real memory: f1 f1 f1 f1 is the left redzone's 32 bytes, 00 00 is arr's own 16 addressable bytes, the bracketed [f3] is the shadow byte this write hit, f3 f3 together are the right redzone's 16 bytes, and the 00 bytes after them are the rest of the frame. The legend names them as well: f1 goes by Stack left redzone, f3 by Stack right redzone. One easily confused pair of names deserves a sentence here: ASan's redzone and the x86-64 red zone from topic 02 share a name but not an identity. The former is the poisoned padding ASan pads in; the latter is the 128 bytes below rsp that the ABI leaves to leaf functions.

One more engineering detail is hiding in the run: the before line on stdout is gone. At the moment the process aborted, the block buffer on stdout was never flushed; the report itself travels on stderr, and it came through complete. If you build this kind of demo yourself, either flush stdout honestly with fflush(stdout) or send the progress lines down the stderr road, which is what we did in d3 and d4b.

The same round of testing works on one more source file. d1.cpp from the first piece (the original, with g_index at -1, the one that silently corrupted scalar under bare -O0) goes to ASan unchanged:

```text
$ g++ -O0 -g -fsanitize=address -o d1_asan d1.cpp && ./d1_asan
==181448==ERROR: AddressSanitizer: stack-buffer-underflow on address 0x77ee0d0f001c at pc 0x59e780e7f3c7 bp 0x7ffe6655a9a0 sp 0x7ffe6655a990
WRITE of size 4 at 0x77ee0d0f001c thread T0
    #0 0x59e780e7f3c6 in f() /tmp/cpu-book-04/d1.cpp:18
    #1 0x59e780e7f496 in main /tmp/cpu-book-04/d1.cpp:25
    ...(libc frames and the SUMMARY line elided, same shape as the previous report)

Address 0x77ee0d0f001c is located in stack of thread T0 at offset 28 in frame
    #0 0x59e780e7f1d8 in f() /tmp/cpu-book-04/d1.cpp:11

  This frame has 1 object(s):
    [32, 48) 'arr' (line 12) <== Memory access at offset 28 underflows this variable
...(the HINT and SUMMARY lines and the all-00 shadow rows around => elided)
=>0x77ee0d0f0000: f1 f1 f1[f1]00 00 f3 f3 00 00 00 00 00 00 00 00
...(shadow byte legend same as the previous report, elided)
==181448==ABORTING
exit=1
```

The negative subscript hit the left redzone (offset 28, the fourth f1 in the shadow map), and the error category even gets a name of its own, `stack-buffer-underflow`. One source file, two fates, and by now we have laid both on the table: under bare -O0 it runs quietly all the way to the end, the neighbor rots away and nobody says a word; under ASan it is stopped right at the write instruction at d1.cpp:18. The interception happens where the write happens, with no waiting for the function to return, and that is exactly where it beats the canary: the canary guards a single sentinel, covers only the high-address direction, and checks only at return, while ASan reports the direction, the byte count, and the object's range all at once.

## No Crash Is Luck, Not Permission

The program didn't crash, printf did its job as usual, the exit code was still 0. Watched from start to finish, the scene is easy to read as "nothing wrong with this way of writing code". Put the standard's position next to it: an out-of-bounds write is undefined behavior, and the standard's stance is that it promises nothing. The compiler is free to arrange variables however it likes and optimize however it likes, and whatever result comes out counts as correct. So the "didn't crash" we watched today is one concrete arrangement, on this machine's build of GCC, under this particular round of options. Change the compiler version, change the optimization level, change the shape of the function, and the layout rearranges itself; the landing spot of the very same out-of-bounds write moves with it, and the arr[-1] that stayed quiet last time may be sitting on somebody's return address the next time around. This time we missed the vitals, and luck is the reason. Nothing in the standard ever promised that no crash means you may write this way.

A few places to go next, picked for you. An overflow on the heap is a different scene, its layout decided by the allocator and malloc's metadata, and the crash-lab volume on this site has the full forensic practice: [how to read a crash scene](../../../crash-lab/a-memory-safety/01-null-deref.md) trains the craft of reading a scene with gdb. We do not take up the systematic treatment of C strings and buffer safety in this piece; why strcpy and its kind are dangerous, and what the safe alternatives look like, is in [vol1's piece on buffer safety](../../../vol1-fundamentals/c_tutorials/11-c-strings-and-buffer-safety.md). For how a stack frame stands up, [the stack frame walkthrough in topic 02](../02-function-calls-and-stack-frames/01-call-and-stack-frame-walkthrough.md) is the place to go back to.

The next piece in the series enters the world of member functions, and the this pointer previewed at the end of [the third piece](../03-arguments-in-registers/03-floats-structs-and-abi-docs.md) finally takes the stage, walking exactly that rdi position. And we leave you with the cheapest self-check there is: compile d1.cpp twice, once bare at -O0 and once with -fsanitize=address. The first run finishes quietly (exit code 0); the second stops the process on the spot, at the very line of the write.
