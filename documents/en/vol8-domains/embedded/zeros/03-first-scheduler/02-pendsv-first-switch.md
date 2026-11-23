---
title: "Fabricating a Birth Certificate at the Stack Top: PendSV, SVC, and the First Context Switch"
description: "0bf9461 transformed ZerOS from a clock library into an RTOS. This article discusses its mechanism: eight bytes of hardware exception frame, fabricate_frame forging a birth certificate at the top of a new task's stack (xPSR must set the Thumb bit), svc 0 as the springboard for the first context switch, EXC_RETURN 0xFFFFFFFD and naked assembly saving/restoring r4-r11; it also fulfills the foreshadowing left in station 2, changing 0xF0 to 0xFF in init_time, making SysTick and PendSV be on the same priority level; I exported the source code from the /tmp copy according to 0bf9461 for cross-compilation, and saw tasks A/B print alternately at 1000/700ms in Renode"
chapter: 3
order: 2
tags:
  - stm32f1
  - advanced
  - 嵌入式
  - 寄存器
  - concepts
difficulty: advanced
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 26
prerequisites:
  - "The Definition of a Second: Tick, Wraparound Arithmetic, and Expiry Queue"
  - "Who is the Most Urgent, Who Gets to Play: Ready Bitmap, Task Three States, and Selection Arbitration"
related:
  - "Who Owns the CPU: From Ready Bitmap to Interrupt Wakeup"
current_status:
  title: Guide to Fabricating a Birth Certificate
  detail: The ZerOS chapter is currently being polished
  variant: polishing
  icon: 🪄
translation:
  source: documents/vol8-domains/embedded/zeros/03-first-scheduler/02-pendsv-first-switch.md
  source_hash: 8171cc6e445816299f721ee27b94b1de03c298ad5f39068bea82d651b51bfbfe
  translated_at: '2026-11-22T13:34:16+00:00'
  engine: qwen3:14b
  token_count: 15637
---
# Fabricating a Birth Certificate at the Stack Top: PendSV, SVC, and the First Context Switch

When I wrapped up the previous station, I left an unfulfilled foreshadowing: the 0xF0 in `init_time` was later changed to 0xFF, making SysTick and context switch interrupts be on the same priority level. You may ask why the heartbeat of the timer is on the same level as the switch, and the answer is right here, but it has to wait until we finish explaining the switching mechanism before it becomes clear, because the logic of 0xFF is embedded in the switching process, and we have to witness the switch from birth to the first occurrence before we understand why the two interrupts are tied together.

The commit we are going to analyze is `0bf9461`, with the subject "feat: Sched OK", which includes 674 lines of code, covering the scheduler, the switching porting layer, and the dual-task demo on the board. The arbitration logic for selecting the next task, such as how the ready bitmap selects a person and whether preempted tasks return to the head of the queue, is covered in the article on the arbitration side, and I won't repeat it here. In this article on the mechanism side, we will answer only one question: the CPU state of a task that has never run is fabricated from scratch, and we will explain exactly how we fabricate it and how we make it run after fabrication.

## Exception Frame: Eight Bytes Each for Hardware and Us

Each task on the Cortex-M3 has its own stack, and the skeleton at the top of the stack (I call the frame that records the CPU context at the top of the stack the skeleton, and I will refer to it as such from now on) determines where the CPU will continue running. The division of labor between the two halves of the skeleton is clear: one half is pushed by hardware, and the other half is pushed by us. Let's look at `include/ZerOS/arch/arm_cortex_m3/arch.hpp`, which is a new file introduced by 0bf9461:

```cpp
struct ExceptionFrame {
    std::uint32_t r0;
    std::uint32_t r1;
    std::uint32_t r2;
    std::uint32_t r3;
    std::uint32_t r12;
    std::uint32_t lr;
    std::uint32_t pc;
    std::uint32_t xpsr;
};

struct SoftwareFrame {
    std::uint32_t r4;
    std::uint32_t r5;
    std::uint32_t r6;
    std::uint32_t r7;
    std::uint32_t r8;
    std::uint32_t r9;
    std::uint32_t r10;
    std::uint32_t r11;
};

static_assert(sizeof(ExceptionFrame) == 8 * sizeof(std::uint32_t));
static_assert(sizeof(SoftwareFrame) == 8 * sizeof(std::uint32_t));
```

The eight bytes of `ExceptionFrame` are defined by the ARMv7-M architecture. When an exception occurs (including interrupts and `svc`), the hardware automatically pushes eight registers (r0, r1, r2, r3, r12, lr, pc, xPSR) in order onto the stack of the current task. After the push, the exception handler works on the skeleton. When returning from the exception, the hardware pops them back in the same order. At the end of the file, two `static_assert` statements guard the layout of the eight-byte frame. If someone ever changes the fields, the firmware will not even compile, and layout mismatches are not exposed until runtime.

The eight bytes of `SoftwareFrame` are r4 to r11. Their ownership needs to be clarified: the architecture only specifies the eight bytes pushed by hardware, and the rest of the registers are not managed by the architecture. r4 to r11 belong to the called function's saved range. If the handler needs to modify them, it must save and restore them itself. Therefore, the code for switching is written by us to push the software's eight bytes manually, and the position of the push is lower than the hardware's eight bytes, because the hardware only recognizes the eight bytes at the top of the stack when returning from an exception. The instructions for pushing and popping are handled in the assembly section, which we will cover in the assembly section.

## fabricate_frame: Fabricating a Fake Context at the Stack Top

When a task has not run yet, the stack is empty, but the switching logic requires that each task's stack top must have a complete context. Therefore, the task of fabricating a complete context is done by us. I have brought the code here, from `src/arch/arm_cortex_m3/switch.cpp`'s `fabricate_frame` section, and we will look at the entire section:

```cpp
void fabricate_frame(ZerOS::sched::TCB& t) {
    auto* top = t.stack_view_.data() + t.stack_view_.size();
    auto* hw = reinterpret_cast<ExceptionFrame*>(top) - 1;
    auto* sw = reinterpret_cast<SoftwareFrame*>(hw) - 1;

    *sw = {}; // Clear, memset set bytes, we say class clean
    *hw = ExceptionFrame{
        .r0 = reinterpret_cast<std::uint32_t>(&t.task_wrapper_), // 任务参数
        .r1 = 0x00000000,
        .r2 = 0x00000000,
        .r3 = 0x00000000,
        .r12 = 0x00000000,
        .lr = reinterpret_cast<std::uint32_t>(&Trap), // 返回即陷阱
        .pc = reinterpret_cast<std::uint32_t>(&sched::UserTaskWrapper::InvokeUserTask), // 跳板
        .xpsr = 0x01000000u,
    };

    t.stack_pointer_ = reinterpret_cast<std::uint32_t*>(sw);
}
```

The pointer arithmetic is simple enough for you to follow: the `span` of the stack grows from low address to high address, and the top of the stack is located at `data() + size()`. `hw` subtracts the width of an `ExceptionFrame` from the top of the stack, meaning that the hardware's eight bytes are located in the eight bytes just below the top of the stack. `sw` then gives way to the software save area. After all the fields are filled, `stack_pointer_` stores the address of `sw`, and the subsequent switching assembly uses it to continue the entire context.

Let's go through the purpose of each field in the fake context one by one.

`pc` is filled with `InvokeUserTask`, which is the point where the CPU starts executing after the exception return. Its home is in `include/ZerOS/kernel/sched/task_control_block.hpp`. Let's look at it:

```cpp
struct UserTaskWrapper {
    using UserTask = void (*)(void*);
    UserTask function;
    void* argument;

    // When Marking Static, it is free, plain ABI
    static void InvokeUserTask(UserTaskWrapper& wrapper) {
        if (wrapper.function) {
            wrapper.function(wrapper.argument);
        }
    }
};
```

It is made into a `static` member function, and the line in the comment "it is free, plain ABI" brings out the reason: when returning from an exception, the CPU directly inserts `pc` into the program counter and jumps, without going through any C++ calling convention. Therefore, the function's parameters must be received in the most ordinary ABI, and a reference in the shape of `this` (i.e., `wrapper`) is exactly suitable. If you make it a regular member function or a callable with captures, the first parameter is unclear, so the shape of Plain ABI is chosen here, and the trick of jumping directly is valid.

`lr` is filled with `Trap`, whose home is in `include/ZerOS/arch/arm_cortex_m3/trap.hpp`. Let's look at its shape:

```cpp
extern "C" [[noreturn]] inline void Trap() {
    while (1) {
        asm volatile("wfi");
    }
}
```

After `InvokeUserTask` runs the user function, it `returns`, and the return address is `lr`'s `Trap`, which falls into a `wfi` dead loop (waiting for an interrupt). The task function is not allowed to return, and if it returns, it falls into the trap. The name `Trap` brings out the intention, and you can see it clearly at a glance.

`r0` is filled with `&t.task_wrapper_`, and we put the user function and its parameters inside it, passing them as parameters to the jumpboard. The remaining r1, r2, r3, r12 are filled with zeros, and we use zero as the initial value, which is the cleanest choice.

The most important value is `xpsr = 0x01000000u`. I spent a long time on it when I was writing the article. 0x01000000 corresponds to the T bit (Thumb bit) of xPSR. Cortex-M3 only recognizes Thumb instructions, and when returning from an exception, the hardware checks the T bit. If it finds zero, it immediately enters HardFault. If you forget to set it, the fake context looks correct, the task is switched, but the CPU falls on the first instruction of `InvokeUserTask`, and the entire system is dragged into a fault. The life and death of a value is determined by a single bit, and the title's "birth certificate" is chosen for it: without the T bit, the birth certificate becomes invalid.

::: warning
xPSR has other fields besides the Thumb bit (condition flags, interrupt numbers, etc.), and it is safe to set them all to zero when forging the frame. When returning from an exception, the hardware will take all the parts except IPSR as is, and the only check is the T bit. If you later do the same work on a core like M4 with FPU, the hardware frame will be longer, and the layout must follow the architecture manual's diagram, but the value of 0x01000000 itself is still correct.
:::

## start_scheduler: Arming and the First Switch, Never to Return

Now that we know how to fabricate a context, let's see how it is ignited. To get a task that has never run onto the CPU, the most straightforward idea is to directly call its function in C++. However, doing so would still be running the context of `main`: using the stack of `main`, the registers of `main`, and the task's own stack would not be touched at all, and the switch we want would never happen. The warehouse's method is to ignite the fire through an exception. Why exactly an exception, we will explain after looking at the code. The code is at the end of `switch.cpp`, the first part is responsible for the `spawn`:

```cpp
void spawn(ZerOS::sched::TCB& t) {
    fabricate_frame(t);
    system_sched.add(&t);
}
```

The declaration in the header file has Chinese comments, and the line for `spawn` says "forge frame + add". Let's look at the function body, which does exactly these two things: fabricate the context for the task and then hand it over to the scheduler to enqueue.

The second half of the ignition is `start_scheduler`, let's look at it:

```cpp
void start_scheduler(std::span<std::uint32_t> idle_stack) {
    auto idle = system_sched.fetch_idle_task();
    idle->task_wrapper_ = {+[](void*) {
                               for (;;) {
                                   asm volatile("wfi");
                               }
                           },
                           nullptr};
    idle->stack_view_ = idle_stack;
    fabricate_frame(*idle);

    *kShpr3 = (*kShpr3 & 0xFF00FFFFu) | (0xFFu << 16);
    asm volatile("dsb\nisb\nsvc 0");
    for (;;) {
    }
}
```

Its comment says "arm + first switch, never to return", and you can see it marked with `[[noreturn]]`. After the scheduler starts running, `main` can never return, and the empty loop at the end is actually unreachable, placed there to make the compiler feel at ease. The things in the function, we will go through in order: the idle task body is made into a `wfi` loop (let the CPU sleep when there is nothing to do), the stack is given to it, and a fake context is fabricated for idle. The priority of PendSV is written later, and we will talk about the foreshadowing section later. Finally, it falls on `svc 0`, and the first switch is ignited from here.

`svc` is the Supervisor Call instruction, which immediately triggers an SVC exception. The handling program for it is in the 11th slot of the vector table. You might ask, why isn't the first switch written as a direct function call, why go through an exception? Because the exception return is the only channel in our hands that can put "any context" into the CPU: when `svc` comes in, the hardware pushes the context of `main` (the context of `main` is not needed by us) onto the MSP. After the handler finishes its work, it does an exception return, and where it returns to, and which stack it uses, is all arranged by us in the handler. The stack left by `main` on the MSP is never touched again, the MSP is left for interrupts, and the tasks are moved to the PSP, and the division is completed in this one `svc 0`.

The entire handler is a piece of inline assembly, let's look at it:

```cpp
extern "C" [[gnu::naked]] void SVC_Handler() {
    asm volatile("cpsid   i\n"
                 "bl      context_switch\n"
                 "ldmia   r0!, {r4-r11}\n"
                 "msr     psp, r0\n"
                 "ldr     lr, =0xFFFFFFFD\n"
                 "cpsie   i\n"
                 "bx      lr\n");
}
```

`[[gnu::naked]]` tells the compiler not to generate a prologue and epilogue (stacking, restoring registers, etc.), because every byte on the stack is managed by the handler itself. If the compiler is unaware and also stacks, the layout will be completely messed up. Let's go through each line:

`cpsid i` turns off the global interrupt, and no one can enter during the switch. `bl context_switch` calls into the C++ world to select the next task, and r0 goes in with "the stack pointer to be saved", and comes out with "the stack pointer of the next task". `ldmia r0!, {r4-r11}` pops the eight bytes of the software save area back into r4 to r11, and after popping, r0 itself also grows to the top of the hardware frame. `msr psp, r0` writes the pointer into the PSP (the stack pointer for thread mode), and the task now runs on the PSP. `ldr lr, =0xFFFFFFFD` is the key to the exception return: 0xFFFFFFFD tells the hardware to "return to thread mode, use the PSP, no FPU extension". `cpsie i` turns on the interrupt, and finally `bx lr` executes the exception return, and the hardware pops the eight bytes from the top of the stack pointed to by the PSP, r0 to r3, r12 are the initial values we forged, and pc falls on `InvokeUserTask`, and the first task is thus alive.

The C++ side's `context_switch` has only three lines, let's take a look:

```cpp
extern "C" std::uint32_t* context_switch(std::uint32_t* saved_sp) {
    if (saved_sp != nullptr) {
        system_sched.current_task()->stack_pointer_ = saved_sp;
    }
    return system_sched.pick_next()->stack_pointer_;
}
```

If the argument is not empty, it means there is a task currently running, and we save its stack pointer into its TCB. If the argument is empty, it means no task is running, and we directly select the next one. The selection of the next task is handled by `pick_next`, which was discussed in the article on the arbitration side. Here I truthfully report one thing: on the path of `svc 0`, r0 is not zeroed before entering `context_switch`. At this moment, `current_` is still a null pointer, and as long as the residual value of r0 is not zero, the dereference of `current_task()` will write to a wild address, which becomes undefined behavior. In real runtime, it didn't crash, relying on luck, the residual value of r0 at that moment was just not problematic. The next commit in the warehouse (6308dd0) added a line `movs r0, #0`, with the comment "first switch ever: nobody is running, nobody to save", turning luck into an explicit `nullptr`. I show you the original shape at that time, because it's very useful for understanding the path of the first switch: you can see with your own eyes that there is no task to save at the time of the first switch, and the code at that time expressed it with an unwritten branch.

The handler for PendSV is the same skeleton as SVC, with the additional "save" part. Let's compare them:

```cpp
extern "C" [[gnu::naked]] void PendSV_Handler() {
    asm volatile("cpsid   i\n"
                 "mrs     r0, psp\n"
                 "cbz     r0, 1f\n"
                 "stmdb   r0!, {r4-r11}\n"
                 "1:\n"
                 "bl      context_switch\n"
                 "ldmia   r0!, {r4-r11}\n"
                 "msr     psp, r0\n"
                 "ldr     lr, =0xFFFFFFFD\n"
                 "cpsie   i\n"
                 "bx      lr\n");
}
```

PendSV is switched out from the task (the task runs on the PSP), so it needs to `mrs r0, psp` to get the current task's stack pointer, and push r4 to r11 into it using `stmdb r0!, {r4-r11}` (`db` means decrement before, and after pushing, the pointer exactly falls on the software save area, continuing the layout we mentioned earlier). `cbz r0, 1f` is a fallback insurance: if the PSP is zero, it means no task is running, and it skips the save step and directly selects the next one. The following five lines are exactly the same as SVC. If you read the two handlers side by side, the symmetry of save and restore is clearly visible.

In the future, when a task voluntarily yields the CPU, or sleeps until it is awakened, or a more urgent task becomes ready, we will see the path of PendSV. The scheduling API itself never directly switches from the calling context, it only records a switch request. The real work is done by PendSV. How is the request recorded? It's written in the following line:

```cpp
void CortexM3SwitchPort::request_switch() {
    *kIcsr = (1u << 28); // Set the PendSV intrs
}
```

The full name of ICSR is the Interrupt Control and Status Register, its address is at 0xE000ED04, and its bit 28 is PENDSVSET. We write 1 into it, which suspends PendSV. Although it is suspended, it does not switch immediately, which is the method of delayed switching: the interrupt handler calls it, and the switch does not happen inside the interrupt, but after the interrupt exits, PendSV queues up and switches. The design of the delay will be fulfilled in the article on interrupt wake-up later, and we just need to remember the mechanism today.

## Fulfilling the Foreshadowing: Why SysTick and PendSV Are on the Same Priority Level

Now we can answer the question left at the previous station. At the previous station, the priority of `init_time` was 0xF0. Let's look at the changes in `src/arch/arm_cortex_m3/time_kernel_cm3.cpp` of 0bf9461:

```cpp
void ZerOS::arch::cortex_m3::init_time(std::uint32_t cycles_per_tick) {
    // same level with PendSV
    *kScbShpr3 = (*kScbShpr3 & 0x00FFFFFFu) | (0xFFu << 24);
    *kSystRvr = cycles_per_tick - 1;
    *kSystCvr = 0;
    *kSystCsr = kSystEnable | kSystTickInt | kSystClkSource;
}
```

In `start_scheduler`, the line for PendSV is `0xFFu << 16`, and the line for SysTick is `0xFFu << 24`. Let's compare them, writing to the same register SHPR3's two bytes: PendSV's priority field is in bits 16 to 23, and SysTick's is in bits 24 to 31 (PendSV and SysTick's two kernel exception numbers are 14 and 15, and SHPR3 is exactly for them). Both are written with 0xFF, and the comment line "same level with PendSV" explains the intention.

You may ask why both must be 0xFF and why they must be on the same level. The Cortex-M3's standard is that the smaller the value, the more urgent it is, and 0xFF is the lowest level. We will explain it from both ends.

We let PendSV be at the bottom, to avoid cutting in. The task switching work requires stacking, unstacking, and switching the stack pointer, and the entire action's premise is that the old task's context has been fully saved. If PendSV's priority is high, an USART interrupt may cut in while the switch is halfway through. It pushes its own stack using MSP, not touching the task's stack, but it sees r4 to r11 as residual values from the halfway switch. And if it calls the scheduling API, it may suspend a new PendSV, and the context's ownership becomes a mess. After being at the bottom, any real interrupt can hold the switch's steps, and after they all finish, the switch's turn comes.

SysTick is on the same level as it because the timer's heartbeat cannot intrude into the switch. Imagine this scene: PendSV has just popped r4 to r11, the PSP has already been switched to the new task's stack, and the exception return has not been executed yet. In this narrow gap, SysTick comes in. It will run `SysTick_Handler`'s `on_elapsed`, which will modify the time queue. Its stack and the register state it sees are part of an unfinished half-finished context. If the two interrupts are not on the same level, the more urgent one can cut in, but in this scenario, both are the kernel's own heartbeat, and the matter of who is more urgent or slower should not be decided by them. Two exceptions on the same level cannot interrupt each other, and when one is running, the other can only queue up. After the running one exits, the tail chain (the hardware processes the queued exceptions in one go, without fully entering and exiting the stack) passes, and the intermediate state of the switch is thus protected. The question left at the previous station, our answer is right here: the timer's heartbeat is on the same level, and the reason has nothing to do with the judgment of who is more important, but lies in the fact that exceptions on the same level cannot interrupt each other.

## Board Demo: Two Tasks Taking Turns to Wake Up

Now that we've explained the mechanism, let's see what it looks like when it runs. To verify a scheduler, a single task cannot show the switch, and the printed pattern must match a timetable, so there must be two tasks with staggered rhythms on the field. The warehouse upgraded `example/main.cpp` from a single ticker to two tasks, and we'll look at the skeleton in two parts. The first part is the static declaration of the stack and TCB:

```cpp
alignas(8) constinit std::uint32_t stack_a[64] = {};
alignas(8) constinit std::uint32_t stack_b[64] = {};
alignas(8) constinit std::uint32_t idle_stack[32] = {};

constinit ZerOS::sched::TCB tcb_a{};
constinit ZerOS::sched::TCB tcb_b{};
```

The stack and TCB are all static objects with `constinit`, and we continue the zero-heap discipline.

After the stack and TCB are ready, the two task bodies that actually do the work come on stage. Let's look at them:

```cpp
void task_a(void*) {
    for (;;) {
        ZerOS::board::print("A\r\n");
        task_sleep(1000);
    }
}

void task_b(void*) {
    for (;;) {
        ZerOS::board::print("B\r\n");
        task_sleep(700);
    }
}
```

The two task bodies are symmetrical, both doing a loop of printing a letter and then sleeping. A sleeps for 1000ms, and B sleeps for 700ms, so the printed rhythm is inevitably staggered.

`task_sleep` is handcrafted in the example, and we'll take it out separately because the main character of the next article is hidden inside:

```cpp
ZerOS::sched::TCB* owner_of(ZerOS::base::BorrowedPtr<ZerOS::clock::TimeWaiter> w) {
    return reinterpret_cast<ZerOS::sched::TCB*>(
        reinterpret_cast<std::byte*>(w.get()) - offsetof(ZerOS::sched::TCB, action));
}

void wake(ZerOS::base::BorrowedPtr<ZerOS::clock::TimeWaiter> w) {
    system_sched.ready(owner_of(w));
}

void task_sleep(std::uint32_t ms) {
    auto cur = system_sched.current_task();
    cur->action.OnTime = wake;
    system_time.call_after_span(&cur->action, ms);
    system_sched.block(cur);
}
```

The idea of sleep is to leverage: the TCB already has an embedded `TimeWaiter` (a friend from the previous station), and we hang it in the time kernel. When it's time, the callback `wake` puts the task back into the ready queue. `owner_of` is a trick to infer the owning object from the member, subtracting the offset of the embedded member from its address to get the TCB's own address, and `offsetof` calculates this offset at compile time.

The final actions of `main` are the following lines, let's look at them together:

```cpp
    tcb_a.task_priority_ = 1;
    tcb_a.name_ = "a";
    tcb_a.task_wrapper_ = {task_a, nullptr};
    tcb_a.stack_view_ = stack_a;

    tcb_b.task_priority_ = 1;
    tcb_b.name_ = "b";
    tcb_b.task_wrapper_ = {task_b, nullptr};
    tcb_b.stack_view_ = stack_b;

    ZerOS::arch::cortex_m3::spawn(tcb_a);
    ZerOS::arch::cortex_m3::spawn(tcb_b);

    ZerOS::arch::cortex_m3::init_time(SystemCoreClock / 1000);
    ZerOS::arch::cortex_m3::start_scheduler(idle_stack);
```

The two tasks are on the same level (both have priority 1), `spawn` fabricates the frame for us and enqueues the task, the time base is set to 1kHz, and then `start_scheduler` ignites the fire. When the `svc 0` exception rings, the task_a at the head of the queue comes to life, prints a letter, and sleeps for 1000ms. The scheduler then selects task_b, which prints a letter and goes to sleep for 700ms, and whoever wakes up is the one who gets to run next. The stack of `main` is never used again, and the CPU alternates between the two tasks and idle.

## Acceptance

The chain I reproduced is as follows: I copied the warehouse to a copy in /tmp, checked out to `0bf9461` (the working tree under third_party is a gitfile, i.e., .git is just a file pointing to the real warehouse, I used `git archive 0bf9461` to export the tree), and then built the firmware with a cross-toolchain:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The build size is real, I've pasted it below:

```text
   FLASH:        1680 B        64 KB      2.56%
     RAM:         944 B        20 KB      4.61%
   text	   data	    bss	    dec	    hex
    1488	     192	    752	   2432	    980
```

The scheduler, switching layer, and dual-task demo are all included. Let's look at the numbers: flash uses 1680 bytes, RAM takes up 944 bytes (the stack and TCB's home are in bss, 752 bytes are mainly their cost), which is still far from the budget number D12 set in the board package comments, with the budget line drawn at 6KB of core flash and 1KB of RAM.

Then we ran it for 8 seconds in Renode. The experience from the previous article on running the ticker is repeated here: my local Renode 1.17.0 still can't recognize the `I2C.STM32F4_I2C` type in `bluepill.repl`, I commented out the two nodes of i2c1 and i2c2 entirely, and the simulation started (the demo doesn't go through i2c, so the behavior is unaffected). The real output of the serial port window is as follows:

```text
[INFO] usart1: [host: 0.17s (+0.17s)|virt: 0s (+0s)]
[INFO] usart1: [host: 0.17s (+0.81ms)|virt: 0s (+0s)] ZerOS multi-task demo @ Blue Pill
[INFO] usart1: [host: 0.18s (+13.96ms)|virt: 19.12µs (+19.12µs)] A
[INFO] usart1: [host:  0.19s (+0.66ms)|virt:  20.74µs (+1.62µs)] B
[INFO] usart1: [host:   0.83s (+0.64s)|virt:       0.7s (+0.7s)] B
[INFO] usart1: [host:    1.13s (+0.3s)|virt:         1s (+0.3s)] A
[INFO] usart1: [host:    1.53s (+0.4s)|virt:       1.4s (+0.4s)] B
[INFO] usart1: [host:    2.13s (+0.6s)|virt:         2s (+0.6s)] A
[INFO] usart1: [host: 2.23s (+99.71ms)|virt:      2.1s (+100ms)] B
[INFO] usart1: [host:    2.93s (+0.7s)|virt:       2.8s (+0.7s)] B
[INFO] usart1: [host:    3.13s (+0.2s)|virt:         3s (+0.2s)] A
[INFO] usart1: [host:    3.63s (+0.5s)|virt:       3.5s (+0.5s)] B
[INFO] usart1: [host:    4.13s (+0.5s)|virt:         4s (+0.4s)] A
[INFO] usart1: [host:    4.33s (+0.2s)|virt:       4.2s (+0.2s)] B
[INFO] usart1: [host:    5.03s (+0.7s)|virt:       4.9s (+0.7s)] B
[INFO] usart1: [host:    5.13s (+0.1s)|virt:        5s (+100ms)] A
```

You can see the part of virt, and the acceptance facts we want are all there: B's rhythm is one line every 0.7 seconds, A's rhythm is one line every 1 second, B's number of lines is denser than A's (the cycle ratio is 700 to 1000), and the staggered pattern perfectly matches the overlapping pattern of the two cycles. At 19 microseconds after boot, A printed the first letter, and at 21 microseconds, B followed, which is exactly the trace of the first switch: `svc 0` selected the task at the head of the queue, it printed the letter and went to sleep, and the scheduler immediately switched to the second one.

I truthfully report the unit tests on the host side: the `test/test_sched.cpp` submitted by 0bf9461 at that time couldn't compile, the test stub was missing the lock/unlock required by the `Switchable` concept, and it wasn't until the next commit that it was added, so the "Sched OK" at that time tested the board in Renode, and the green light of CI didn't exist at all. The subsequent story, including how to personally add the two empty functions, is fully expanded in the article on the arbitration side, and I won't cross its boundary.

## Next Article

The mechanism is working, but if you look back at `task_sleep`, you will find that it is three steps separated: setting the callback, `call_after_span` defense, and `block` blocking, with no locks between the three steps. If the defense is done after and before blocking, the timer just happens to reach the point, `wake` will find that the task has not yet entered the Blocked state, and the wake-up registration will be empty, and the task will continue to `block` and sleep, and no one will ever call it. This race condition of missing wake-up is real, and the submitter left it as is in the example, and it was not until 6308dd0 that `park_for` was used to put the defense and blocking into the same critical section, and the entire scheduler's type encapsulation was also redone by it. The next article we need to do is to reproduce the process of sleeping to death and see how it is blocked.
