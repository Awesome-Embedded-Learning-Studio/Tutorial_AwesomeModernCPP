---
title: "Neighbors in the Stack Frame: Who Takes the Out-of-Bounds Write"
chapter: 4
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "Measuring the layout of f's stack frame with objdump and gdb, seeing that arr[-1] hits scalar, then verifying that what decides the layout is the stack protector level rather than declaration order"
reading_time_minutes: 19
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/04-stack-overflow-why-no-crash/01-stack-frame-neighbors.md
  source_hash: 9ba0351741efaf632e23f51f7c1b331f44c30ad78ad1c0ed616de23d39b01653
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Neighbors in the Stack Frame: Who Takes the Out-of-Bounds Write

In the [previous piece](../03-arguments-in-registers/01-integer-args-register-allocation.md) we followed the arguments through their handoff in the registers, and in [the piece before that](../02-function-calls-and-stack-frames/01-call-and-stack-frame-walkthrough.md) we built a stack frame up from its parts: above rbp sit the saved rbp and the return address, and the locals live in the region at negative offsets from rbp. This piece does something dangerous on the frame we built in 02, writing past the end of a four-element array on the stack. Intuition says a write past the end should blow up on the spot, yet the program below runs to completion without a fuss: printf finishes its job as if nothing happened, and the exit code is a clean 0.

The program we use is small enough to paste in full. Inside f we put an `int arr[4]` and a scalar named scalar, then use a negative index to write a 999 past the end of the array:

```cpp
// d1.cpp -- the main source for the out-of-bounds write experiment
// The same code gets built at -O0 and -O2, and handed to ASan again later
// Both the index and the payload go through volatile globals, so the compiler cannot fold them
// For how the stack layout was measured, see the end of the layout section
#include <cstdio>

volatile int g_index   = -1;  // the out-of-bounds index; why -1 becomes clear once you see the measured layout
volatile int g_payload = 999; // the value written out of bounds

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

Compiling and running takes two commands, and the output is pasted below exactly as it appeared (the exit=<number> at the end of each run block is the exit code echoed with echo $? right after the command; I won't repeat this explanation later):

```text
$ g++ -O0 -g -o d1_o0 d1.cpp
$ ./d1_o0
before: scalar=111 arr={1,2,3,4}
after : scalar=999 arr[g_index]=999
main: returned from f, program ends normally
exit=0
```

Look at the after line: scalar was 111 and is now 999. The array we declared has only 4 elements, so arr[-1] is not inside the array's bounds at all, yet the write landed without a scratch and clobbered a neighbor in the same frame, and the program still ran to the end and exited normally. The questions this piece answers are sitting right there: why didn't it crash? What exactly did the write hit? How far does it have to reach before something breaks?

Let's also fix the naming. Writing past the end of an array is what the standard calls undefined behavior. In plain words, the standard washes its hands of the matter and leaves everything to the implementation: how the compiler arranges the variables, what it optimizes them into, what the program does when it runs, the standard will call all of it correct. So what this piece really examines is the actual arrangement made by this build of the compiler on my machine. From here on we draw no layout from memory; every judgment comes from disassembly and measurements taken in the debugger.

Every example and experiment in this piece is original to this site. Every block of output you see in the rest of it comes from a real run on my machine.

## Our Test Environment

Following this site's convention, the environment comes first, so that every block of assembly and every session below comes from the same setup:

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is
NO warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.

$ gdb --version | head -1
GNU gdb (GDB) 17.2

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux

$ ulimit -s
8192

$ cat /proc/sys/kernel/randomize_va_space
2
```

Several of these settings come back later. The soft limit on the stack is 8192 KiB, exactly 8 MiB (that's the ulimit -s line), and in the [fourth piece](04-stack-headroom-asan-and-luck.md) it becomes the floor under us when we measure how much stack headroom a program has. randomize_va_space reads 2, meaning address space randomization is at its maximum and the stack's baseline moves on every run; [02](../02-function-calls-and-stack-frames/02-leaf-functions-stack-direction-and-o2.md) saw its effect in the section on nested calls.

The distro turns the stack protector on by default. The environment list in [01](../01-prefix-vs-postfix-increment/01-int-instructions.md) already introduced -fstack-protector-strong: functions with locals like arrays carry extra instructions that load and verify a canary (a sentinel value the stack protector plants). This topic is precisely a close-up study of that machinery. As for whether my machine's default level actually equals strong, I won't take that equation on faith; the [third piece](03-canary-defense-and-the-o2-story.md) checks it face to face with an instruction-by-instruction diff.

One more piece of evidence discipline, carried over from 02: a non-interactive WSL2 shell does not print the words Segmentation fault, so at the shell level the only evidence is the exit code. 139 minus 128 gives 11, the number of SIGSEGV; 134 minus 128 gives 6, the number of SIGABRT. Wherever the text SIGSEGV or SIGABRT appears in this piece, it comes either from gdb's Program received signal line or from the program's own stderr. The `*** stack smashing detected ***` line is what glibc writes to stderr; we can see it in the shell, so it gets pasted as-is.

## Who Lives in f's Stack Frame

To find out who arr[-1] hits, let's lay f's stack frame out on the table and match every address to its occupant. 02 explained that at -O0 GCC uses rbp as the frame's anchor: rbp+8 holds the return address, rbp itself points at the saved rbp, and the locals live in the negative-offset region. The f in d1 has one thing the 02 example didn't: the canary from the environment section, and its home is rbp-0x8. We don't guess at who goes where; we survey the site with objdump:

```text
$ objdump -d --no-show-raw-insn -C d1_o0 | sed -n '/<f()>:/,/^$/p'
```

```asm
0000000000001159 <f()>:
    1159:	push   %rbp
    115a:	mov    %rsp,%rbp
    115d:	sub    $0x30,%rsp
    1161:	mov    %fs:0x28,%rax
    116a:	mov    %rax,-0x8(%rbp)
    116e:	xor    %eax,%eax
    1170:	movl   $0x1,-0x20(%rbp)
    1177:	movl   $0x2,-0x1c(%rbp)
    117e:	movl   $0x3,-0x18(%rbp)
    1185:	movl   $0x4,-0x14(%rbp)
    118c:	movl   $0x6f,-0x24(%rbp)
    1193:	mov    -0x14(%rbp),%r8d
    ...(argument shuffling for printf, elided)...
    11b7:	call   1030 <printf@plt>
    11bc:	mov    0x2e6a(%rip),%edx        # 402c <g_payload>
    11c2:	mov    0x2e60(%rip),%eax        # 4028 <g_index>
    11c8:	cltq
    11ca:	mov    %edx,-0x20(%rbp,%rax,4)
    ...(argument shuffling for the second printf, elided)...
    11f3:	nop
    11f4:	mov    -0x8(%rbp),%rax
    11f8:	sub    %fs:0x28,%rax
    1201:	je     1208 <f()+0xaf>
    1203:	call   1040 <__stack_chk_fail@plt>
    1208:	leave
    1209:	ret
```

Reading the initialization instructions line by line gives us the slot assignment; the constant in each line is the evidence:

| Slot | Offset | Evidence |
|---|---|---|
| Canary | rbp-0x8 | loaded with `mov %fs:0x28,%rax`, then stored to `-0x8(%rbp)` |
| arr[0..3] | rbp-0x20 .. rbp-0x14 | the four initializations starting at `movl $0x1,-0x20(%rbp)` |
| scalar | rbp-0x24 | `movl $0x6f,-0x24(%rbp)`, and 0x6f is 111 |
| Out-of-bounds addressing | base rbp-0x20, stride 4 | `mov %edx,-0x20(%rbp,%rax,4)` |

Put arr's offsets next to scalar's and the crux shows itself: rbp-0x24 is lower than rbp-0x20, so the scalar lives below the array. That is the opposite of the picture in quite a few older textbooks, where the array sits at the low end and the scalar sits higher up, so that marching upward with positive indices lands you exactly on the scalar. On this machine, GCC 16 at -O0 under the default stack protector level arranges things differently: the array is placed just below the canary, at the high end of the locals area, and the scalar is moved underneath the array. We compute the index from the measured offsets. The out-of-bounds addressing uses base rbp-0x20 with a stride of 4 bytes, and scalar's slot relative to that base is (rbp-0x24 - (rbp-0x20)) / 4 = -1. That is why g_index is set to -1 in the source, and arr[-1] lands squarely on scalar. Now strike in the positive direction: arr[4] falls at rbp-0x10 and arr[5] at rbp-0xc, and those 8 bytes are padding nobody lives in. To touch the canary you have to reach arr[6]; arr[8] gets to the saved rbp, and arr[10] to the return address. We measure that distance head-on in the [next piece](02-hitting-canary-saved-rbp-and-return-address.md).

We reserve a spot here for a diagram of f's stack frame layout, still to be added: read from the return address downward it will show the saved rbp, the canary (rbp-0x8), the 8 bytes of padding, the four cells of arr (rbp-0x20 to rbp-0x14), and scalar (rbp-0x24), drawn in the same visual system as 02's stack frame diagram and filled in later by this site's drawing pipeline.

Half of why the program didn't crash is already readable off the layout: the write landed inside the ordinary variable area of this same frame, far enough from the control data (the canary, the saved rbp, the return address), so the only casualty was scalar, one data neighbor. Claiming we hit it is not proof. Let's have gdb match the claim against memory, with the breakpoint on the source line of the second printf, the moment after the write and before the return:

```text
$ gdb -batch -q \
      -ex 'break d1.cpp:20' -ex run \
      -ex 'printf "== 写入后 f 帧内内存(从 rbp-0x28 往上) ==\n"' \
      -ex 'x/10wx $rbp-0x28' \
      -ex 'printf "== 关键槽位 ==\n"' \
      -ex 'printf "scalar 槽 rbp-0x24 = %d\ncanary 槽 rbp-0x8  = 0x%lx\n", *(int*)($rbp-0x24), *(unsigned long*)($rbp-0x8)' \
      -ex 'info frame' \
      --args ./d1_o0
```

(The == separator lines in the session come from the printf commands we had gdb run, and their labels are in Chinese: in-frame memory after the write, scanned upward from rbp-0x28, then the key slots. Housekeeping lines like Breakpoint 1 at, the debuginfod prompts, and the thread-library notices were deleted; everything else is verbatim.)

```text
Breakpoint 1, f () at d1.cpp:20
20	    std::printf("after : scalar=%d arr[g_index]=%d\n",
== 写入后 f 帧内内存(从 rbp-0x28 往上) ==
0x7fffffffd098:	0xf7ebd538	0x000003e7	0x00000001	0x00000002
0x7fffffffd0a8:	0x00000003	0x00000004	0x00000001	0x00000000
0x7fffffffd0b8:	0xef969100	0x2364d555
== 关键槽位 ==
scalar 槽 rbp-0x24 = 999
canary 槽 rbp-0x8  = 0x2364d555ef969100
Stack level 0, frame at 0x7fffffffd0d0:
 rip = 0x5555555551d4 in f (d1.cpp:20); saved rip = 0x555555555213
 called by frame at 0x7fffffffd0e0
 source language c++.
 Arglist at 0x7fffffffd0c0, args:
 Locals at 0x7fffffffd0c0, Previous frame's sp is 0x7fffffffd0d0
 Saved registers:
  rbp at 0x7fffffffd0c0, rip at 0x7fffffffd0c8
```

Let's check it line by line. The Saved registers part of info frame tells us where rbp landed: 0x7fffffffd0c0. Our x/10wx reads ten 4-byte words starting at rbp-0x28 and stops right at rbp. The 0x3e7 at 0x...d09c is 999, and its position is exactly rbp-0x24, scalar's slot. The four cells from 0x...d0a0 through 0x...d0ac are arr's 1, 2, 3, 4. From 0x...d0b8 up sits the intact canary 0x2364d555ef969100. The two padding cells at rbp-0x10 and rbp-0xc still hold old values, but search the slot assignment as we might, no variable claims them. The write's target, the casualty, and the survivors are all lying in memory right in front of us.

Every number from the opening scene now has its answer: arr[-1] hit scalar, changing it from 111 to 999; the canary and the return address are intact; f returned normally and the exit code stayed 0. Who ends up above the array and who ends up below it is GCC's call, and our index was computed from measured offsets. The next section verifies who actually gets to decide that arrangement.

## The Protection Level Decides the Layout, Not Declaration Order

Our first suspect is declaration order: if the scalar is declared before the array, does it get to live higher up? We built a four-way comparison to interrogate it. The two sources d7a and d7b differ only in that the order of the two declarations is swapped, and we compile each of them twice, once at the default level (canary on) and once with -fno-stack-protector added (protection removed):

```cpp
// d7a.cpp -- the one that declares the array first
__attribute__((noinline))
void f() {
    int arr[4];
    int scalar;

    arr[0] = 0x11111111; // find movl $0x11111111,-0xXX(%rbp) in the assembly to locate the array base
    scalar = 0x22222222; // find movl $0x22222222,-0xYY(%rbp) to locate the scalar

    arr[1] = scalar; // use both variables
}

int main() {
    f();
    return 0;
}
```

```text
g++ -O0 -g -o d7a_p d7a.cpp            # default (strong)
g++ -O0 -g -o d7b_p d7b.cpp
g++ -O0 -g -fno-stack-protector -o d7a_n d7a.cpp
g++ -O0 -g -fno-stack-protector -o d7b_n d7b.cpp
```

We picked the two instantly recognizable constants 0x11111111 and 0x22222222 precisely so that the two variables' slots could be identified straight from the assembly. The output at the protected level follows (d7b_p's output is identical to it instruction for instruction, so we skip pasting it):

```asm
0000000000001139 <f()>:
    1139:	push   %rbp
    113a:	mov    %rsp,%rbp
    113d:	sub    $0x30,%rsp
    1141:	mov    %fs:0x28,%rax
    114a:	mov    %rax,-0x8(%rbp)
    114e:	xor    %eax,%eax
    1150:	movl   $0x11111111,-0x20(%rbp)   <- arr base rbp-0x20
    1157:	movl   $0x22222222,-0x24(%rbp)   <- scalar at rbp-0x24, below the array
    115e:	mov    -0x24(%rbp),%eax
    1161:	mov    %eax,-0x1c(%rbp)
    1164:	nop
    1165:	mov    -0x8(%rbp),%rax
    1169:	sub    %fs:0x28,%rax
    1172:	je     1179 <f()+0x40>
    1174:	call   1030 <__stack_chk_fail@plt>
    1179:	leave
    117a:	ret
```

The two builds with protection removed look like this instead (d7b_n is identical to d7a_n instruction for instruction, so we skip pasting it):

```asm
0000000000001119 <f()>:
    1119:	push   %rbp
    111a:	mov    %rsp,%rbp
    111d:	movl   $0x11111111,-0x20(%rbp)   <- arr base rbp-0x20
    1124:	movl   $0x22222222,-0x4(%rbp)    <- scalar at rbp-0x4, above the array
    112b:	mov    -0x4(%rbp),%eax
    112e:	mov    %eax,-0x1c(%rbp)
    1131:	nop
    1132:	pop    %rbp
    1133:	ret
```

The four layouts are gathered into one table (the arrow annotations in the assembly are ours; the original objdump output has none of them):

| Build | arr base | scalar | Relative position | Next to the canary |
|---|---|---|---|---|
| d7a_p (array declared first, strong) | rbp-0x20 | rbp-0x24 | scalar below the array | the array (8 bytes of padding in between) |
| d7b_p (scalar declared first, strong) | rbp-0x20 | rbp-0x24 | same as above | same as above |
| d7a_n (array declared first, no protector) | rbp-0x20 | rbp-0x4 | scalar above the array | none (no canary; the scalar sits against the saved rbp) |
| d7b_n (scalar declared first, no protector) | rbp-0x20 | rbp-0x4 | same as above | none |

The four-way comparison hands down a crisp verdict. The first part we have already spoiled: declaration order has zero effect on the -O0 layout. Within each pair the outputs match instruction for instruction and the addresses do not move by a hair, so trying to arrange things by declaring the scalar before the array accomplishes nothing on GCC 16. What does the work is whether the canary exists. With protection on, the array is placed just below the canary at the high end and the scalar is moved underneath the array, so a forward out-of-bounds write can never reach the scalar; the only way to touch it is a negative index, and the opening experiment was a product of exactly this layout. We measured the world after the canary comes off too: the scalar is repacked to rbp-0x4, wedged between the array and the saved rbp, and now a forward out-of-bounds arr[7] overwrites the scalar directly, while arr[8] reaches the saved rbp. It is the array that lives against the canary, and there lies the stack protector's design intent: the thing most likely to run out of bounds is placed closest to the control data, with a sentinel standing in between.

We leave you the method for measuring offsets as well; it beats any memorized number for reliability. Once you have objdump's disassembly, look for initialization lines of the form `movl $const,-off(%rbp)` in the output: the constant names each variable's slot outright. With the array base's offset and the target's offset in hand, the index is (target offset minus array base offset) divided by 4. The GCC version, the protector level, and the shape of the function (whether it has a printf, how many arguments) all shift the offsets. Every concrete number in this piece is a measurement from GCC 16.2.1 at -O0. Whatever layout your own binary comes up with, measure it before you judge, and don't treat our offset table as a universal promise. The nearby landing spots are measured; the distance from the array to the canary and the return address is what we measure next.
