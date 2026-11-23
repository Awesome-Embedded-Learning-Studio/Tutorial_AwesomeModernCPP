---
title: "The Same Heartbeat in QEMU: Second Board Zero-Change, Thin Port Realized"
description: "8efe93a brings ZerOS to the second board: QEMU's mps2-an385, with 13 new files, 445 new lines, and no changes to the kernel code. Removing CMSIS's arch layer to allow dual boards to share, CMSDK UART's flat four-piece set faces STM32 USART in a different look, and the memory-type code area has no Flash waiting; I saw the heartbeat on QEMU's serial port using run-mps2-heartbeat, and after a line-by-line diff of the two main.cpp files, only the header comments differ."
chapter: 9
order: 1
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - CMake
  - 工具链
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 20
prerequisites:
  - "Forging a Birth Certificate on the Stack Top: PendSV, SVC, and the First Context Switch"
related:
  - "Run for Five Seconds, Say Your Line: CI Smoke Testing, Include Firewall, and Volume Gate"
  - "Removing the Heartbeat for Experiment: NO_TICK, Zombie Tasks, and Patch-Driven Probes"
current_status:
  title: Second Board Zero-Change Deployment
  detail: ZerOS's chapter is currently being polished
  variant: polishing
  icon: 🖥
translation:
  source: documents/vol8-domains/embedded/zeros/09-second-board-and-release/01-qemu-mps2-second-board.md
  source_hash: 0aca6dbaf86b8e639abd48b19b1ac09404020b108b6bb795bc321e2b5a73ac9c
  translated_at: '2026-11-22T14:06:55+00:00'
  engine: qwen3:14b
  token_count: 12491
---
# The Same Heartbeat in QEMU: Second Board Zero-Change, Thin Port Realized

The previous stop ended on a real development board. Let's briefly recap the three articles:

- 《Welding a Stopwatch into Silicon: Clock Tree Climbs to 72MHz, Measuring the First Context Switch》climbed the clock tree to 72MHz and measured the first context switch with DWT.
- 《Textbook Loses to Prefetch Buffer: .ramfunc Reversal and an Honest Experiment Notebook》showed that even a textbook can lose to a prefetch buffer.
- 《Removing the Heartbeat for Experiment: NO_TICK, Zombie Tasks, and Patch-Driven Probes》closed the performance line.

The silicon story has come to a temporary end. Now, we return to an earlier promise: bringing the same ZerOS to the second board.

The commit we're going to break down is `8efe93a`, with the subject "feat: add qemu mps2_an385". It added 13 new files, 445 new lines, and removed 6 lines. The main character is a board we've never seen or touched: QEMU's mps2-an385. It is a Cortex-M3 development board model that comes with QEMU, corresponding to Arm's MPS2+ evaluation board. However, we have never touched the real board, and it doesn't come with any vendor libraries. The register layout is entirely determined by the simulator. We chose it as the second board precisely because of its cleanliness: the Cortex-M3 core is neatly laid out, and no vendor's boot code has followed.

We've been looking forward to "bringing it to the second board" since we started disassembling CMSIS. In 《Comments Write Lowest, Silicon Judges Highest: Removing CMSIS's Priority Shift》, we removed the vendor library from the port, and the idea of a thin port was planted at that time: the thinner the port, the easier it is to switch boards. The time has come. We don't listen to self-praise in the comments, but instead use the build, QEMU serial port logs, and line-by-line diffs to verify whether we've delivered on our promise. We'll know for ourselves after reading.

## The Kernel Didn't Change a Single Line, Only One Addition

Let's look at all the footprints `8efe93a` left in `src/CMakeLists.txt`:

```diff
 if(NOT ZEROS_BUILD_TESTS) # boards only exist for the cross build
     add_subdirectory(board/stm32f103_bluepill)
+    add_subdirectory(board/mps2_an385)
 endif()
```

The files under `include/ZerOS/kernel/` and `src/arch/arm_cortex_m3/` were not touched by this commit, not even a single byte. The new board is entirely housed in its own package under `src/board/mps2_an385/`, and the example registration file contains a public source list. Let's look at it:

```cmake
# everything a demo links besides its own main.cpp: the arm bridge(与
# bluepill 共用同一套 arch 源)+ 板级启动与 UART
set(ZEROS_DEMO_COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/src/arch/arm_cortex_m3/fault.cpp
    ${CMAKE_SOURCE_DIR}/src/arch/arm_cortex_m3/launch.cpp
    ${CMAKE_SOURCE_DIR}/src/arch/arm_cortex_m3/system.cpp
    ${CMAKE_SOURCE_DIR}/src/arch/arm_cortex_m3/this_task.cpp
    ${CMAKE_SOURCE_DIR}/src/arch/arm_cortex_m3/time_kernel_cm3.cpp
    ${CMAKE_SOURCE_DIR}/src/arch/arm_cortex_m3/switch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../uart.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../startup.cpp
)
```

Counting the list above, the bluepill demo also links to the same set of arch files. In the board package's own `CMakeLists.txt`, the author left a self-evaluation:

```cmake
# 同一份 src/arch/arm_cortex_m3/ 直接服务两块板——port 薄的红利首次兑现
```

Whether the benefits have been realized, comments don't count. Let's look ahead to the build and run. However, there is a check that has stood guard for us: the include firewall discussed in 《Run for Five Seconds, Say Your Line: CI Smoke Testing, Include Firewall, and Volume Gate》, which blocks any board or vendor names that appear in the kernel header files. We can't find "mps2" or "qemu" in `include/ZerOS/kernel/`, and if we do, that would be a problem.

## CMSDK UART: Another Look at the Serial Port

The serial port is the most changed when switching boards. UART0 on the mps2 board uses CMSDK APB UART. CMSDK is a set of standard peripheral templates defined by Arm for Cortex-M systems, and APB UART is one of them. Let's look at its `uart.cpp` in three parts. The device's home is at 0x40004000, and the first part recognizes the register's address:

```cpp
namespace {
// CMSDK APB UART 寄存器(字对齐,偏移即下标)
auto* const kData    = reinterpret_cast<volatile std::uint32_t*>(0x40004000);
auto* const kState   = reinterpret_cast<volatile std::uint32_t*>(0x40004004);
auto* const kCtrl    = reinterpret_cast<volatile std::uint32_t*>(0x40004008);
auto* const kBaudDiv = reinterpret_cast<volatile std::uint32_t*>(0x40004010);

constexpr std::uint32_t kStateTxBufferFull = (1u << 0);
constexpr std::uint32_t kCtrlTxEnable      = (1u << 0);
} // namespace
```

If you've seen the bluepill's USART, you can immediately see the depth of the difference. STM32's USART is asleep on power-up, and we need to enable the clock gate and configure the pin multiplexing. Its status bits are hidden in SR, and the enable bits are hidden in CR1. Each register family has its own home. CMSDK APB UART has a different look: four registers—DATA, STATE, CTRL, BAUDDIV—are aligned and lined up. Before sending, check the TXBF bit in STATE. If it's empty, write a byte to DATA, and that's it.

After recognizing the address, let's look at the initialization:

```cpp
void uart0_init() {
    // 无引脚无时钟门控可配——QEMU 的外设上电即在;只喂分频与使能
    *kBaudDiv = 1;          // 全速:QEMU 模型对 chardev 直通,分频不构成节流
    *kCtrl = kCtrlTxEnable; // TXEN(接 RX 中断时再补 RXEN 与 NVIC)
}
```

The two lines in `uart0_init` cover the entire initialization: we just need to write 1 to BAUDDIV. QEMU's model directly passes through the character device, and the frequency division doesn't take on the task of throttling. Then, we set TXEN in CTRL to enable sending. The register model and base address are explained in the comments: they are taken from QEMU 11.0.3's info qtree, and the device's name is cmsdk-apb-uart. The author recognizes the land given by the simulator and doesn't recognize any assumptions from the manual. In its world, the final judgment is QEMU itself. When we write the QEMU board package, this habit is worth remembering. If something can't be found in the manual, the simulator's check command is the manual.

How does one byte get sent out? Let's look at `uart0_putc`:

```cpp
void uart0_putc(char c) {
    while ((*kState & kStateTxBufferFull) != 0) {
    }
    *kData = static_cast<std::uint8_t>(c);
}
```

Sending itself is polling. We are busy waiting for TXBF to clear before writing. The comments state its scope of use, and it is present in tasks, ISRs, and the final scene of a will. Polling takes up CPU time, which is its cost. The benefit is that it doesn't depend on any interrupt, and the final few words can still be printed out in the event of a fault. The will mechanism installed in 《The Chip's Last Will: HardFault Scene, Stack Canary, and Zero Allocation Log》 relies precisely on such a channel.

How does the channel connect to the kernel? Let's look at the end of the file:

```cpp
namespace {
// the channel body: everything log::print produces lands here, byte-polled
bool channel_body(const char* s) {
    while (*s != '\0') {
        uart0_putc(*s++);
    }
    return true;
}
} // namespace

void register_uart_channel() {
    ZerOS::log::RegisterGlobalChannel(&channel_body);
}
```

The kernel's log only recognizes channels registered via `RegisterGlobalChannel`. Whether the channel is backed by STM32's USART or CMSDK's UART, the kernel has no concept of the hardware behind it, and it shouldn't. When we stopped the UART abstraction at the channel registration and didn't move it into the kernel, the reason was here: once the abstraction enters the kernel, we'll have to change one part for one board today, and another part for another board tomorrow. In the end, the kernel becomes a place for storing board-level details.

## Memory as Code Area: No Waiting for Flash

The second major change is in the memory map. Let's look at the beginning of `link.ld`:

```ld
/* mps2_an385 链接脚本 —— QEMU mps2-an385(Cortex-M3 通用开发板模型,无真机)
 * 内存映射取自 QEMU 11.0.3 info mtree(最终裁决就是这台仿真器):
 *   0x00000000  ssram1  4MB(代码,内存型 SRAM——无 Flash 等待,写它即生效)
 *   0x20000000  ssram23 4MB(数据) */

ENTRY(reset_handler)

MEMORY
{
    CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}
```

The CODE area on the bluepill side is a real Flash, and fetching instructions requires calculating wait cycles according to the manual's table. We counted those cycles in the previous stop. The mps2-an385 has no real Flash. QEMU makes the code area into memory-type SRAM, and the CPU fetches instructions from it with zero wait. Therefore, there is a reversed thing worth noting: we keep the `.ramfunc` segment, but the comment clearly states the reason it is not enabled on this board:

```ld
        /* RAM 常驻热路径开关段(与 bluepill 同构;F103 上实测负优化,
         * 本板内存型代码区无等待周期,更没有启用的理由) */
```

In 《Textbook Loses to Prefetch Buffer: .ramfunc Reversal and an Honest Experiment Notebook》, we tested that moving the hot path into RAM is actually a negative optimization on F103. On the zero-wait code area, there is even less reason to move. The performance test results have reversed the board-level design, and we welcome this flow.

The four-part countermeasure from the linking phase has also been moved over. Let's take a look; it is isomorphic to bluepill:

```ld
PROVIDE(__cxa_atexit = ASSERT(0, "global destructors banned"));
PROVIDE(_Znwj = ASSERT(0, "heap new banned"));
PROVIDE(_Znwm = ASSERT(0, "heap new banned"));
PROVIDE(__cxa_throw = ASSERT(0, "exceptions banned"));
```

Let's recite the meaning of the four-part countermeasure: whoever introduces global destructors, heap new, or exceptions in the code, the build is immediately blocked at the linking stage. The length assertion of `.init_array` follows, and the global construction at runtime is also blocked. The four refusals of freestanding C++ on bare metal, we count them on this new board, and we still find the same four clean lines.

The startup code `startup.cpp` is an isomorphic version of bluepill, but it has no vendor library available. The vector table has to be written by us, the copying of `.data` and zeroing of `.bss` also have to be written by us, and even `memset` has to be provided by us (`-nostdlib` world doesn't have anyone else to prepare it for us). I'll pick out two details for you to see. The first is the filling method of the peripheral interrupt slot, where slots 16 to 47 of the vector table are all filled with `fault_handler` as a fallback. The comment says that fallback printing is better than a lockup caused by `nullptr`. If any peripheral accidentally triggers, we'll see a line `[FAULT] halted` instead of a silent crash. The second is next to the HardFault filling method, where the comment says that QEMU directly routes unmapped accesses to the BusFault slot, which is the simulator's own path. When we troubleshoot out-of-bounds access on QEMU, the first thing we should look at is the BusFault slot.

The small agreement for running demos on QEMU, let's look at `sim/qemu/run-qemu.cmake`:

```cmake
# demo 是常驻循环,timeout 到点收场:退出码 124 = 预期内正常,其余 = 真失败

execute_process(
    COMMAND timeout ${QEMU_SECONDS} qemu-system-arm -M mps2-an385 -cpu cortex-m3
            -nographic -monitor none -serial mon:stdio -kernel ${QEMU_ELF}
    WORKING_DIRECTORY ${QEMU_ROOT}
    RESULT_VARIABLE rc
)
if(rc EQUAL 124)
    message(STATUS "(timeout - normal exit for a resident demo)")
elseif(NOT rc EQUAL 0)
    message(FATAL_ERROR "qemu exited with ${rc}")
endif()
```

The demo's main body is a dead loop, an endless loop that never ends. When the timeout is reached, it is terminated, and the exit code 124 is the timeout's own signal. The script recognizes it and we treat it as a normal ending. The `-M mps2-an385` selects the board, `-cpu cortex-m3` selects the core, `-nographic` removes the display, and the serial port is directed to our terminal, from which our output comes.

## In the Same Commit, the CI Log Filtering Was Also Removed

There are three changes unrelated to QEMU among the 13 files, all landing in `scripts/ci.sh`. Let's report them truthfully. Let's look at the original host check:

```bash
-    cmake -B "$HOST_DIR" -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug >/dev/null
-    cmake --build "$HOST_DIR" -j"$NCPU" 2>&1 | grep -E 'error|warning' && fail "host build" || true
+    # logs are printed unconditionally — success or failure, full output
+    cmake -B "$HOST_DIR" -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
+    cmake --build "$HOST_DIR" -j"$NCPU" || fail "host build"
```

There is a similar line in the size check. When the author deleted it, they left an English comment, which I've translated for you: grep filtering used to swallow the real error lines, but treated simple warnings as failures. The build output has hundreds of lines, and the filtering intention is to save logs. However, when the build really fails, the error context goes down the drain of the filter, and the person troubleshooting is left with only a few fragments. The change is unconditional full printing, printing both success and failure logs, and using the real exit code to judge failure. We've discussed this filtering shape in 《Run for Five Seconds, Say Your Line》, and the original words were that we wouldn't cover for it. In this commit, the author themselves removed it. The third landing point is in the smoke check: Renode's output was originally redirected to /dev/null, and we couldn't see anything. This commit removed the redirection, and the demo scene is now directly passed to the log.

## On the Machine: Build the mps2 Target, See the Heartbeat in the Serial Port

After the code tour, let's run it. My reproduction chain is the same: using `git archive 8efe93a` to export the tree to a copy in /tmp (third_party is the old handling of gitfile, and since the first cut, we've been using it), the toolchain on my machine is arm-none-eabi-g++ 16.2.0, and QEMU is 11.1.2 on my machine. The memory mapping in the comments is taken from 11.0.3's info mtree. I can't check if the two versions have bit-by-bit differences, because I only have 11.1.2 on my computer. The outputs below are all from it.

The two-step build and run, the repository has prepared the target for us:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --target zeros-mps2-demo-heartbeat
cmake --build build --target run-mps2-heartbeat
```

The first target I ran compiled the firmware, and the linker immediately reported the memory usage:

```text
Memory region         Used Size  Region Size  %age Used
            CODE:        3928 B         4 MB      0.09%
             RAM:        2588 B         4 MB      0.06%
size report: demo-heartbeat (mps2)
   text	   data	    bss	    dec	    hex
   3440	    488	   2100	   6028	   178c
```

4MB of land, the firmware used less than 4KB, the numbers gave us enough courage. The bluepill's heartbeat reported flash 3812 and RAM 2460 in the CI's size check, and the mps2 version's 3928 and 2588 are close in scale. However, I'll be honest, my build includes Release, and the CI's version doesn't. These two sets of numbers can only be used as references for scale, and we won't do byte-by-byte comparisons.

The second target connects the build and run together: after compiling, it directly enters QEMU, simulates for five seconds, and the output is directly passed to the terminal. Let's look at the real serial port:

```text
ZerOS demo: heartbeat (mps2)
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
qemu-system-arm: terminating on signal 15 from pid 173443 (timeout)
-- (timeout - normal exit for a resident demo)
[100%] Built target run-mps2-heartbeat
```

In QEMU's serial port, the heartbeat is exactly the same as on the bluepill board. The first line of the startup banner, then the two tasks take turns speaking, and ten pairs are said in five seconds. We've counted the rhythm: the two tasks have the same priority (prio 1), each sleeps for 500ms, and after speaking, the next one takes over. The interval between each line is 500ms, and ten pairs are made in five seconds. The end is signaled by timeout's signal 15, and the script's 124 code didn't have any unexpected branches. CMake reported a normal completion, and the overall exit code of the target fell on 0.

We're also worth checking the startup ceremony. The `demo_setup.hpp` setup is the same as bluepill's, and the comment clearly states the only differences are the UART number and the main frequency:

```cpp
inline void setup(const char* name) {
    board::clock_init();  // 固定 25MHz,空操作——仪式同形
    board::uart0_init();
    board::register_uart_channel(); // this board talks over UART0
    log::print("\r\nZerOS demo: {}\r\n", name);
    arch::cortex_m3::install_default_testaments(); // obligations, explicitly
    arch::cortex_m3::init_time(25'000'000 / 1000);  // 1kHz tick @25MHz
}
```

On the bluepill side, the `clock_init` needs to climb the clock tree, and we have to manually configure each step of HSE into PLL, and 72MHz is configured step by step. On this QEMU, the power-on is 25MHz, and `clock_init` left an empty operation. The shape of the startup ceremony remains unchanged, but the content is reduced to a single line. The calculation of the time base follows the main frequency. We calculate 25,000,000 divided by 1000, one beat per millisecond, the same division as on the 72MHz side, but the numbers are different. The heartbeat's 500ms comes from here.

## Line-by-Line Diff: What's the Difference Between the Two main.cpp Files

The final confrontation is at the application layer. We've taken one version of the heartbeat `main.cpp` from the bluepill and one from the mps2, and we've put them together for a diff:

```diff
--- src/board/stm32f103_bluepill/example/heartbeat/main.cpp
+++ src/board/mps2_an385/example/heartbeat/main.cpp
@@ -1,7 +1,8 @@
 /**
  * @file main.cpp
  * @author CharlieChen114514 (725610365@qq.com)
- * @brief All right, your bootstrap example
+ * @brief   All right, your bootstrap example — mps2_an385/QEMU port
+ *          (与 bluepill/heartbeat 同源零改:demo_setup 同名同形)
  * @version 0.1
  * @date 2026-09-06
  *
@@ -32,7 +33,7 @@
 }

 int main() {
-    ZerOS::demo::setup("heartbeat");
+    ZerOS::demo::setup("heartbeat (mps2)");

     auto shape = ZerOS::task::named("HeartBeat").prio(1).words(64);
```

The result of the diff is only the Doxygen comment at the file header, the name string of `setup()`, and a newline added at the end of the file. The rest of the body is exactly the same: two `HeartBeatPackage` packages, the heartbeat task body, the task factory `task::named("HeartBeat").prio(1).words(64)`, two `launch` calls, and `start_scheduler`, all are still the same as before. The self-evaluation in the comments says zero changes, and we've verified it with the diff, proving that the self-evaluation didn't exaggerate. We've changed a board, changed a type of serial port, changed the memory map, and the application code didn't change a single line. This sentence was our expectation for a thin port before, and now it's a diff we've held in our hands.

## Acceptance

Let's summarize the verifiable facts of this article, totaling three, and we'll go through them one by one.

The first is the reproduction path. You check out to `8efe93a` (or follow our old method to use `git archive` to export the tree), and you follow the three commands above. Your Memory region should report CODE 3928 and RAM 2588 (the compiler version will affect the decimal, my numbers are from 16.2.0, please remember this). The end of `run-mps2-heartbeat` should be signal 15's timeout, the log should carry the signal " (timeout - normal exit for a resident demo)", the overall exit code of the target should be 0, and the serial port should show the banner plus ten pairs of lines.

The second is the diff confrontation. We've put the two `main.cpp` files together for a diff, and you should only get the comments at the file header, the name of `setup()`, and the newline at the end. If any other line changes appear, it shouldn't be counted within the zero-change range.

The third is unverified, and I'll be honest with you. The mapping's source is from QEMU 11.0.3's info mtree, but I have 11.1.2 on my machine, and I don't have the older version installed. I can't check if there are any differences between the two versions of the mapping on the spot. I haven't run the GitHub Actions cloud CI, and I've only verified the local part. There is a real-world development board corresponding to mps2-an385, but we haven't touched it. The CMSDK UART register behavior has only been verified in QEMU, and the performance on the real board is not part of our current verification. On the timing, I've only counted the number of pairs and the rhythm (ten pairs in five seconds), and we haven't measured how accurate the virtual time of QEMU is.

## Next Article

The second board is now standing, and the four exam grids for QEMU have been filled. The next commit to be addressed is `fb9072b`, with the subject "feat: stack watermark, tickless", a commit that includes two features, which we'll split into two articles as usual: the stack canary is upgraded to full stack painting, the water level report answers how much stack each task uses, and the tickless idle changes the heartbeat to one-shot, answering how many interrupts can be saved when sleeping. We've counted the heartbeat on this new board, and in the next article, we'll learn how to slow it down. See you there.
