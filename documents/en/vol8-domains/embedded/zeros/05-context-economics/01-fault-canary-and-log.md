---
title: "Chip's Last Will: HardFault Scene, Stack Canary, and Zero-Allocated Logging"
description: "commit feef8fb fulfilled the pending work from the initial project setup by delivering a complete fault report. The naked HardFault_Handler uses tst and ite to determine whether the current stack is MSP or PSP, and the tail jump doesn't touch a single byte. The FaultReport includes CFSR, HFSR, BFAR, and the eight-byte hardware scene. The canary 0xEF114514 is buried at the bottom of the stack in spawn and idle. The patrol is embedded in context_switch, using PendSV's path for free. The zero-allocated formatter uses a single {} to let types define the grammar and overload as extension points. printdec is retired. I built a copy in /tmp, cross-compiled, and injected udf into Renode with x to see the complete will (cfsr=0x00010000 UNDEFINR with hfsr=0x40000000 FORCED), and pressed y to smash the canary and wait for the next switch to report eaten=2. All seven test cases and fifteen assertions in test_format on the host side passed."
chapter: 5
order: 1
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - 寄存器
  - cpp-modern
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "Faking a Birth Certificate at the Stack Top: PendSV, SVC, and the First Switch"
related:
  - "Interrupts Only Send Notes: Ring Buffer, Worker Threads, and Deferred Bottom Half"
  - "Wait 300 Milliseconds Without Raising a Task: Software Timers and fn+void* Hard Currency"
current_status:
  title: Will System On Duty
  detail: ZerOS's chapter is being polished
  variant: polishing
  icon: 🪦
translation:
  source: documents/vol8-domains/embedded/zeros/05-context-economics/01-fault-canary-and-log.md
  source_hash: fee65940acf7b74ceb9ec82374299244e24f3a0f46cf01bfed2461d139d0ca52
  translated_at: '2026-11-22T13:42:34+00:00'
  engine: qwen3:14b
  token_count: 18315
---
# Chip's Last Will: HardFault Scene, Stack Canary, and Zero-Allocated Logging

In the vector table of the article "Engineering Setup: From Empty Repository to Renode's First Output," the HardFault slot points to `fault_handler`, which prints a single line `[FAULT] halted` and halts. At that time, I wrote that the complete scene capture should be left to the HardFault module inside the kernel. Imagine the chip as someone who dies suddenly, and you'll realize that such a fallback is not even a death certificate. We don't know where the person died, which registers they were holding, or whose stack they were on. Firmware failures in the wild are so silent that the board just goes cold, and you're left with only a USB cable and an unreadable scene.

Commit `feef8fb` fulfilled the pending scene capture, with the subject being "feat: fault_package, logs ok." I counted its diff, and it changed 17 files, adding 429 lines and removing 45 lines. The theme of this site is **which context the code is running in**, and this article handles the fault context, i.e., after a crash, where the handling code runs, which stack it uses, and whether it can still speak. The way interrupts offload heavy work to workers is covered in 02 "Interrupts Only Send Notes" with ring buffer and workers. The way to wait 300 milliseconds without raising a task is covered in 03 "Wait 300 Milliseconds, No Task Raised" with software timers. They are sister articles under the same theme, and I won't go beyond that. In the main body of this article, we'll explain the will one by one: a capture port that doesn't pollute the scene, a canary buried at the bottom of the stack, and a formatter that can print numbers without touching the heap.

## Naked Capture Port: Not a Single Byte is Allowed to Move

What should a qualified will look like? We need to define it before showing the code. When the chip dies suddenly, we need to know who was running, where it died, and what the cause of death was. The old `fault_handler` only left a single line `[FAULT] halted` and sent the person away. The form of the will is thus determined: it should be a report that only contains data. The kernel is responsible for collecting the scene into the report, and the printing is handled by the board-level registration port. Both sides do their own jobs. The declaration of the will is in `include/ZerOS/arch/arm_cortex_m3/fault.hpp`, and the author's own comments set the metaphor. I'll give you the original text as is, the first paragraph is the main body of the will:

```cpp
#pragma once

// The last will and testament: when the chip dies, it dictates the scene
// before lying down. Declaration face only — bodies live in fault.cpp.

#include <cstdint>

namespace ZerOS::arch::cortex_m3 {

struct FaultReport {
    std::uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr; // the hardware-pushed frame
    std::uint32_t cfsr, hfsr, bfar;                  // SCB fault status & address
    const char* task_name;                           // who was running ("boot" if nobody)
    std::uint8_t task_prio;
    const std::uint32_t* frame;                      // stack top, for dumping
};
```

`FaultReport` is the main body of the will, reporting three sources of information: the eight-byte scene that the hardware has already pushed (r0 to xpsr, which we dealt with individually in "Faking a Birth Certificate at the Stack Top"), the values of three fault status registers in SCB, and the name and priority of the task that was running at that time.

Once the will is established, the next step is to register the receiving port. Let's look at the other half of the header file:

```cpp
using FaultSink = void (*)(const FaultReport&);
inline FaultSink fault_sink{nullptr}; // the board wires its printer here

} // namespace ZerOS::arch::cortex_m3
```

`FaultSink` is a bare function pointer. The kernel is only responsible for filling in the `FaultReport` and passing it out. Whether to print it or where to print it is up to the board-level to decide. If you think carefully, you'll see that this division of labor keeps the kernel silent.

I left the `—` on the declaration as is, because it's the original wording from the repository. The capture body is in `src/arch/arm_cortex_m3/fault.cpp`, and the opening assembly code is worth reading word by word:

```cpp
extern "C" [[gnu::naked]] void HardFault_Handler() {
    asm volatile("tst    lr, #4\n" // EXC_RETURN bit2: which stack was in use
                 "ite    eq\n"
                 "mrseq  r0, msp\n"
                 "mrsne  r0, psp\n"
                 "mov    r1, lr\n"
                 "b      hardfault_capture\n"); // tail jump — no push on the way
}
```

`[[gnu::naked]]` we've seen in "Faking a Birth Certificate at the Stack Top," the compiler doesn't generate a prologue or epilogue, and everything on the stack is managed by the assembly. The more stringent part here is that it doesn't even do the push action. The reason is hidden in `tst lr, #4`: when the exception comes in, the hardware puts the EXC_RETURN into `lr`, and the second bit indicates which stack was used at the time of the incident. 0 means the MSP (the stack used during interrupts and early boot), and 1 means the PSP (the stack used by the task). `ite eq` is the conditional block instruction of Cortex-M3, and the following `mrseq` and `mrsne`, one takes the MSP when the equality holds, and the other takes the PSP when the equality doesn't hold. After four instructions, the stack where the scene occurred is determined. The result is put into r0, and `lr` is moved into r1 as evidence. Finally, the `b` is a tail jump, landing into a regular C++ function. The comment "no push on the way" is the bottom line of the entire section: the tail jump doesn't go through any push, and every byte of the scene remains exactly as it was when the chip fell.

Why do we care so much about this? You see, if the handler is a regular function, the compiler will push a bunch of register archives onto the current stack, and the stack being used at this moment may be the stack of the deceased. Every byte pushed down is stepping on the inheritance. The combination of `naked` and tail jump makes the capture code stand outside the scene. We are collecting evidence on the ruins of others, and we don't throw anything into the ruins ourselves.

The C++ function that takes over fills in the will. Let's look at the first part of it:

```cpp
extern "C" void hardfault_capture(std::uint32_t* frame,
                                  std::uint32_t exc_return [[maybe_unused]]) noexcept {
    using ZerOS::arch::cortex_m3::FaultReport;
    using ZerOS::sched::zeros_impl::TCBKeys;

    const char* name = "boot";
    std::uint8_t prio = 0xFF;
    if (auto cur = ZerOS::system::os().current_task()) { // boot crash: nobody runs
        name = TCBKeys::name(*cur);
        prio = TCBKeys::prio(*cur);
    }
```

The time of death may fall into two worlds, and we'll look at them separately. If the task was running when it crashed, `current_task()` can get its owner, and the name and priority are queried from the TCB. If it crashed before the scheduler was even started, `current_task()` returns empty, and the name falls back to the default `"boot"`, and the comment says "nobody runs" referring to it. This line covers the way of death in the early milliseconds of startup. As you continue reading, the function reads the three fault registers into the report, and the addresses are hard-coded: CFSR is at 0xE000ED28, HFSR is at 0xE000ED2C, and BFAR is at 0xE000ED38. After passing the report to `fault_sink`, the function falls into a `wfi` dead loop. The original comment says `testament delivered; lie down`, meaning that the will has been delivered and it lies down on the spot.

## How to Read the Will: Bitfields of CFSR, HFSR, BFAR

The three registers passed down are a series of hexadecimal numbers. Whether they are valuable or not depends on whether we can read them. According to the "ARMv7-M Architecture Reference Manual," we'll record the口径 of ARMv7-M: CFSR (Configurable Fault Status Register) is a 32-bit tripartite, with the low byte belonging to MemManage, the second byte to BusFault, and the high half-byte to UsageFault. In HFSR (HardFault Status Register), we are most concerned about bit 30, FORCED, which is set to 1 meaning that the original light-weight fault was not caught and was forcibly upgraded to a HardFault by the hardware. BFAR is the address register for BusFault. However, it has a switch, and only when the BFARVALID (bit 15) in CFSR is set, it has meaning.

An empty bitfield table is not as good as a real will. Let's go to the later verification section and match it with the `cfsr=0x00010000 hfsr=0x40000000` scene injected by Renode. You'll see that the two bits UNDEFINR and FORCED match the manual one by one.

::: warning
BFAR outputting 0x00000000 does not mean that the bad address of the bus access is zero. It only means that BFARVALID is not set, and there is no valid value in the register. When reading the fault report, each address register must be checked with its VALID bit. A single number cannot be used as evidence. The bitfield table I checked is based on the口径 of the ARMv7-M manual, and the specific bits for UNDEFINR and FORCED have been verified with real outputs in the verification section. For other bits, refer to the manual when using them.
:::

## Stack Canary: Free Patrol on the Switch Path

Stack overflow is one of the most common ways to die in the bare-metal world, and the death scene is particularly confusing: the corrupted data is often someone else's, and the crash site is several tasks away from the crime scene. Investigation uses the old mining technique, where miners go down the mine with a canary bird. If the bird faints, the miners retreat. Let's look at ZerOS, which buries the bird in the stack view of the TCB. The code is in `include/ZerOS/kernel/sched/stack_guard.hpp`:

```cpp
namespace ZerOS::task {
constexpr inline std::uint32_t kCanaryWord = 0xEF114514;
constexpr inline std::size_t kCanarySize = 8;

struct TaskGuardHelper {
    static void bury_canary(sched::TCB& t) {
        auto& stack_view = sched::zeros_impl::TCBKeys::stack_view(t);
        // word-wise: memset fills BYTES, it would chop the pattern to 0x14
        for (std::size_t i = 0; i < kCanarySize && i < stack_view.size(); ++i) {
            stack_view[i] = kCanaryWord;
        }
    }
```

Let's look at the location and geometry of the burial. Eight sentinels (each is `0xEF114514`) occupy the lowest 32 bytes of the stack view, which is the last line of defense of the entire stack. The task's stack grows from high address to low address, and the first bite of overflow is the highest `view[7]` in the sentinel. After it is eaten, the next bite is `view[6]`, `view[5]`, and so on, heading toward the lowest address. The burial method is written word by word, and the comment leaves a real misjudgment: `memset` fills the bytes, and three of the four bytes in `0xEF114514` are 0x14, resulting in a single stripe, and the eight-character pattern is lost, making the investigation unrecognizable.

Let's look at the inspection, which is divided into two levels, with the cheaper one first:

```cpp
    static bool fast_check_stack(sched::TCB& t) {
        // the deepest sentinel is the first thing an overflow eats
        return sched::zeros_impl::TCBKeys::stack_view(t)[kCanarySize - 1] != kCanaryWord;
    }

    static std::size_t eaten_length(sched::TCB& t) {
        auto& stack_view = sched::zeros_impl::TCBKeys::stack_view(t);
        std::size_t length = 0;
        for (std::size_t i = 0; i < kCanarySize; ++i) {
            if (stack_view[i] != kCanaryWord) {
                length++;
            }
        }
        return length;
    }
```

`fast_check_stack` only reads the single word at the edge of `view[7]`, and if it's not equal, it passes. Each patrol tax on the switch is a comparison and a memory access. When it finds something wrong, `eaten_length` counts the eight words one by one, and the number of eaten birds is reported. I like its cheap shape, the cost of investigation is always zero, and the cost of damage is only paid once after the incident.

The timing of the burial is at birth. Let's look at:

```cpp
void spawn(ZerOS::sched::TCB& t) {
    task::TaskGuardHelper::bury_canary(t);
    fabricate_frame(t);
    system_sched.add(&t);
}
```

`spawn` buries the canary, forges the frame, and then enqueues it. The idle task also goes through the same `bury_canary` in `start_scheduler`, and no one's stack is left out. Where is the real patrol? It's in the `context_switch` we read in "Faking a Birth Certificate at the Stack Top," and `feef8fb` added a section to it:

```cpp
extern "C" std::uint32_t* context_switch(std::uint32_t* saved_sp) {
    using TaskGuardHelper = task::TaskGuardHelper;
    if (saved_sp != nullptr) {
        auto prev = system_sched.current_task();
        TCBKeys::sp(*prev) = saved_sp;
        if (TaskGuardHelper::fast_check_stack(*prev) &&
            TaskGuardHelper::overflow_reporter != nullptr) {
            TaskGuardHelper::overflow_reporter(prev, TaskGuardHelper::eaten_length(*prev));
        }
    }
    return TCBKeys::sp(*system_sched.pick_next());
}
```

The patrol's logic is clear from `saved_sp`: a non-empty `saved_sp` means that a real task has been switched off the CPU, and PendSV has saved its scene before coming here. At this moment, it's the best time to check its stack, which is now complete and fixed. The first switch is `svc 0`, and `saved_sp` is empty, meaning no task has been switched off the CPU, and the check is naturally skipped. Each task switch must go through this function, and the canary patrol is not missed, without opening an additional interrupt or starting a new task, not occupying any runtime resources, only paying the cost of one comparison per trip.

The destination after detecting a problem is also a continuation of the old method: `overflow_reporter` is another bare function pointer, and the kernel only reports the number, and the board makes the decision. In the demo, the wiring prints a line `[STACK OVERFLOW] task=c eaten=2` and then halts. The real output we see in the Renode section.

## Zero-Allocated Formatter: Types as Grammar, Overloads as Extension Points

For the will to be printed, we need a way to convert hexadecimal and decimal into characters. The early `printdec` in the first few chapters handled the decimal part, manually writing a reverse remainder loop, and the ticker in "The Definition of a Second" has been using it all along. It only has the ability of decimal, but the will needs a line like `pc=0x08000460` with formatting, and `printdec` can't handle it. `feef8fb` has retired it, and replaced it with `include/ZerOS/log/format.hpp`.

The two paths that were originally in front of us are both not feasible. Following the old path of `printdec`, for each format beyond decimal, we would have to manually write another function for counting, and the kernel code would have to be reprocessed every time the will format changes. There is also a path to set up a format string grammar, like the `%08x` pattern that `printf` recognizes, but the grammar can't be turned into a parser. `feef8fb` didn't take either of these two paths, and each value enters with its own type, and how to print is decided by the type itself.

Let's look at the header file in two parts, the first part is the facade and position:

```cpp
#pragma once

// The whole "upper-layer formatter" the architecture allows (kernel stays
// formatting-free). One placeholder "{}" — the TYPE says how to print, so
// there is no format-string grammar and no parser:
//
//   format_to(buf, "task={} pc=0x{}\r\n", "H", Hex{0x08000F42u});
//
#include <cstddef>
#include <span>

#include "ZerOS/log/format_impl.hpp"

namespace ZerOS::log {
```

The bottom line of the design is written in the comments, and we'll look at the code. You see, the placeholder is only `{}`. The grammar is decided by the type: passing `Hex` prints the eight-digit uppercase hexadecimal, passing `Dec` prints the decimal, and there is no `%08x` pattern in the format string, so no parser is needed.

Once the facade is established, let's look at the working template body, and the no-alloc declaration is right on top of it:

```cpp
// No allocation, no state, no locks → safe from tasks, ISRs and fault
// handlers. Returns the number of chars written (truncates at out.size()).
template <typename... Args>
std::size_t format_to(std::span<char> out, const char* fmt, Args... args) {
    char* cursor = out.data();
    char* limit = out.data() + out.size();
    [[maybe_unused]] auto print_arg = [&](auto&& arg) {
        if (format_impl::emit_plain(fmt, cursor, limit)) { // a placeholder for it?
            format_impl::emit_value(cursor, limit, arg);   // a plain call, one body
        }
    };
    (print_arg(args), ...);
    while (*fmt != '\0') { // trailing plain text
        format_impl::emit(cursor, limit, *fmt++);
    }
    return static_cast<std::size_t>(cursor - out.data());
}

} // namespace ZerOS::log
```

It is no-alloc, no-state, and no-lock, so we can use it in tasks, interrupts, and fault handling. The will must be printed in places where we can't touch the heap, and no-alloc is the qualification for it, but it's not a virtue. When the buffer is full, it truncates, and there are also `cursor` and `limit`, and out-of-bound content can't be written.

`(print_arg(args), ...)` is a C++17 fold expression, and the parameter package is expanded in order. Each actual argument asks whether there is still a placeholder waiting for it in the format string. If there is, it hands its value to `emit_value` (the comment in the code says "a plain call, one body" which we'll use shortly). Extra actual arguments are quietly skipped, and extra placeholders are left as is in the output, and both directions of mismatch don't explode.

The shape of the extension point is clearly written in the comments of `include/ZerOS/log/format_impl.hpp`. If a new type wants to get on the printing machine, it adds a value-wrapping class in this file (like `Hex`, `Dec`), and adds an overload of `emit_value`, and `format.hpp` doesn't need to change a single line. The overload resolution replaces us to assign the type to the correct printing function, and the landing point of "type as grammar" is here. Let's look at the two ready-made printing bodies:

```cpp
void emit_value(char*& cursor, char* limit, Hex h) {
    constexpr char kDigits[] = "0123456789ABCDEF";
    for (int shift = 28; shift >= 0; shift -= 4) { // fixed 8 digits, zero padded
        emit(cursor, limit, kDigits[(h.value >> shift) & 0xFu]);
    }
}

void emit_value(char*& cursor, char* limit, Dec d) {
    char tmp[10]; // 2^32 needs at most 10 digits
    int n = 0;
    do {
        tmp[n++] = static_cast<char>('0' + d.value % 10);
        d.value /= 10;
    } while (d.value != 0);
    while (n > 0) {
        emit(cursor, limit, tmp[--n]);
    }
}
```

Let's look at `Hex`, which prints eight digits fixed, with zeros filled in, and uppercase output. The `0x08000460` in the will is its handiwork. `Dec` is still the old technique of `printdec`, the old way of reverse remainder and writing in reverse, but it has changed positions: from the board-level's manually written function to an overload of the formatter, and decimal is now also on the type side, no longer a special case.

There is also a clever idea hidden in the file header, the original comment says `ONE copy per binary, no matter how many translation units call format_to`, meaning that the entire binary has only one body, no matter how many translation units call `format_to`. The expansion of the template would normally inline the loop into each call point, resulting in dozens of identical loops in the binary. The repository's approach is a thin template shell plus a regular function body, with the body only in `src/log/format_impl.cpp`, and `feef8fb` has put it into the newly established `zeros_common` static library. Both the board package and the host test are linked from the library. Code size in our project is a hard constraint with budget number D12, and one body is one flash cost.

Let's look at the board side, `uart.hpp`'s `printdec` is gone, and the new one is:

```cpp
// one-shot formatted line: fixed buffer, polled out — task/ISR/fault safe
template <typename... Args>
void format(const char* f, Args... args) {
    char buf[160];
    const auto n = ZerOS::log::format_to(buf, f, args...);
    for (std::size_t i = 0; i < n; ++i) {
        uart1_putc(buf[i]);
    }
}
```

You see its one-time use, a 160-byte stack buffer, and the formatted characters are sent out one by one after formatting. It's usable in three places: tasks, ISR, and fault. Polling sending was discussed in the "Engineering Setup" article, it's slow, but the will doesn't need to hurry, and slow and stable is just right.

::: warning
I've left one seam unpatched: the `global_channel.hpp` in the article "The Definition of a Second" has only a declaration and no implementation up to commit `feef8fb`, and the two lines of assertion logs that were commented out are still hanging. `feef8fb` has delivered the formatter and the fault report as two foundations, and the facade of the global logging channel is still empty. If you want to connect it, you'll have to wait for the later work.
:::

## Host Exam: Seven Test Cases on Grammar

The formatter is pure logic, and it's perfectly suitable to test it on the host. The exam paper is the well-written `test/test_format.cpp`, and it has seven `TEST_CASE`s. Let's pick three to look at, each guarding one boundary of the grammar. The first one handles the hexadecimal format:

```cpp
TEST_CASE("hex is 8 digits zero padded, uppercase", "[format]") {
    CHECK(run("{}", Hex{0u}) == "00000000");
    CHECK(run("{}", Hex{0xDEADBEEFu}) == "DEADBEEF");
    CHECK(run("{}", Hex{0x0000000Fu}) == "0000000F");
}
```

We've checked the three shapes of zero, full value, and zero-padding in the low bits. `0x0000000F` is printed as `0000000F`, and the format is thus determined. The second one handles the two directions of mismatch:

```cpp
TEST_CASE("more args than placeholders: extras are ignored", "[format]") {
    CHECK(run("only {}", "used", "dropped") == "only used");
}

TEST_CASE("more placeholders than args: leftover {} stays visible", "[format]") {
    CHECK(run("got {} want {} - only one", "a") == "got a want {} - only one");
}
```

Extra actual arguments are skipped, and extra placeholders are left as is. I think the second one was chosen beautifully, a missing `{}` in the log is a clear sign, more honest than silent swallowing, and you can tell at a glance which value didn't connect. The third one handles truncation:

```cpp
TEST_CASE("buffer full: truncates, never overruns", "[format]") {
    char small[4];
    const auto n = format_to(small, "abcdefgh");
    CHECK(n == 4);
    CHECK(std::memcmp(small, "abcd", 4) == 0);
}
```

Let's look at the truncation test case, four bytes of buffer filled with eight letters, and the output is exactly the first four, and the return value also truthfully reports four. This test case is a direct exam of the safety of the fault scene, and the `limit` wall really blocked it. The remaining test cases handle pure text transmission, the interleaving order of placeholders and text, and the zero and full values of decimal, I won't go through them one by one, you'll see them all by running it once.

## Renode Acceptance: Press x to Write the Will, Press y to Smash the Canary

I reproduce the chain as usual: using `git archive feef8fb` to export the tree to a copy in /tmp, CMSIS is copied in from the submodule, and the two cross-compilation commands are the same as in the previous article:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The size is real, the three will items plus the demo are all in, let's see:

```text
   FLASH:        4812 B        64 KB      7.34%
     RAM:        2448 B        20 KB     11.95%
   text	   data	    bss	    dec	    hex	filename
   4620	     192	   2256	   7068	   1b9c	.../zeros-bluepill.elf
```

Flash has grown to 4812 bytes, about 1.2KB remains to the D12 6KB budget line, and RAM's 2256 bytes bss has added the canary and a larger task stack. The budget is still within reach, but the space is visibly shrinking, I report the numbers as is.

The `main` in the demo has added two wiring, and the comment in one sentence explains the division of labor: `the two testaments: kernel catches, board decides (print + halt)` (two wills are caught by the kernel, decided by the board, decided, then printed and halted). Task c provides us with the key experiment, pressing `x` executes a `udf #0` (undefined instruction, CPU-level fault, the simulator won't be lenient), pressing `y` smashes its own canary. The action of smashing is described in a long comment in the source code, I'll give it to you as is:

```cpp
// 'y' smashes task C's own canary — an honest simulation of what a real
// overflow leaves behind. (Actually burning the stack with recursion is a
// fight against the optimizer: GCC turned three "unfolds" in a row into
// loops. The mechanism under test — eaten sentinel → PendSV patrol →
// report — doesn't care who ate the words.)
```

It's honest cheating: if you want to really burn through the stack with recursion, you have to fight with the optimizer. GCC folds three manually written expansions back into a loop, so the demo directly kills the two deepest birds, and the tested mechanism doesn't care who the culprit is.

I'll explain the way to inject the key, because the serial port analysis window in Renode can't type keys in headless mode. I followed the upstream demonstration script of Renode (scripts/complex/hci_uart/hci_uart.resc, installed on /opt/renode locally), attached a socket terminal to usart1, and used a few lines of Python to send bytes from the TCP side. The Renode-side command exits after start, and the injection must be completed within its ten-second window. I sent the bytes when the simulation ran to 8 seconds:

```shell
renode --console --disable-xwt \
  -e "emulation CreateServerSocketTerminal 34568 \"term\"" \
  -e '$bin=@build/src/board/stm32f103_bluepill/zeros-bluepill.elf' \
  -e "include @src/board/stm32f103_bluepill/sim/renode/bluepill.resc" \
  -e "connector Connect sysbus.usart1 term" \
  -e "start" -e "sleep 10" -e "quit" &
```

```python
import socket
s = socket.create_connection(("127.0.0.1", 34568))
s.sendall(b"x")  # 按 y 的实验就把 b"x" 换成 b"y"
s.close()
```

Let's look at the will when pressing `x`, the real output in the serial window is as follows:

```text
[INFO] usart1: [host: 5.59s (+51.49ms)|virt:   8.04s (+42.58ms)] c: executing an undefined instruction, goodbye
[INFO] usart1: [host:  5.59s (+0.16ms)|virt:   8.04s (+11.17µs)] *** HARD FAULT *** task=c prio=0
[INFO] usart1: [host:  5.59s (+0.42ms)|virt:   8.04s (+0s)] pc=0x08000460 lr=0x08000175 xpsr=0x81000000
[INFO] usart1: [host:  5.59s (+0.11ms)|virt:   8.04s (+0s)] cfsr=0x00010000 hfsr=0x40000000 bfar=0x00000000
[INFO] usart1: [host:  5.59s (+0.17ms)|virt:   8.04s (+0s)] stack: 00000000 20000740 C0000000 40013800 00000000 08000175 08000460 81000000
```

Let's read out the information together. The one who died was task c with priority 0, the name and priority were queried from the TCB, indicating that the scheduler has already started, and the death occurred on the task's PSP. `pc=0x08000460` is the crime scene, `udf` is lying at the crime scene address, you can disassemble it at a glance. The two status registers match the manual: `cfsr=0x00010000`, the lower two bytes are all zero, and the upper half is 0x0001, exactly on bit 16 of the UsageFault half, UNDEFINR (undefined instruction) is set. `hfsr=0x40000000` matches bit 30, FORCED is set, the UsageFault was not caught independently, and the hardware forcibly upgraded it to a HardFault, the two values match the bitfields of ARMv7-M one by one. BFAR's 0x00000000 is read according to the warning above: BFARVALID is not set, the printed zero is not valid. The last line of eight bytes is the original scene pressed by the hardware, the second-to-last word in `stack:` is 0x08000460, and it mutually verifies with `pc=`, the two copies of the same scene match. The startup code of the demo points the vector table slots 3 to 6 to `HardFault_Handler`, the simulator comes from any slot, and the same person takes over, the insurance of the origin is written in the startup comment.

The report when pressing `y` is much shorter, let's look at:

```text
[INFO] usart1: [host: 8.28s (+74.73ms)|virt:   8.17s (+68.99ms)] c: smashing my canary
[INFO] usart1: [host:   8.28s (+0.4ms)|virt:   8.17s (+11.19µs)] [STACK OVERFLOW] task=c eaten=2
```

`smash_canary` killed the two deepest birds (`view[7]` and `view[6]`), task c slept for two milliseconds, and PendSV switched it off at that moment, the patrol discovered it, and `eaten_length` counted two words, the number in the report matches the number of the crime perfectly. Note the timeline: after smashing, the chip didn't die immediately, it lived for two more milliseconds, and the cause of death was discovered during the next switch. This is the difference between the patrol on the switch path and the fault interrupt, the overflow is not an exception, it doesn't trigger any vector, and only appears when someone else checks.

I also paste the `ctest` on the host side as is, this test suite comes with ASan and UBSan reinforcement options, all ten targets passed:

```text
100% tests passed out of 10

Total Test time (real) =   0.18 sec
```

The formatter's own scorecard, let's take a look:

```text
All tests passed (15 assertions in 7 test cases)
```

The numbers of seven test cases and fifteen assertions, and these are the results run with my local GCC 16.2.1.

## Acceptance

The verifiable facts of this article are summarized into four items, and we'll go through them one by one.

The first item is the host's exam. You checkout to `feef8fb`, configure and build with `cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug`, and `ctest` should give the result that all ten targets passed, and the scorecard of `test_format` should be seven test cases and fifteen assertions.

The second item is the complete will of `x`. After cross-compiling, run Renode, inject `x` into usart1, and you should see the first line of the will `task=c prio=0`, as well as `cfsr=0x00010000` UNDEFINR, `hfsr=0x40000000` FORCED, and the eight-byte scene. The address pointed by `pc` has `udf`, and `arm-none-eabi-objdump -d` disassembles it at a glance.

The third item is the overflow report of `y`. After pressing `y`, you should see `[STACK OVERFLOW] task=c eaten=2`, the two words eaten correspond to the two deepest birds killed by the demo. You can also modify `smash_canary` yourself, kill one more bird, and `eaten` will increase accordingly, the mechanism is robust enough to withstand your code changes.

The fourth item is the confrontation between bitfields and the manual, which is also the part I honestly admit I haven't verified completely. I have verified the two bits UNDEFINR (CFSR bit 16) and FORCED (HFSR bit 30) with real outputs, and they match the bitfields of the ARMv7-M manual. The other bits of CFSR (such as PRECISERR, IMPRECISERR in the BusFault byte) and the behavior of BFARVALID, this article hasn't constructed the corresponding faults to trigger, I have only checked the manual's wording, not the experimental endorsement, when you use them, refer to the manual, don't take my table as the final judgment.

## Next Article

The will of the fault context has been written, the cause of death, the scene, and the overflow have all been accounted for. The same theme still has two tasks left: the way interrupts offload the work is covered in "Interrupts Only Send Notes," with the anchor point at `3570f27`. The way to wait 300 milliseconds without raising a task is covered in "Wait 300 Milliseconds, No Task Raised" with the timer, with the anchor point at `766c462`. They are independent of this article, and when we combine the three articles, we have the complete answer to this chapter.
