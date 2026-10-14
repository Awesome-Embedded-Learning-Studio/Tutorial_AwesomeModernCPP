---
title: "The Canary Defense and a Different Story at -O2"
chapter: 4
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "The canary's load, plant, and check read instruction by instruction, three stack-protector levels compared and the default level verified with a diff, then -O2 moves the scalar into the instruction stream and the array to rsp+0"
reading_time_minutes: 8
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/04-stack-overflow-why-no-crash/03-canary-defense-and-the-o2-story.md
  source_hash: 7d87d6a63cb9586d8126967bbda371b23ba77e8c52a566f396258725f4d87d9a
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# The Canary Defense and a Different Story at -O2

The previous piece ended with all three of the distant targets hit. Here we put the defense itself, the thing standing in front of the return address, under direct examination. The environment is the same as in [the first piece](01-stack-frame-neighbors.md), so follow that setup if you want to rerun anything.

## The First Line of Defense: How the Canary Gets Loaded, Checked, and Whom It Catches

Back in piece 1 of the series we brushed past a few instructions touching `%fs:40` and said their job was to catch the kind of damage a stack overflow does. Now we look at them head-on. The canary (a sentinel value the stack protector plants) appeared in d1's f as a complete three-step routine. Step one is loading: `mov %fs:0x28,%rax` reads a random value from offset 0x28 in the fs segment (the %fs:40 written back in piece 1 is the same location; decimal 40 and hexadecimal 0x28 are one and the same place). Step two is planting: the value gets stored into the sentinel slot at rbp-0x8, wedged between the array and the control data. Step three is checking: the `sub %fs:0x28,%rax` just before the return performs one comparison, and on a mismatch comes `call __stack_chk_fail`. That random value is generated when the process starts, and its home is TLS (thread-local storage, private data of which every thread keeps its own copy). The original never sits on the stack, so an out-of-bounds write on the stack cannot reach it, and that is what makes the comparison mean anything.

Not every function carries the three steps; whether they go in is decided by the protection level. We take a pair of functions for comparison: scalar_only holds only scalars, while with_arr holds an array:

```cpp
// d5.cpp — canary planting and checking, assembly compared across three stack-protector levels
#include <cstdio>

__attribute__((noinline))
int scalar_only(int x) { // no array: the strong level plants no canary, the all level does
    int t = x * 3;
    return t + 1;
}

__attribute__((noinline))
int with_arr(int x) { // has an array: strong and all both plant a canary
    int arr[4] = {x, x + 1, x + 2, x + 3};
    int s = 0;
    for (int i = 0; i < 4; ++i)
        s += arr[i];
    return s;
}

int main(int argc, char**) {
    std::printf("scalar_only=%d with_arr=%d\n",
                scalar_only(argc), with_arr(argc));
    return 0;
}
```

```text
g++ -O0 -g -o d5_strong d5.cpp                 # default
g++ -O0 -g -fstack-protector-all -o d5_all d5.cpp
g++ -O0 -g -fno-stack-protector -o d5_none d5.cpp
./d5_strong && ./d5_all && ./d5_none     # all three print scalar_only=4 with_arr=10
```

All our comparisons use -O0, and one failed attempt has to be reported here: the level comparison was originally tried at -O2, but at -O2 with_arr folded entirely into a single `lea 0x6(,%rdi,4),%eax`. The array was nowhere in the instruction stream, the canary went away with it, and the levels no longer had anything to differ on. So this whole piece discusses layout and protection levels at -O0, and the -O2 world gets a section of its own next.

Under the default level, scalar_only shows no canary anywhere in the assembly:

```asm
0000000000001149 <scalar_only(int)>:
    1149:	push   %rbp
    114a:	mov    %rsp,%rbp
    114d:	mov    %edi,-0x14(%rbp)
    ... (pure arithmetic, omitted)
    1162:	pop    %rbp
    1163:	ret
```

Now switch to -fstack-protector-all, and both ends get planted:

```asm
0000000000001149 <scalar_only(int)>:
    1149:	push   %rbp
    114a:	mov    %rsp,%rbp
    114d:	sub    $0x20,%rsp
    1151:	mov    %edi,-0x14(%rbp)
    1154:	mov    %fs:0x28,%rax        <- load the canary
    115d:	mov    %rax,-0x8(%rbp)      <- store into rbp-0x8
    ... (pure arithmetic, omitted)
    1175:	mov    -0x8(%rbp),%rdx      <- read back
    1179:	sub    %fs:0x28,%rdx        <- compare against the original
    1182:	je     1189 <scalar_only(int)+0x40>
    1184:	call   1040 <__stack_chk_fail@plt>
    1189:	leave
    118a:	ret
```

The -fno-stack-protector output matches the default level and carries no trace of a canary either, so we won't paste it twice. with_arr, the one with the array, has both ends present even under the default level (arr lives at rbp-0x20 again; feel free to check the layout one more time):

```asm
0000000000001164 <with_arr(int)>:
    1164:	push   %rbp
    1165:	mov    %rsp,%rbp
    1168:	sub    $0x40,%rsp
    116c:	mov    %edi,-0x34(%rbp)
    116f:	mov    %fs:0x28,%rax        <- load
    1178:	mov    %rax,-0x8(%rbp)      <- store at rbp-0x8
    117c:	xor    %eax,%eax
    117e:	mov    -0x34(%rbp),%eax
    1181:	mov    %eax,-0x20(%rbp)     <- arr[0]; note arr is at rbp-0x20 again
    ... (arr[1..3] and the summation loop, omitted)
    11c5:	mov    -0x28(%rbp),%eax
    11c8:	mov    -0x8(%rbp),%rdx      <- read the canary back
    11cc:	sub    %fs:0x28,%rdx        <- compare
    11d5:	je     11dc <with_arr(int)+0x78>
    11d7:	call   1040 <__stack_chk_fail@plt>
    11dc:	leave
    11dd:	ret
```

The three levels' behavior we read straight out of the assembly: strong plants a canary only in functions with array-like locals, and pure-scalar functions go without. -fstack-protector-all plants one even in a function like scalar_only, which a single lea could finish computing. And -fno-stack-protector plants nothing at all. Whether this machine's default level equals strong, we verify with an instruction-by-instruction diff rather than taking a book's word for it:

```text
$ g++ -O0 -g -fstack-protector-strong -o d5_explicit d5.cpp
$ objdump -d --no-show-raw-insn -C d5_strong > a.txt
$ objdump -d --no-show-raw-insn -C d5_explicit > b.txt
$ diff a.txt b.txt
2c2
< d5_strong:     file format elf64-x86-64
---
> d5_explicit:   file format elf64-x86-64
```

The only difference between the two objdump dumps is the filename line; the instruction streams are completely identical. So on this machine the default level is -fstack-protector-strong.

### Smash the Canary on Purpose and Watch the Interception

That settles the levels. Now let's trample the canary itself once. d5smash has the same shape as d3's default build: arr base at rbp-0x20, the canary at rbp-0x8, and the index that lands on it is 6:

```cpp
// d5smash.cpp — smash the canary on purpose and trigger __stack_chk_fail
#include <cstdio>

volatile int g_index   = 6;          // measured layout: arr base at rbp-0x20, canary at rbp-0x8, i.e. arr[6]
volatile int g_payload = 0x41414141;

__attribute__((noinline))
void f() {
    int arr[4] = {1, 2, 3, 4};
    arr[g_index] = g_payload; // out-of-bounds write, lands right on the canary
}

int main() {
    f();
    std::fprintf(stderr, "main: returned from f normally\n");
    return 0;
}
```

As usual we survey the layout with objdump. f's assembly is the same shape as d3's default build: the canary load, the out-of-bounds addressing in `mov %edx,-0x20(%rbp,%rax,4)`, and the final comparison are all present, so we skip pasting it. Let's run it:

```text
$ ./d5smash
*** stack smashing detected ***: terminated
exit=134
```

main's progress line never printed. The abort happened in f's epilogue, and control never made it back to main. gdb, meanwhile, recorded the whole chain from end to end:

```text
$ gdb -batch -q -ex 'set debuginfod enabled off' -ex run -ex bt \
      --args ./d5smash 2>&1 | grep -v 'libthread_db\|Thread debugging'
*** stack smashing detected ***: terminated

Program received signal SIGABRT, Aborted.
0x00007ffff789a17c in ?? () from /usr/lib/libc.so.6
#0  0x00007ffff789a17c in ?? () from /usr/lib/libc.so.6
#1  0x00007ffff783e5d0 in raise () from /usr/lib/libc.so.6
#2  0x00007ffff7825685 in abort () from /usr/lib/libc.so.6
#3  0x00007ffff7826704 in ?? () from /usr/lib/libc.so.6
#4  0x00007ffff792d030 in __fortify_fail () from /usr/lib/libc.so.6
#5  0x00007ffff792e414 in __stack_chk_fail () from /usr/lib/libc.so.6
#6  0x00005555555551a3 in f () at d5smash.cpp:11
#7  0x00005555555551ae in main () at d5smash.cpp:14
```

At this point we can fill in the timeline of the moment the canary got hit. arr[6] writes an int, 4 bytes, and it lands on the low 4 bytes of the canary slot, so the 8-byte sentinel gets half of itself overwritten. The comparison before f returns finds a mismatch and jumps into `__stack_chk_fail`, which goes by way of `__fortify_fail` to abort; the process receives a SIGABRT, and the return address stays intact the entire time. This whole defense does exactly one thing: compare the sentinel once before returning, and on a mismatch halt the process right there. This call never returns.

What the canary catches is the classic path: writes from the array toward higher addresses, heading for the return address. What it cannot catch we lay out just as plainly: in the first piece, d1's arr[-1] wrote toward lower addresses and hit a scalar in the same frame, the canary stayed intact from start to finish, the check kept passing, and the program exited normally. The sentinel only guards the post where it stands. Writes in the negative direction, and data neighbors within the same frame, are none of its business.

## Same Source Code, a Different Story Under -O2

Layout and defense are both settled now. Let's take the d1.cpp from the first piece and compile it again, unchanged, with -O2. The same `arr[g_index] = g_payload` in the same source now follows a different script:

```text
$ g++ -O2 -g -o d1_o2 d1.cpp
$ ./d1_o2
before: scalar=111 arr={1,2,3,4}
after : scalar=111 arr[g_index]=999
main: returned from f, program ends normally
exit=0
```

Put the two outputs side by side: at -O0 the after line reads scalar=999, at -O2 it reads scalar=111. The scalar never moved, and arr[g_index] still reads back 999 in both. Where did the change go? We hunt for it instruction by instruction in the -O2 disassembly of f (not one line deleted; the listing below is the whole function):

```asm
0000000000001180 <f()>:
    1180:	sub    $0x28,%rsp
    1184:	mov    $0x1,%edx
    1189:	mov    $0x2,%ecx
    118e:	mov    $0x4,%r9d
    1194:	mov    %fs:0x28,%rax
    119d:	mov    %rax,0x18(%rsp)
    11a2:	movabs $0x200000001,%rax
    11ac:	mov    $0x3,%r8d
    11b2:	mov    $0x6f,%esi
    11b7:	lea    0xe4a(%rip),%rdi        # 2008 <_IO_stdin_used+0x8>
    11be:	mov    %rax,(%rsp)
    11c2:	movabs $0x400000003,%rax
    11cc:	mov    %rax,0x8(%rsp)
    11d1:	xor    %eax,%eax
    11d3:	call   1030 <printf@plt>
    11d8:	mov    0x2e4a(%rip),%edx        # 4028 <g_payload>
    11de:	movslq 0x2e47(%rip),%rax        # 402c <g_index>
    11e5:	mov    %edx,(%rsp,%rax,4)
    11e8:	movslq 0x2e3d(%rip),%rax        # 402c <g_index>
    11ef:	mov    0x18(%rsp),%rdx
    11f4:	sub    %fs:0x28,%rdx
    11fd:	jne    1219 <f()+0x99>
    11ff:	mov    (%rsp,%rax,4),%edx
    1202:	mov    $0x6f,%esi
    1207:	xor    %eax,%eax
    1209:	add    $0x28,%rsp
    120d:	lea    0xe24(%rip),%rdi        # 2038 <_IO_stdin_used+0x30>
    1214:	jmp    1030 <printf@plt>
    1219:	call   1040 <__stack_chk_fail@plt>
```

Look at scalar first: its stack slot is gone. In both printf calls, scalar is 111 loaded on the spot by `mov $0x6f,%esi`. The scalar has moved into the instruction stream, and there is no slot left on the stack that can be corrupted. The location that got rewritten to 999 at -O0 has no owner in the -O2 world.

Next, the array: it moved to rsp+0x00. The two instructions `movabs $0x200000001,%rax` and `mov %rax,(%rsp)` pack 1 and 2 into 8 bytes and write them in one shot, and 3 and 4 get packed the same way into rsp+8. The array does have to keep a base object, because the runtime-indexed access `mov (%rsp,%rax,4)` needs it. But the position moved from rbp-0x20 to rsp+0x00, the frame shrank from 0x30 to 0x28, and the addressing base switched from rbp to rsp (in this version f does not even push rbp, so there is no saved rbp to speak of).

The same arr[-1] now writes to rsp-4, which falls into the red zone covered in 02, the 128-byte region below rsp reserved for the current function. Nothing else lives there. The written 999 gets touched exactly once, by the read-back `mov (%rsp,%rax,4),%edx` right after it, which prints it for us to see; after that nobody ever reads it again. The canary moved from rbp-0x8 to rsp+0x18, and the geometry of the protection has not changed: going up from the array's upper edge, idx 4 and 5 are 8 bytes of padding, idx 6 is the canary slot, above that another 8 bytes of padding, and beyond rsp+0x28 sits the return address. The ending turned into a tail call, `jmp printf`: f hands the whole wrap-up over to the callee and skips even its own ret.

Almost every one of the usual quiet-overflow mechanisms is present in this -O2 assembly, and we can match them one by one. One is the write into padding and alignment gaps; the 8 bytes at idx 4 and 5 are the evidence. Another is the compiler's variable reshuffling: the array moved and the scalar moved into the instruction stream. Yet another is a write to a value nobody reads again; the 999 in the red zone got read back exactly once. Both scripts of the same source are real: at -O0 the write corrupts a neighbor, at -O2 it strikes empty space, and which one you get is decided by the compiler's variable layout and its optimizations. That is where the landing sites came from. One more path to a no-crash outcome has nothing to do with landing sites at all, and that is where we go next.
