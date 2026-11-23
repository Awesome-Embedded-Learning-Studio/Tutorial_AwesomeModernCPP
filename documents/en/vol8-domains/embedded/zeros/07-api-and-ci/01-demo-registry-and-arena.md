---
title: "One Scene, One Room: Demo Registration Center, Three Paths for Task Creation, and Testament Becomes an Obligation"
description: "We split the 342-line monolithic example into a registration center with one scene per subdirectory, zeros_add_demo generates firmware and Renode targets with a single line of registration, task creation converges into three paths: hand-laid slot, arena slicing, and builder chains, weak and strong interrupt handlers hand the choice over to the linker, and the testament report becomes an obligation that must be connected. I ran all eight demos in the /tmp copy, and all 14 host-side tests passed, and both death methods in testaments left their own testaments."
chapter: 7
order: 1
tags:
  - stm32f1
  - intermediate
  - embedded
  - CMake
  - builder pattern
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 24
prerequisites:
  - "Faking a Birth Certificate on the Stack: PendSV, SVC, and the First Context Switch"
  - "Leaving a Will Before the Chip Dies: HardFault Scene, Stack Canary, and Zero Allocation Log"
  - "Interrupts Only Send Notes: Ring Buffer, Worker Thread, and Bottom Half Delay"
related:
  - "Count to Ten and Switch: Equal Time Slices and Non-Inherited Slice Debt"
  - "Run for Five Seconds, Say Your Line: CI Smoke Testing, Include Firewall, and Size Gate"
current_status:
  title: Eight Rooms Divided
  detail: The ZerOS chapter is currently being polished
  variant: polishing
  icon: 🏠
translation:
  source: documents/vol8-domains/embedded/zeros/07-api-and-ci/01-demo-registry-and-arena.md
  source_hash: 9b7b16e3a24dffa2dd3b3046f8a7ecbf105f73811e1404d4a029dd19ebc4ce8f
  translated_at: '2026-11-22T13:56:43+00:00'
  engine: qwen3:14b
  token_count: 21794
---
# One Scene, One Room: Demo Registration Center, Three Paths for Task Creation, and Testament Becomes an Obligation

Following the previous chapters, we've always seen the same face on the Renode serial window: the L, M, H of the reversed theater, the byte-queue and worker, and the A and B that type letters, all living in the same 342-line `example/main.cpp`. If you want to test priority inheritance alone, you have to include the bottom half and heartbeat in the firmware, because the 342-line house plays all the roles, and changing the lines means eight unrelated characters have to be re-introduced.

`0054cf8` tore down this house. Its subject is "feat: separate the examples and find more cases" (seperate is the original spelling in the repository, the author missed an 'i', we keep it as is), and it submitted 37 files, adding 1051 lines and removing 472 lines. The old `example/main.cpp` was entirely deleted in the diff, replaced by eight subdirectories under the example directory, one for each scene. This same change also redefined three APIs: task creation changed from one path to three, logging and hardware were separated, and the death report became an obligation that must be connected, otherwise it would not pass. We'll go through these changes in order, and you'll be able to run all eight demos with a single command at the end.

## The Old 342-Line House: How Crowded It Was

The deleted old `main.cpp` starts with a row of global declarations, all of which support the tasks. Let's take a look:

```cpp
alignas(8) constinit std::uint32_t stack_a[64] = {};
alignas(8) constinit std::uint32_t stack_b[64] = {};
alignas(8) constinit std::uint32_t stack_c[96] = {}; // canary 32B + ~350B usable: burns in 3-4 levels
alignas(8) constinit std::uint32_t stack_holder[96] = {};
alignas(8) constinit std::uint32_t stack_urgent[64] = {};
alignas(8) constinit std::uint32_t stack_bully[64] = {};
alignas(8) constinit std::uint32_t idle_stack[32] = {};

// zero storage, meaning comes only from the factory chain
constinit ZerOS::sched::TCBStorage tcb_a{};
constinit ZerOS::sched::TCBStorage tcb_b{};
constinit ZerOS::sched::TCBStorage tcb_c{};
```

The array is the bed for the task, `TCBStorage` is its box, and every time we add a task, we have to declare the bed in one place, the box in another, and connect the chain to them. At the end of the file, the main function adds a, b, c, which type letters, and L, H, M, which perform the reversed theater, and the worker as well, six declarations in a row, and `start_scheduler(idle_stack)` requires you to hand the idle bed in yourself. Each line is correct on its own, but together they form a dead end for teaching: when readers open the example to see one concept, they are met with all concepts, and the noise buries the signal.

## Registration Center: One Line of Registration, Firmware and Simulation Target Generated Together

The foundation of the new structure is in `src/board/stm32f103_bluepill/example/CMakeLists.txt`. Let's look at the file header, where the principle is written in the comments:

```cmake
# Example Registrytion — the demo sample plate.
#
# One scenario, one subdirectory. Each subdirectory registers itself with
# the one-liner:  zeros_add_demo(<name>)
```

(Registrytion is another spelling that we keep as is, the file header has a typo, we don't change it for the author.) The principle is implemented as a CMake function. Before reading the code, we establish what the function does: for a demo house to run, a set of fixed overheads must be behind it, the executable target must compile this scene's main.cpp with the common kernel source, the linker must be fed the board's linking script, use gc-sections to cut unused sections, leave a map file for symbol lookup, report the size after compilation, and if you want to run it locally again, you need a simulation target that feeds the ELF to Renode. Without this function, this entire set of tasks would have to be written eight times in each subdirectory's CMakeLists, and if the linker options change, the eight CMakeLists would have to be updated one by one. `zeros_add_demo` collects this set into a function, and we'll look at it in three parts:

```cmake
function(zeros_add_demo name)
    add_executable(zeros-demo-${name} ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
                                         ${ZEROS_DEMO_COMMON_SOURCES})
    target_link_libraries(zeros-demo-${name} PRIVATE zeros_common)
    target_link_options(
        zeros-demo-${name}
        PRIVATE -nostdlib
        -Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/../../link.ld
        -Wl,--gc-sections
        -Wl,--print-memory-usage
        -Wl,-Map,${CMAKE_BINARY_DIR}/demo-${name}.map
    )
```

The firmware target is formed in this section: `zeros-demo-<name>` compiles this scene's own main.cpp and the `${ZEROS_DEMO_COMMON_SOURCES}` common kernel source together, and connects to `zeros_common`. The linking options are all set at once: the standard library is not included, the linking script uses the `link.ld` from the upper directory, gc-sections cuts the unused sections, the memory usage is printed on the spot after compilation, and the map file is left in the build directory for reference.

```cmake
    add_custom_command(
        TARGET zeros-demo-${name}
        POST_BUILD
        COMMAND arm-none-eabi-size $<TARGET_FILE:zeros-demo-${name}>
        COMMENT "size report: demo-${name}"
    )
```

At the end of the compilation, `arm-none-eabi-size` reports the size on the spot. The eight demo sizes we saw in the verification section are all generated by it.

```cmake
    add_custom_target(
        run-${name}
        COMMAND ${CMAKE_COMMAND}
        -DRENODE_ROOT=${CMAKE_SOURCE_DIR}
        -DRENODE_ELF=$<TARGET_FILE:zeros-demo-${name}>
        -DRENODE_RESC=src/board/stm32f103_bluepill/sim/renode/bluepill.resc
        -DRENODE_SECONDS=${ZEROS_SIM_SECONDS}
        -P ${CMAKE_CURRENT_LIST_DIR}/../../sim/renode/run-renode.cmake
        DEPENDS zeros-demo-${name}
        USES_TERMINAL
    )
endfunction()
```

The last member is the simulation target `run-<name>`: it passes the firmware and simulation script paths to `run-renode.cmake`, which feeds the ELF to Renode, and `DEPENDS` ensures that the firmware is compiled first. The simulation only exits after `ZEROS_SIM_SECONDS` seconds, the default value is 5 seconds, a configurable cached variable, you can change it if you want to run it longer.

A few lines of include paths have been omitted, the original file adds the kernel headers, board headers, and CMSIS search paths for each demo, we won't expand on it today. The combined effect of the three parts is that you register a name, and the firmware and simulation target are both in place. Therefore, the `CMakeLists.txt` in each demo subdirectory only has one line, the full text of heartbeat is:

```cmake
zeros_add_demo(heartbeat)
```

The registration center finally lists all eight subdirectories in order, we'll read them one by one:

```cmake
add_subdirectory(heartbeat)
add_subdirectory(event_stream)
add_subdirectory(inversion)
add_subdirectory(bottom_half)
add_subdirectory(timers)
add_subdirectory(events_notify)
add_subdirectory(testaments)
add_subdirectory(round_robin)
```

One scene, one room is what it says: each subdirectory is a scene, `main.cpp` runs from start to finish, you can open the inversion firmware to see the inversion, open the testaments firmware to see the testaments, and they don't interfere with each other.

Each room also shares a common opening ceremony, which lives in `example/demo_setup.hpp`, we'll look at it:

```cpp
inline void setup(const char* name) {
    board::uart1_init();
    board::register_uart_channel(); // this board talks over UART1
    log::print("\r\nZerOS demo: {}\r\n", name);
    arch::cortex_m3::install_default_testaments(); // obligations, explicitly
    arch::cortex_m3::init_time(72'000'000 / 1000); // 1kHz tick
}
```

Its positioning is the "Three lines of boilerplate, one call" in the file header comment: the board is up, the log channel is registered, the testament is installed, and a 1kHz tick is set, four things are collected into a `demo::setup("name")`. Among them, the line of testament installation is the main character of the "Testament Becomes an Obligation" section later, here you just need to be aware of its existence.

## Three Paths for Task Creation

The demos have been separated, and the old method of creating tasks should be upgraded accordingly, because the method of writing bed, box, and chain separately is exactly the soil that made the 342-line code expand. After `0054cf8`, there are now three paths for creating tasks, they all lead to the same builder chain (it was born in the scheduler period, we disassembled its type state design in the drop-wakeup chapter), the difference lies only in the source of memory.

The old path is hand-laid storage: you declare an array of `TaskStack` type and a `TCBStorage` box yourself, the chain is `named().prio().stack(span).entry(fn, arg).spawn_into(box)`. We haven't abandoned the old path, the worker in the bottom half demo is still using it today, because the user_worker's embedded stack must be passed on to someone. The other two paths are new ones laid out by `0054cf8`, we'll look at them one by one.

### Slot Path: Box and Bed Merged into One Type

The second path solves the problem of bed and box being separated. The bed still carries an old illness, we'll hand it over to the type in front: a bare `uint32_t` array only guarantees 4-byte alignment, but AAPCS (ARM's procedure call standard) requires the stack pointer at the public interface to be 8-byte aligned. A task starting on a skewed stack top will die in an inexplicable way. The new type in `include/ZerOS/kernel/sched/stack.hpp` solves this problem, turning the alignment from something you have to remember into something the compiler ensures for you, we'll look at it:

```cpp
template <std::size_t Words>
struct TaskStack {
    static_assert(Words % 2 == 0, "even word count keeps the top 8-aligned");

    alignas(8) std::uint32_t words_[Words]{};

    constexpr operator std::span<std::uint32_t>() noexcept { return words_; }
};

static_assert(alignof(TaskStack<8>) == 8);
```

The solution is clear: by taking an even number of words, the stack top is always 8-byte aligned, and the compiler will stop you on the spot if you write an odd number of words, the alignment is now guaranteed by the type instead of being something you have to remember.

Next, we look at `task.hpp`, which merges the box and bed again:

```cpp
template <std::size_t Words = 64>
struct TaskSlot {
    sched::TCBStorage tcb{};
    TaskStack<Words> stack{};
};
```

You declare a `TaskSlot`, and the storage for the task is complete, the chain accordingly adds an extra stage:

```cpp
    // the everyday road: hand in a TaskSlot, box and bed travel together
    template <std::size_t Words>
    TaskBuilder<Slotted> stack(TaskSlot<Words>& slot) requires std::same_as<Stage, Priorized> {
        return TaskBuilder<Slotted>{
            Slotted{stage.name, stage.prio, &slot.tcb, slot.stack}};
    }
```

In the `Slotted` stage, the box pointer and stack view travel together, and when they reach `entry`, they are combined into the `Armed` stage, and its own `spawn_into()` no longer needs any parameters, because everything is recorded on the chain. The example at the top of the file writes the daily usage as two lines, we'll look at it:

```cpp
//     constinit task::TaskSlot<64> a{};
//     spawn(task::named("a").prio(1).stack(a).entry(fn, arg).spawn_into());
```

The first time I read `stack(a)` both taking `span` and `TaskSlot`, I was worried that such an overload would be confused, but looking back, I was relieved: both overloads stop at the same station on `Priorized`, the compiler chooses based on whether you pass a bare `span` or `TaskSlot`, we can see that it recognizes the path better than people.

### Arena Path: You Just Say the Number of Words, the Memory is Cut by the Site

The third path doesn't even let you declare a `TaskSlot`. `src/arch/arm_cortex_m3/launch.cpp` prepares a 2048-byte static memory during the wiring phase, and the declaration comes with comments:

```cpp
// the wiring-time task block: ~8 demo-sized tasks of (TCB + 64-word stack).
// A named number beats a spell — resize here, the Check reports if dry
constexpr std::size_t kTaskArenaBytes = 2048;

alignas(8) constinit std::byte g_task_block[kTaskArenaBytes]{};
constinit ZerOS::task::Arena g_task_arena{g_task_block, sizeof(g_task_block)};
```

Arena (literally meaning an arena, in our case it refers to a block of static memory set aside for allocation) lives in `include/ZerOS/kernel/sched/arena.hpp`, the allocation action is just a pointer shift:

```cpp
    // alignment-rounded slice, or nullptr when the block ran dry
    [[nodiscard]] void* take(std::size_t bytes, std::size_t align) noexcept {
        const auto base = reinterpret_cast<std::uintptr_t>(cursor_);
        const auto miss = (align - (base % align)) % align;
        if (miss + bytes > left_) {
            return nullptr;
        }
        cursor_ += miss;
        left_ -= miss;
        auto* out = cursor_;
        cursor_ += bytes;
        left_ -= bytes;
        return out;
    }
```

The file header writes its worldview in one sentence, the original sentence is: "Running dry is a wiring bug, the caller decides how loud to die", we reverse it to: "If it can't be cut, it means there's a wiring error, the decision of how loud to die is handed over to the caller." `launch()` adopts the loud way of reporting:

```cpp
sched::TCB* launch(task::Launchable spec) noexcept {
    // even words keep the stack TOP 8-aligned — same rule TaskStack asserts
    ZerOS::debug::Check(spec.words % 2 == 0, "launch: odd stack words");

    auto* box = static_cast<sched::TCBStorage*>(
        g_task_arena.take(sizeof(sched::TCBStorage), alignof(sched::TCBStorage)));
    auto* words =
        static_cast<std::uint32_t*>(g_task_arena.take(spec.words * 4, 8));
    ZerOS::debug::Check(box != nullptr && words != nullptr, "launch: task arena dry");

    return &spawn(task::named(spec.name)
                      .prio(spec.prio)
                      .stack(std::span<std::uint32_t>{words, spec.words})
                      .entry(spec.run.function, spec.run.argument),
                  *box);
}
```

The caller passes to `launch` only a `Launchable`, its fields are name, priority, number of stack words, entry function plus arguments. The TCB box and stack memory are all cut by the site, the chain therefore adds two stages, `words(n)` sends you from `Priorized` to `Worded`, `entry` then completes `Worded` into `Launchable`, the declaration is complete. You should note that after `take`, there is no `free`, nor is there any room for retry, once the scheduler starts, the entire block of memory is frozen, so it serves only the period of starting the wiring. The note in the comment, "frozen once the scheduler starts", says exactly its boundary. Another small change is that the idle bed has been incorporated into the kernel itself, `start_scheduler()` no longer takes parameters, and the declaration in `switch.hpp` has changed to:

```cpp
[[noreturn]] void start_scheduler(); // idle 栈内核自带
```

Among the three paths, my own preference is clear: for daily demos, the arena path is preferred, the declaration is the shortest, the 2048-byte budget is watched by `Check`, if you really need to precisely control the position of a certain storage (such as putting it into a specific linking segment), the slot path and hand-laid path are still available, the path is not blocked. The heartbeat demo performs a small use of the arena path, cloning the same shape into two tasks:

```cpp
auto shape = ZerOS::task::named("HeartBeat").prio(1).words(64);

// clone the shapes
ZerOS::arch::cortex_m3::launch(shape.entry(heartbeat, &banner));
ZerOS::arch::cortex_m3::launch(shape.entry(heartbeat, &task));
```

We'll look at `shape`: it's a half-finished product of the `Worded` stage, `entry` splits into a complete `Launchable` every time it's called, two heartbeat tasks run separately with different parameters.

### One Step Missed, the Compiler Speaks

The common defense line for the three paths is the same, we'll look at it: it's the type state of the `TaskBuilder`, each member of `TaskBuilder` is hanging `requires std::same_as<Stage, ...>`, from an incorrect stage calling an incorrect step, the compiler directly refuses. The file header of `task.hpp` says it all, the original sentence is at the beginning of the file:

```cpp
// The task factory: the chain IS the contract. Skip a step and the rest
// of the chain simply does not exist — no comment can save you then.
//   named("blink").prio(1).stack(stack).entry(fn, arg).spawn_into(box)
```

We've seen the defense line before, we've tested its value in the drop-wakeup chapter, `0054cf8` did the job of connecting all three paths to the same defense line, no constraints on the chain have been relaxed, regardless of where the memory comes from.

## Writing If with the Linker: Weak and Strong Interrupt Handlers

More demos mean new problems: not every demo wants to listen to the serial port. The demo that receives bytes must catch the USART1 interrupt, the timer and heartbeat demos have no interest in the bytes received by the serial port, but if no one handles the RXNE interrupt, the flag won't be cleared, and the interrupt will keep coming again and again. `0054cf8`'s answer is a single translation unit `src/board/stm32f103_bluepill/uart_drain.cpp`, its full text is only 13 lines, I'll give you the code with comments:

```cpp
// Default RX consumer, its OWN translation unit: when an image has nobody
// listening, bytes rain into this drain. A demo that wants them defines
// its own strong handler and the linker drops this weak one.

#include "stm32f1xx.h"
#include "uart.hpp"

extern "C" void USART1_IRQHandler() __attribute__((weak));
extern "C" void USART1_IRQHandler() {
    if ((USART1->SR & USART_SR_RXNE) != 0) {
        (void)USART1->DR; // read DR, drop the byte, clear the flag
    }
}
```

`__attribute__((weak))` declares it as a weak symbol, and the linker's rule is: among the same name symbols, the strong definition is in, the weak one gives way. So the demos that don't define a handler (heartbeat, timer, inversion) link in the drain, when bytes come, it reads the data register once and clears the flag, the water is drained, and no one is awakened. The demos that want to listen to the bytes define a strong handler themselves, the linker throws away the weak definition, and only one is left in the binary. The choice happens during the linking phase, there is no runtime judgment branch in the firmware, I call it writing if with the linker, the `event_stream` demo gives it a live example:

```cpp
// a REAL uart interrupt may join the stream too — this strong handler
// overrides the board's weak drain
extern "C" void USART1_IRQHandler() {
    if ((USART1->SR & USART_SR_RXNE) != 0) {
        g_stream.post(ZerOS::board::uart1_getc());
    }
}
```

On the same symbol, the strong definition overrides the drain, the received bytes go into the queue. The behavior of the linker recognizing strong and not weak, I've tested it face-to-face with simulation: after the firmware runs, I fed a character 'q' into the receive side from Renode's monitor, at that time the 500 millisecond timer was feeding 'a' to 'f', the 'q' appeared in the middle of 'b' and 'c' in the serial log (the output is extracted, I only took the virtual time column here, the complete log is explained in the verification section):

```text
[INFO] usart1: [host:  1.09s|virt:   1s] got 'b'
[INFO] usart1: [host:  1.34s|virt: 1.25s] got 'q'
[INFO] usart1: [host:  1.58s|virt:  1.5s] got 'c'
```

q is what I fed in, it appears in the queue, this shows that the strong handler is indeed on duty, and the drain has indeed given way.

::: warning
The weak symbol's giving way is effective across the entire symbol name, a firmware can only have one strong definition of the same name handler. If you merge the main.cpp of two demos and compile them, both write strong handlers, the linker will report duplicate definitions instead of giving way, it's doing this for you to prevent ambiguity, we shouldn't bypass it.
:::

## Logging is Convenient, Testament is an Obligation

The demos have been divided into rooms, we also took the opportunity to clarify the printing path. `include/ZerOS/log/print.hpp` is the new formatting entry point, the header comment divides the work:

```cpp
// format → the registered global channel, in one breath. No hardware
// anywhere near this file: where the text lands is the board's business
// (register a channel), not the formatter's.
//
// An unregistered channel DROPS the line — logging is a convenience, not
// an obligation. (Testaments are obligations; those have their own teeth.)
```

We'll look at the division of `log::print`: it formats the content into a 128-byte stack buffer, and then hands it over to the global channel, and the formatter doesn't recognize any hardware from start to finish, where the text ends is decided by the board (the board registers UART1 as the channel in `demo::setup`). 128 is not a number made up on the spot, its comment has a record:

```cpp
// one line at a time; long lines truncate, they never overrun.
// 128 keeps a 64-word task stack printable (160 overflowed it — the
// canary testified twice); heavy printers should ask for 96 words
```

We'll read the record: a 160-byte line buffer once overflowed a 64-word task stack, the canary testified twice, and the buffer was reduced to 128. Another tightening is the default stack of the worker: the comment in `user_worker.hpp` says that `log::print` opens a 160-byte line buffer on the worker's stack, 64 words is too thin, this time it was caught by the canary, so the default value of `StackWords` was raised to 128. The default stack of the slot path and hand-laid path is still 64 words, if their tasks only print short lines, 64 words is still enough. A line that is too long will be truncated instead of going out of bounds, printing is convenient, and convenience should have a limit.

Testament is the other end: what we call testament, refers to the report left by the chip before it dies. `include/ZerOS/arch/arm_cortex_m3/testaments.hpp` is a new file introduced by this commit, it brings a set of default death reports, the header comment is clear:

```cpp
// The opt-in default testaments: print-and-halt for both death styles.
// Explicitly installed (nobody silently gets defaults), printing through
// the registered log channel — this file never touches a UART itself.
```

The default testament must be explicitly installed, and no one will be quietly given one, the installation action is the line `install_default_testaments()` in `demo::setup`, it gives each hook a print and shutdown implementation. If you ask what happens if you don't install, the answer is that it won't pass, the assertion in `fault.cpp` is watching:

```cpp
    // dying with no testament is not allowed either — wire one, or install
    // the defaults; silence on a hard fault is a bug, not a mode
    ZerOS::debug::Check(ZerOS::arch::cortex_m3::fault_sink != nullptr, "fault sink not wired");
    ZerOS::arch::cortex_m3::fault_sink(r);
```

If a hard error occurs and no one listens, we say it's not a working mode, but a bug, `Check` clearly states it. `switch.cpp` did the same to the stack overflow reporter, the original `overflow_reporter` was empty and silently skipped, the judgment was changed to go through an assertion:

```diff
-        if (TaskGuardHelper::fast_check_stack(*prev) &&
-            TaskGuardHelper::overflow_reporter != nullptr) {
+        if (TaskGuardHelper::fast_check_stack(*prev)) {
+            // an eaten canary with nobody listening must NOT pass in silence
+            ZerOS::debug::Check(TaskGuardHelper::overflow_reporter != nullptr,
+                                "overflow reporter not wired");
             TaskGuardHelper::overflow_reporter(prev, TaskGuardHelper::eaten_length(*prev));
         }
```

If the canary is eaten and no one listens, it's also not allowed to pass silently. Logging is a convenient thing, testament is an obligation, we laid the foundation in "Leaving a Will Before the Chip Dies: HardFault Scene, Stack Canary, and Zero Allocation Log", but at that time the reporter was optional, `0054cf8` turned optional into an obligation: you can replace the default implementation, but you can't not connect.

## Eight Demos, One by One

The registration center has eight rooms, we'll report the performance of each room, and align the lineage along the way: they are mostly old plays on a new stage, the scenes we've personally tested in previous chapters now each have their own room.

Heartbeat is the new host, two heartbeat tasks with a 500 millisecond rhythm, performing the shape cloning of the arena path. Event_stream performs the queue plus timer feeding, and is paired with the strong handler we just saw. Inversion performs the reversed theater: L holds the lock and waits for 300 milliseconds, M occupies the CPU's idle time, H arrives 100 milliseconds late to request the lock, its script comes from "Watching M's Three Seconds of Silence: Mutex, Priority Inheritance, and Ownership Transfer". Bottom_half performs the bottom half delay, the timer is in the position of the interrupt, every second sends a heavy job, the worker receives it and sleeps for 200 milliseconds in the task's context, the script comes from the one we disassembled, "Interrupts Only Send Notes: Ring Buffer, Worker Thread, and Bottom Half Delay". Timers perform the software timer, a one-time 300 millisecond, a periodic 250 millisecond, the script comes from the one we wrote, "Wait 300 Milliseconds, Don't Raise a Task: Software Timer and fn+void* Hard Currency". Events_notify performs the AND mask plus direct notification of the event group, two keys are pressed down at 400 and 900 milliseconds respectively, at 600 milliseconds the postman directly delivers a 42 to the task's mailbox, the script comes from the one we wrote, "Wake Everyone Up, Each Rechecks the Truth: Event Group, Direct Notification, and Full Ring Backpressure". Testaments perform the testament itself, the normal run is a quiet heartbeat, you can choose its death method by injecting characters from the monitor. Round_robin performs the equal time slice, two never-sleeping spin tasks of the same priority fight for three seconds, the alternating output is its characteristic, the script comes from the one we disassembled, "Count to Ten and Switch: Equal Time Slices and Non-Inherited Slice Debt".

After you checkout the repository to `0054cf8`, follow the following commands, the firmware and simulation will all be there:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake
cmake --build build
cmake --build build --target run-heartbeat
cmake --build build --target run-event_stream
cmake --build build --target run-inversion
cmake --build build --target run-bottom_half
cmake --build build --target run-timers
cmake --build build --target run-events_notify
cmake --build build --target run-testaments
cmake --build build --target run-round_robin
```

Each `run-*` target compiles the firmware and then starts Renode, runs for 5 seconds, and the serial log is left in the root of the repository `run.log`. I have to explain one environmental difference: my local Renode is 1.17.0, when loading `bluepill.repl`, it doesn't recognize the type `I2C.STM32F4_I2C`, I commented out the two nodes i2c1, i2c2 and the interrupt wiring, and the simulation started. This is the same method used in the verification of "Faking a Birth Certificate on the Stack: PendSV, SVC, and the First Context Switch", the eight demos don't take the i2c path, and the behavior is unaffected.

The following outputs are all representative excerpts from the respective `run.log`, the time is taken from Renode's virtual time column, each demo's complete log is longer than what is shown.

Heartbeat's two beats: two tasks with the same parameters but different content, each beats every 500 milliseconds, we count their beats:

```text
[INFO] usart1: [virt: 0s] ZerOS demo: heartbeat
[INFO] usart1: [virt: 46.9µs] Hello! ZerOS
[INFO] usart1: [virt: 52.23µs] A heartbeat example!
[INFO] usart1: [virt: 0.5s] Hello! ZerOS
[INFO] usart1: [virt: 0.5s] A heartbeat example!
[INFO] usart1: [virt: 1s] Hello! ZerOS
[INFO] usart1: [virt: 1s] A heartbeat example!
```

Inversion's quiet window. We look: M originally shouts every 100 milliseconds, the virtual time reaches 2.0 seconds, L takes the lock and starts waiting, at 2.1 seconds H requests the lock, after that M is silent for 300 milliseconds, at 2.3 seconds L releases the lock, H immediately gets it, and M's voice returns in the same millisecond:

```text
[INFO] usart1: [virt:      2s] M: running
[INFO] usart1: [virt:      2s] L: locked, busy 300ms
[INFO] usart1: [virt:    2.1s] H: wants the lock
[INFO] usart1: [virt:    2.3s] L: unlocked
[INFO] usart1: [virt:    2.3s] H: LOCKED (inheritance paid off)
[INFO] usart1: [virt:    2.3s] M: running
```

M's 300 milliseconds of silence is the cost of inheritance: L raises H's level to finish the waiting, M is squeezed out. The warehouse itself writes "inheritance paid off", it's the warehouse's own line, the CI's smoke check grep is looking for it, we'll see it in the next chapter.

Bottom_half's heavy job, the job comes in, sleeps for 200 milliseconds, and reports completion, the sleep happens in the worker's task context, we count the time, it sleeps fully:

```text
[INFO] usart1: [virt: 1s] worker: job #1 in, napping 200ms
[INFO] usart1: [virt: 1.2s] worker: job #1 done
[INFO] usart1: [virt: 2s] worker: job #2 in, napping 200ms
[INFO] usart1: [virt: 2.2s] worker: job #2 done
```

Timers' two timers, the one-time one is lit at 300 milliseconds and disappears, the periodic one beats every 250 milliseconds, we listen to its rhythm:

```text
[INFO] usart1: [virt: 0.25s] beat #1
[INFO] usart1: [virt:  0.3s] one-shot: fired, going cold
[INFO] usart1: [virt:  0.5s] beat #2
[INFO] usart1: [virt: 0.75s] beat #3
```

Events_notify's two events, the 600 millisecond letter arrives earlier, the 900 millisecond key is assembled, the AND mask is released, we match the time:

```text
[INFO] usart1: [virt: 0.6s] nt: letter says 42
[INFO] usart1: [virt: 0.9s] evt: both keys in (bits=0x00000003)
```

Round_robin's two spin tasks, rr1 appears at 47 microseconds, rr2 follows at 10 milliseconds, we look at them each reporting once every 500 milliseconds, the interval is constant at 10 milliseconds, no one is starving:

```text
[INFO] usart1: [virt:   47.15µs] rr1 alive
[INFO] usart1: [virt:      10ms] rr2 alive
[INFO] usart1: [virt:      0.5s] rr1 alive
[INFO] usart1: [virt:     0.51s] rr2 alive
[INFO] usart1: [virt:        1s] rr1 alive
[INFO] usart1: [virt:     1.01s] rr2 alive
```

Testaments' normal run is a line of alive, the real play is waiting for us in the injection. I fed `0x79` into the serial port from Renode's monitor, the terminal task smashed its stack canary, the next PendSV patrol handed the report out:

```text
[INFO] usart1: [virt: 1.25s] term: smashing my canary
[INFO] usart1: [virt: 1.25s] [STACK OVERFLOW] task=term eaten=2
```

Feeding `0x78` takes another path, we look: `udf #0` burned an undefined instruction, the hard error triggered on the CPU level immediately, the complete report along with the eight words of the scene was left:

```text
[INFO] usart1: [virt: 1.23s] term: executing an undefined instruction, goodbye
[INFO] usart1: [virt: 1.23s] *** HARD FAULT *** task=term prio=0
[INFO] usart1: [virt: 1.23s] pc=0x080004EA lr=0x08000D8B xpsr=0x01000000
[INFO] usart1: [virt: 1.23s] cfsr=0x00010000 hfsr=0x40000000 bfar=0x00000000
[INFO] usart1: [virt: 1.23s] stack: 00000001 20000383 C0000000 08000D79 00000000 08000D8B 080004EA 01000000
```

cfsr's 0x00010000 is the undefined instruction bit, hfsr's 0x40000000 is the FORCED bit, we matched the report with the ARMv7-M register definition, it matches. The same demo performed two deaths, the testament was left in both plays, the line installed in `demo::setup` was on duty in both plays.

## Verification

We'll gather the verifiable facts, and I'll explain the verification method for each.

The first is the building and running of the eight demos. You checkout to `0054cf8`, configure the cross-build with the above `cmake -B`, all eight `zeros-demo-*` targets should be compiled, and each `run-*` can be run in Renode with one line. The firmware size is real, the cross toolchain is the same one we used in the scheduler disassembly chapter, the version is arm-none-eabi-g++ 16.2.0, I copied the report from `arm-none-eabi-size` into a table (text, data, bss are code segment, initialized data, zero-initialized data):

| demo | text | data | bss |
| --- | --- | --- | --- |
| heartbeat | 3448 | 364 | 2096 |
| event_stream | 4168 | 420 | 2096 |
| inversion | 4248 | 348 | 2104 |
| bottom_half | 4040 | 1028 | 48 |
| timers | 3032 | 396 | 48 |
| events_notify | 4308 | 436 | 2104 |
| testaments | 4212 | 380 | 2096 |
| round_robin | 3188 | 348 | 2096 |

The differences in bss have their reasons, we'll give two clues: the demos that take the arena path cut the task storage into the 2048-byte site in bss, the tasks in bottom_half and timers are either fewer or stored in the data side. I haven't verified this thoroughly, the two clues are enough, I won't make further assertions.

The second is the host-side verification. I used `git archive 0054cf8` to export the repository to a copy in /tmp, the real STM32CubeF1 was copied into the third_party of the copy (it's just a reference text in the repository, the archive is empty, we filled it in when copying), and then configured the test build according to the README:

```shell
cmake -B build-host -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

My local GCC is 16.2.1, the ctest score is real: all 14 tests passed, the total time is 0.21 seconds. The scheduler test's own list is 70 assertions, 14 test cases, one of which is new in this commit, the Arena's alignment, cutting dry, not looping, all handled by it:

```text
startup arena: aligned slices, dry block refuses, no wraparound
```

The test case specifically verified the alignment padding at the start without counting into the size: we look at its arithmetic, `take(1, 8)` only moves 1 byte, the next 8-byte alignment request has to pay 7 bytes of padding, and the remaining 8-byte site is really cut off. The allocator during the wiring period looks simple, the handling of the boundary is not vague at all.

The third is the injection experiment, the three cases of q, 0x78, 0x79 in the previous testament and strong handler sections are all fed by me from Renode's monitor, the `WriteChar` command sends bytes to the receiving side, the serial log's response is all within the 5-second simulation window.

The fourth is what I haven't verified in this article, I'll be honest: in the eight rooms, the mechanism of events_notify and round_robin (the waiting chain of event groups, the switching rhythm of time slices) I've only verified the surface behavior of the serial port, the kernel-side derivation belongs to their respective chapters, we won't take the stage here. The serial output I've pasted is all extracted, I have to explain this: the original number of lines for each log is more than what is shown, when you rerun, take the complete record in your own `run.log` as the standard.

## Next Article

The rooms are divided, the scripts are all in place, the natural next step is to let the machine verify them one by one, its name is CI. The next article will disassemble d48f5a3, its title is what you've seen in related: "Run for Five Seconds, Say Your Line: CI Smoke Testing, Include Firewall, and Size Gate", four checks turn the eight demos of this site into one, each demo's line is guarded by grep in CI. We'll see you there.
