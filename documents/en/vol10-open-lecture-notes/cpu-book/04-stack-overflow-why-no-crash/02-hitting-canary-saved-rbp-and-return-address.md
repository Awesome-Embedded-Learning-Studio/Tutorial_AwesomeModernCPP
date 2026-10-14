---
title: "Hitting Farther: The Canary, Saved rbp, and the Return Address"
chapter: 4
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "Push the out-of-bounds index up step by step: hitting the canary gets the return intercepted before it happens, hitting the return address hijacks control flow on the spot, and hitting saved rbp makes the caller the one that crashes"
reading_time_minutes: 7
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/04-stack-overflow-why-no-crash/02-hitting-canary-saved-rbp-and-return-address.md
  source_hash: 2f391fa5c41e4a76050c06dac0e44460e208e82a9de5a4cdcfbbf3b53a71517f
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Hitting Farther: The Canary, Saved rbp, and the Return Address

The out-of-bounds write in the previous piece landed close by and hurt only one data neighbor. We're using the same environment as in [the previous piece](01-stack-frame-neighbors.md), so follow that piece if you want to rerun anything. Next question: what happens when the write lands farther out? We made the index come in from the command line, compiled the same source twice, once with the defaults (canary on, where a canary is a sentinel value the stack protector plants) and once with -fno-stack-protector (protection taken off), and we measured the index conversions for both layouts straight off the disassembly:

```cpp
// d3.cpp: out-of-bounds writes that reach the canary / saved rbp / the return address
// the index comes from the command line. The same source is compiled twice,
// once with the default (stack protector on) and once with -fno-stack-protector
#include <cstdio>
#include <cstdlib>

volatile int g_index   = 0;
volatile int g_payload = 0x1234567; // the value written into the target slot, a bad address whose low 32 bits are easy to recognize

__attribute__((noinline))
void f() {
    int arr[4] = {1, 2, 3, 4};
    arr[g_index] = g_payload; // the out-of-bounds write
    std::fprintf(stderr, "store done (idx=%d), now return\n", (int)g_index);
}

int main(int argc, char** argv) {
    g_index = std::atoi(argv[1]);
    f();
    std::fprintf(stderr, "main: returned from f normally\n");
    return 0;
}
```

All of d3's progress printing goes to stderr, which is unbuffered, so lines already printed survive even when the process dies partway through. Here is f from each build (the fprintf argument-shuffling stretch is elided, and the build labels at the end of the function header lines are ours):

```text
$ g++ -O0 -g -o d3_default d3.cpp
$ g++ -O0 -g -fno-stack-protector -o d3_noprot d3.cpp
```

```asm
0000000000001169 <f()>:                (default build, canary on)
    1169:	push   %rbp
    116a:	mov    %rsp,%rbp
    116d:	sub    $0x20,%rsp
    1171:	mov    %fs:0x28,%rax
    117a:	mov    %rax,-0x8(%rbp)
    117e:	xor    %eax,%eax
    1180:	movl   $0x1,-0x20(%rbp)
    ... (arr[1..3] initialization, elided) ...
    11aa:	mov    %edx,-0x20(%rbp,%rax,4)
    ... (fprintf argument shuffling, elided) ...
    11d3:	mov    -0x8(%rbp),%rax
    11d7:	sub    %fs:0x28,%rax
    11e0:	je     11e7 <f()+0x7e>
    11e2:	call   1040 <__stack_chk_fail@plt>
    11e7:	leave
    11e8:	ret
```

```asm
0000000000001159 <f()>:                (unprotected build)
    1159:	push   %rbp
    115a:	mov    %rsp,%rbp
    115d:	sub    $0x10,%rsp
    1161:	movl   $0x1,-0x10(%rbp)
    1168:	movl   $0x2,-0xc(%rbp)
    116f:	movl   $0x3,-0x8(%rbp)
    1176:	movl   $0x4,-0x4(%rbp)
    117b:	mov    0x2ea5(%rip),%edx        # 4028 <g_payload>
    1183:	mov    0x2ec3(%rip),%eax        # 404c <g_index>
    1189:	cltq
    118b:	mov    %edx,-0x10(%rbp,%rax,4)
    118f:	mov    0x2eb7(%rip),%edx        # 404c <g_index>
    ... (fprintf argument shuffling, elided) ...
    11b3:	nop
    11b4:	leave
    11b5:	ret
```

The default build's f has the same shape as the f in the previous piece's d1: the canary sits at rbp-0x8 and the array base is at rbp-0x20. In the unprotected build the canary is gone, the array base has moved to rbp-0x10, and the frame has shrunk to 0x10. The index conversions for both layouts (the address of arr[k] equals the array base plus 4k) are in this table:

| Target | Default build (base rbp-0x20) | Unprotected build (base rbp-0x10) |
|---|---|---|
| canary (rbp-0x8) | k = 6 | none (this build has no canary) |
| saved rbp (rbp+0x0) | k = 8 | k = 4 |
| return address (rbp+0x8) | k = 10 | k = 6 |

## Hitting the Canary: The Write Succeeds, the Return Is Intercepted

```text
$ ./d3_default 6
store done (idx=6), now return
*** stack smashing detected ***: terminated
exit=134
```

The write succeeded and the progress line came out. Then glibc's `__stack_chk_fail` path printed that line of asterisks to stderr, and the process ended in SIGABRT with exit code 134. Watch the timing: the canary check happens before f returns, and the return address was intact from start to finish. The line of defense stood in front of it. Where that asterisk line comes from was covered in the evidence discipline in the environment section of [the first piece](01-stack-frame-neighbors.md), and the full call-chain evidence waits in the smashed-canary section of [the next piece](03-canary-defense-and-the-o2-story.md).

## Hitting the Return Address: Control Flow Hijacked on the Spot

```text
$ ./d3_noprot 6
store done (idx=6), now return
exit=139
```

One progress line printed, the exit code came back 139, and there was no other text in the shell, which is what a non-interactive shell should give you. This build has no canary, so the same k=6 write lands directly on the low 4 bytes of the return address slot. Let's have gdb gather evidence the whole way through:

```text
$ gdb -batch -q \
      -ex 'set debuginfod enabled off' \
      -ex 'break d3.cpp:13' -ex run \
      -ex 'printf "== 写入之后、返回之前 ==\n"' \
      -ex 'info frame' -ex 'x/12gx $rbp-0x28' \
      -ex 'printf "== continue 到崩溃现场 ==\n"' \
      -ex continue \
      -ex 'info registers rip rsp rbp' \
      -ex 'printf "== 崩溃时 rsp 周围 ==\n"' \
      -ex 'x/6gx $rsp-0x10' -ex bt \
      --args ./d3_noprot 6 2>&1 | grep -v 'libthread_db\|Thread debugging'
```

(Housekeeping lines such as `Breakpoint 1 at` have been removed. The == separator lines likewise come from the printf commands we told gdb to run, and everything else is verbatim.)

```text
Breakpoint 1, f () at d3.cpp:13
13	    std::fprintf(stderr, "store done (idx=%d), now return\n", (int)g_index);
== 写入之后、返回之前 ==
Stack level 0, frame at 0x7fffffffd0a0:
 rip = 0x55555555518f in f (d3.cpp:13); saved rip = 0x555501234567
 called by frame at 0x7fffffffd0a8
 source language c++.
 Arglist at 0x7fffffffd090, args:
 Locals at 0x7fffffffd090, Previous frame's sp is 0x7fffffffd0a0
 Saved registers:
  rbp at 0x7fffffffd090, rip at 0x7fffffffd098
0x7fffffffd068:	0x0000000000000002	0x00007ffff7ffd000
0x7fffffffd078:	0x0000555555557da8	0x0000000200000001
0x7fffffffd088:	0x0000000400000003	0x00007fffffffd0b0
0x7fffffffd098:	0x0000555501234567	0x00007fffffffd1e8
0x7fffffffd0a8:	0x00000002f7ebd538	0x00007fffffffd160
0x7fffffffd0b8:	0x00007ffff7827781	0x00007ffff7ecf248
== continue 到崩溃现场 ==
store done (idx=6), now return

Program received signal SIGSEGV, Segmentation fault.
0x0000555501234567 in ?? ()
rip            0x555501234567      0x555501234567
rsp            0x7fffffffd0a0      0x7fffffffd0a0
rbp            0x7fffffffd0b0      0x7fffffffd0b0
== 崩溃时 rsp 周围 ==
0x7fffffffd090:	0x00007fffffffd0b0	0x0000555501234567
0x7fffffffd0a0:	0x00007fffffffd1e8	0x00000002f7ebd538
0x7fffffffd0b0:	0x00007fffffffd160	0x00007ffff7827781
#0  0x0000555501234567 in ?? ()
#1  0x00007fffffffd1e8 in ?? ()
#2  0x00000002f7ebd538 in ?? ()
#3  0x00007fffffffd160 in ?? ()
Backtrace stopped: previous frame identical to this frame (corrupt stack?)
```

Let's line up the key spots. The pre-return info frame reports `saved rip = 0x555501234567`. That used to be an address inside main (somewhere in the 0x5555555552xx range). The low 32 bits have been overwritten by the payload 0x01234567, and the high 32 bits are still the original 0x00005555. This is exactly the scene a 4-byte int write leaves behind, with half the return address ruined. The memory dump shows it just as plainly. The 0x...d078 and 0x...d088 rows spell out arr's 1, 2, 3, 4. 0x...d090 is the intact saved rbp (0x00007fffffffd0b0), and right next to it, 0x...d098 is the corrupted return address slot, 0x0000555501234567. After continue the program takes a SIGSEGV, the rip at the crash site matches the written-in bad address verbatim, and bt can no longer walk the stack (`corrupt stack?`). For what hijacked control flow looks like, you will not find more direct evidence than this.

## Hitting saved rbp: The Crash Moves to the Caller

```text
$ ./d3_noprot 4
store done (idx=4), now return
main: returned from f normally
exit=139
```

This scene deserves a closer look: f itself returned normally (the return address was fine) and main's progress line printed as well, yet the exit code came back 139. We put the breakpoint where main has just taken control back (the command has the same shape as the previous scene, with the breakpoint moved to d3.cpp:19, the threading-library notice lines filtered the same way, the housekeeping lines removed, and everything else verbatim):

```text
store done (idx=4), now return

Breakpoint 1, main (argc=<error reading variable: Cannot access memory at address 0x7fff01234563>, argv=<error reading variable: Cannot access memory at address 0x7fff01234557>) at d3.cpp:19
19	    std::fprintf(stderr, "main: returned from f normally\n");
== f 已返回、main 刚接回控制权 ==
rbp            0x7fff01234567      0x7fff01234567
rsp            0x7fffffffd0a0      0x7fffffffd0a0
== main 帧里残存的 f 帧:rbp-0x10 起 ==
0x7fff0123455f:	Cannot access memory at address 0x7fff0123455f
== continue 看接下来发生什么 ==
main: returned from f normally

Program received signal SIGSEGV, Segmentation fault.
main (argc=<error reading variable: Cannot access memory at address 0x7fff01234563>, argv=<error reading variable: Cannot access memory at address 0x7fff01234557>) at d3.cpp:21
21	}
rip            0x555555555208      0x555555555208 <main(int, char**)+82>
rsp            0x7fffffffd0a0      0x7fffffffd0a0
rbp            0x7fff01234567      0x7fff01234567
#0  main (argc=<error reading variable: Cannot access memory at address 0x7fff01234563>, argv=<error reading variable: Cannot access memory at address 0x7fff01234557>) at d3.cpp:21
Backtrace stopped: Cannot access memory at address 0x7fff0123456f
```

Let's reconstruct what happened. The `pop %rbp` in f's epilogue loaded the bad value 0x7fff01234567 into the rbp register. The return address was intact, so main could keep running, but from that point on main had lost the ability to address anything through rbp, and gdb could not even read argc and argv on its behalf (`Cannot access memory`). In the end it died on main's own epilogue: leave treats the corrupted rbp as rsp and pops from it, and that is where the SIGSEGV landed. The write that corrupted saved rbp was arr[4]'s out-of-bounds write. f returned normally, and the one that died is main, the function that called it. This late-crash tier is the one most likely to fool you during debugging, because the crash site sits one function return away from where the out-of-bounds write happened, and the stack data there has long since been overwritten by later frames. All three distant targets have had their turn. As for the canary standing in front of the return address, how it gets planted and how it gets checked is what the next piece looks at head-on.
