---
chapter: 14
difficulty: beginner
order: 5
platform: stm32f1
reading_time_minutes: 23
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 5: Advanced Debugging — From printf to a Complete GDB Environment'
description: ''
translation:
  source: documents/vol8-domains/embedded/00-env-setup/05-debugging-guide.md
  source_hash: 7bb11967418ba54bf3a3ff193b65a843608db096d64ca98b7336fe3224a11116
  translated_at: '2026-06-16T04:09:16.149518+00:00'
  engine: anthropic
  token_count: 3200
---
# Part 5: Advanced Debugging — From printf to a Complete GDB Environment

> Written for everyone still debugging STM32 programs with printf and wondering "why can't I single-step like a normal program?"
> This post records the entire process of building a complete debugging environment from scratch, including GDB Server principles, command-line debugging in action, VSCode graphical configuration, and how to troubleshoot those maddening debugging issues.

---

## Why I Insist on Writing This Debugging Post

Think back to when you wrote a standard C++ program and wanted to know why a variable's value was wrong. What did you do? You simply set a breakpoint in the IDE, pressed F5 to run, the program stopped there, you hovered your mouse over the variable to see its value, and single-stepped a few times to locate the problem. You've done this workflow thousands of times; it doesn't even require conscious thought.

But when you switch to STM32 development, the world suddenly changes. Your code doesn't run on your computer; it runs on that cheap board. You can't directly "run" it; you can only flash the compiled binary into the Flash memory. Once the program is running, the only feedback you can see is the blinking state of a few LEDs, or, if you are lucky, some characters printed via serial port. At this point, if you want to know the value of a variable, you have to add a printf, recompile, flash, and observe the result. This workflow is slow enough to drive you crazy.

Worse yet, printf debugging has serious limitations in embedded environments. First, it requires serial port resources. What if all your UARTs are already used for communication? Second, printf consumes code space and time. Timing-sensitive code might stop working just because you added a printf. Most fatally, some bugs only appear under specific conditions. After you add printf, the timing changes, and the bug disappears—this is a classic "Heisenbug."

When I was first tinkering with STM32, I relied on this primitive method. Every time I changed a little code, I reflashed and stared at the serial output for ages. Once, a bug in an ISR (interrupt service routine) had me adding a dozen print statements and flashing twenty-plus times, only to find it was an incorrect interrupt priority setting. With a complete debugging environment, I only needed to set a breakpoint in the ISR and glance at the call stack to locate the problem.

So, in this post, I will guide you through building a complete debugging environment that allows you to debug STM32 just like a normal program: setting breakpoints, single-stepping, viewing variables, monitoring registers, and even directly modifying memory values. Once this environment is up and running, your development efficiency will improve by an order of magnitude.

---

## Understand This First: Why Can't We Debug Directly?

Before we start, we need to understand a core question: Why can't STM32 programs be debugged directly like normal programs?

When you debug a normal x86 program, GDB and the target program run on the same machine. They communicate via debugging interfaces (like ptrace) provided by the operating system. The OS knows everything about the process: memory layout, register state, call stack. GDB just needs to ask the OS for this information.

But the situation for STM32 is completely different. Your program runs on a separate chip; its CPU, memory, and peripherals are physically isolated from your development machine. GDB cannot directly access these resources; it needs a "middleman" to help. This middleman is the debug probe, such as the ST-Link V2.

The debug probe communicates with the STM32 via the SWD (Serial Wire Debug) protocol. SWD is a protocol designed by ARM specifically for debugging. It requires only two wires (SWDIO and SWCLK) to implement full debugging features: reading/writing memory, setting breakpoints, single-stepping, and viewing registers. Inside the ST-Link is a dedicated chip that communicates with your computer via USB on one side and with the STM32 via SWD on the other, acting as a "translator."

But we're not done yet. ST-Link is just a hardware-level bridge. We also need software to drive it and "translate" GDB debugging commands into the SWD protocol. This software is OpenOCD (Open On-Chip Debugger). OpenOCD can run in two modes: one is a direct command mode used for flashing firmware; the other is GDB Server mode, which listens on a TCP port waiting for a GDB connection.

When you start OpenOCD's GDB Server, the complete debugging chain looks like this: GDB (client) connects via TCP to OpenOCD (server), OpenOCD communicates via USB with ST-Link, and ST-Link communicates via SWD with STM32. Every link in this chain is indispensable; if any link fails, debugging cannot proceed.

Understanding this architecture, you will know why debugging requires so many steps and where to start troubleshooting when problems occur. By default, OpenOCD listens for GDB connections on port localhost:3333, while simultaneously providing a Telnet console on localhost:4444 (used to execute OpenOCD commands like manual halt or resume).

---

## Start with the Command Line: GDB Debugging in Action

Before configuring the graphical interface, I strongly recommend running through the complete debugging process via command line first. This has two benefits: first, you understand the underlying principles and know what the GUI is actually doing behind the scenes; second, when the GUI has issues, you can use the command line to quickly locate whether it's a configuration problem or an environment problem.

First, start the OpenOCD server. Open a terminal, enter your project directory, and execute:

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg
```

The meaning of this command is: use `stlink.cfg` as the interface configuration (telling OpenOCD we are using ST-Link), and use `stm32f1x.cfg` as the target configuration (telling OpenOCD we are debugging an STM32F1 series chip). If everything goes well, you will see output similar to this:

```text
Open On-Chip Debugger 0.11.0
Licensed under GNU GPL v2
...
Info : stm32f1x.cpu: hardware has 6 breakpoints, 4 watchpoints
```

The last line tells us the GDB server is ready at port 3333. Keep this terminal running; do not close it.

Next, open another terminal and start GDB to connect to OpenOCD:

```bash
arm-none-eabi-gdb build/firmware.elf
```

Here we are using the ARM version of GDB (`arm-none-eabi-gdb`), not the standard GDB that comes with the system. The parameter is the ELF file we compiled, which contains debug symbol information, so GDB can know source code line numbers and variable names.

After entering the GDB command line, you will see the `(gdb)` prompt. Now execute the following commands in order:

```text
target remote localhost:3333
```

This command tells GDB to connect to local port 3333, which is the OpenOCD GDB server. If the connection is successful, you will see a prompt like "Remote debugging using localhost:3333".

```text
load
```

This command flashes the code and data segments from the ELF file into the STM32's Flash and RAM. You will see a progress bar and output like "Transfer rate XXX KB/s". If this reports an error "target not halted", it means the chip is still running, and you need to execute the `monitor halt` command first to stop the chip.

```text
break main
```

Set a breakpoint at the entry of the `main` function. GDB will reply "Breakpoint 1 at 0x...", telling you the breakpoint was set successfully and its address.

```text
continue
```

Let the program continue running. The program will immediately stop at the breakpoint in the `main` function, and you will see output similar to this:

```text
Continuing.
Breakpoint 1, main () at src/main.cpp:10
10      {
```

Now the program has stopped at the first line of `main`. You can start single-stepping. The `step` (or `s`) command will enter inside the function (if the current line is a function call), while the `next` (or `n`) command will execute the current line and stop at the next line (without entering the function). My personal habit is to rely mainly on `next`, only using `step` when I really need to enter a function to see details.

Use the `print` command (or `p`) to view variables:

```text
print counter
```

If the variable is a basic type, GDB will directly display its value. If it is an array or struct, GDB will display the complete structure. You can also use `/x` to display in hexadecimal, or `/t` to display in binary.

Use `info registers` to view register status:

```text
info registers
```

This displays the current values of all general-purpose registers (r0-r12), sp, lr, pc, and special registers (xPSR). In embedded debugging, sometimes you need to view the value of a specific peripheral register, for example, to know the current state of GPIOC's ODR (Output Data Register), you can directly use the `x` command to view memory:

```text
x/1wx 0x4001080C
```

The meaning of `x/1wx` is: display a word (w, 4 bytes) size memory content in hexadecimal (x). `0x4001080C` is the address of GPIOC's ODR register (this address needs to be checked in the reference manual). GDB will output a result like `0x4001080c: 0x00002000`, indicating the current value of this register is `0x2000`, meaning bit 13 is set (GPIOC Pin 13 is the onboard LED).

If you want to directly modify a variable or memory value, use the `set` command:

```text
set variable counter = 1000
```

This is very useful when testing certain boundary conditions. For example, if you want to verify the program's behavior when a counter overflows, you can set it directly to a value near the overflow instead of single-stepping hundreds of times like a fool.

When you are done debugging and want to exit, use the `quit` command (or `q`). If the chip is still running, GDB will ask if you want to stop it; select yes.

---

## Alright, Now Let's Move It Into VSCode

Command-line debugging is indeed cool and makes you look like an old-school hacker, but honestly, in daily development, I still prefer a graphical interface. Being able to see source code, variable lists, call stacks, and setting breakpoints by clicking—these conveniences can't be replaced by nostalgia.

To debug STM32 on VSCode, you need to install an extension: **Cortex-Debug**. It is a debugging plugin designed specifically for ARM Cortex chips, supporting OpenOCD, J-Link, ST-Link, and other debuggers. After installation, we need to create a `launch.json` file to configure debugging behavior.

Let me give you a complete configuration first, then explain it line by line:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "STM32 Debug",
            "cwd": "${workspaceRoot}",
            "executable": "build/firmware.elf",
            "request": "launch",
            "type": "cortex-debug",
            "servertype": "openocd",
            "device": "STM32F103C8T6",
            "interface": "swd",
            "openocdPath": "openocd",
            "configFiles": [
                "interface/stlink.cfg",
                "target/stm32f1x.cfg"
            ],
            "searchDir": [
                "/usr/share/openocd/scripts"
            ]
        }
    ]
}
```

The `name` field is the configuration name you see in the VSCode debug panel; you can change it to whatever you like, just pick one you can remember. `type` must be `"cortex-debug"`, which tells VSCode which plugin to use for this configuration. `request` uses `"launch"` to indicate we are starting debugging (if you already have a running OpenOCD server, you can also use `"attach"` mode).

`servertype` specifies the GDB server type we are using; here fill in `"openocd"`. If you use J-Link, you can change it to `"jlink"`, but the corresponding configuration will also be different. `cwd` is the current working directory, using the `${workspaceRoot}` variable will automatically set it to your project root.

`executable` is the most important item; it points to your compiled ELF file. Note that here you must use ELF, not bin, because ELF contains debug symbols, while bin is just pure binary. The path can be a relative path (relative to workspaceRoot) or an absolute path.

`openocdPath` specifies the full path to the OpenOCD executable. On Ubuntu and Arch, OpenOCD is usually installed at `/usr/bin/openocd`, but if you installed it manually elsewhere, you need to modify this accordingly. The Cortex-Debug extension will automatically start this OpenOCD instance, so you don't need to start it manually yourself.

The `configFiles` array specifies OpenOCD's configuration files. The paths to these two files are relative to `searchDir`. `interface/stlink.cfg` tells OpenOCD we are using the ST-Link debugger, and `target/stm32f1x.cfg` tells it the target chip is the STM32F1 series. These configuration files come with OpenOCD and are located in the `/usr/share/openocd/scripts` directory (this is the path for most Linux distributions).

`searchDir` is that script directory I just mentioned. Cortex-Debug needs to know where to find those `.cfg` files, so specify OpenOCD's script directory here. If OpenOCD is installed elsewhere on your system (for example, compiled from source and installed to `/usr/local/`), you might need to change this to `/usr/local/share/openocd/scripts`.

`device` specifies the specific chip model. This information is mainly used by Cortex-Debug to display the correct register definitions and peripheral information. Filling in `"STM32F103C8T6"` will cover our Blue Pill board.

`interface` specifies the debug interface type; on STM32 it is generally `"swd"` (Serial Wire Debug), requiring only two wires. Older debuggers might use `"jtag"`, but that is rare now. `svdPath` is used to specify a specific debugger (if you have multiple ST-Links connected at the same time), but in most cases, leave it empty.

Once configuration is complete, return to the main VSCode interface, press F5 or click the "Run and Debug" panel on the left, select "STM32 Debug", and debugging will start. You will see OpenOCD startup information in the "Debug Console" at the bottom, and then the program will stop at the `main` function.

---

## Complete Debugging Workflow: Verify Everything is Ready

Now that we have the configuration, it's time to verify that the whole process actually works. I will walk you through a complete debugging workflow to ensure every step works as expected.

First, ensure your STM32 board is connected to the computer via ST-Link, and that OpenOCD has permission to access the USB device (WSL users remember to use `usbipd attach` to forward). Then press F5 in VSCode to start debugging.

If all goes well, you should see output similar to this in the debug console:

```text
Info : Unable to match requested speed 1000 kHz, using 975 kHz
Info : Unable to match requested speed 1000 kHz, using 975 kHz
Info : clock speed 975 kHz
Info : ST-Link V2 JTAG/SWD API v2
Info : stm32f1x.cpu: hardware has 6 breakpoints, 4 watchpoints
```

The last line tells you the chip supports 6 hardware breakpoints and 4 watchpoints, which is the standard configuration for Cortex-M3. A few seconds later, the editor will automatically jump to the first line of the `main` function, and a yellow arrow on the left indicates the current execution position.

Now try single-stepping. Pressing F10 (Step Over) will execute the current line and stop at the next line. If the first line of your `main` function is `HAL_Init()`, after pressing F10, the yellow arrow will move to the next line but will not enter inside the `HAL_Init` function. If you want to enter inside the function, press F11 (Step Into).

The "VARIABLES" panel on the left will automatically display all local variables within the current scope and their values. if a variable displays `<optimized out>`, it means the compiler optimized it away. You need to change the optimization level in CMakeLists.txt to `-Og` or `-O0` (debug optimization).

In the "WATCH" panel, you can manually enter expressions you want to monitor. For example, entering `*GPIOC` allows you to see all register values of the GPIOC peripheral; entering `SystemCoreClock` shows the current system clock frequency. This is very useful when debugging clock configuration.

Now let's try a real-world scenario: monitoring GPIO registers. Suppose your program is blinking an LED, and you want to know when the GPIOC ODR register changes. Enter `*(uint32_t*)0x4001080C` in the "WATCH" panel (this is the address of the ODR register), then press F5 (Continue) to let the program run. You will find the monitored value changes as the LED state changes, from `0x2000` to `0x0000` and back.

If you want to directly modify a variable's value to test a condition, you can right-click the variable in the "VARIABLES" panel and select "Set Value", or enter a GDB command in the "DEBUG CONSOLE":

```text
-exec set variable counter = 500
```

The `-exec` prefix tells VSCode to pass the content that follows to GDB for execution. This trick is particularly useful when you want to test boundary conditions.

During debugging, you might want to view the call stack. For example, if the program stops in an ISR and you want to know where it was triggered from. The "CALL STACK" panel on the left will show the complete call chain, from the current function all the way back to `Reset_Handler`. Clicking any layer will jump the editor to the corresponding source code location, and the context variables will also switch to that layer.

When you are done debugging, press Shift+F5 to stop debugging. VSCode will automatically close the OpenOCD server and disconnect from the ST-Link. At this point, your debugging environment is fully verified. From compilation and flashing to debugging, the entire toolchain is ready, and you can start focusing on writing code instead of being plagued by environment issues.

---

## Advanced Debugging Techniques: Hardware Breakpoints and Memory Viewing

The content above covers 90% of daily debugging needs, but sometimes you will encounter trickier situations that require some advanced techniques.

The first thing to discuss is hardware breakpoints vs. software breakpoints. You may have heard that Cortex-M3 only supports 6 hardware breakpoints, but software breakpoints can be set infinitely. What's the difference? Software breakpoints are implemented by writing a special instruction (BKPT) at the target address. When the CPU executes this instruction, it triggers a debug exception. However, Flash is read-only memory; you cannot modify its contents at runtime, so software breakpoints can only be used for code running in RAM. Hardware breakpoints are implemented through the CPU's internal comparison circuitry and do not require modifying code, so they can be set anywhere in Flash, but the quantity is limited by hardware (6 for Cortex-M3).

In practice, this means when you set a 7th breakpoint, GDB will report "cannot set breakpoint" or the breakpoint simply won't take effect. There are two solutions: one is to delete unnecessary breakpoints, keeping active breakpoints to 6 or fewer; the other is to run a segment of code in RAM (for example, copying a frequently debugged function to RAM for execution), so you can use software breakpoints.

In GDB, you can use `info breakpoints` to view the status of all current breakpoints:

```text
info breakpoints
```

Pay attention to the `y`/`n` column. If it displays `hw`, it means a hardware breakpoint is used; `sw` means a software breakpoint.

The second advanced technique is memory viewing. Sometimes you want to view a large contiguous area of memory, such as an entire DMA buffer or an array of structs. The `x` command can achieve this:

```text
x/10wx 0x20000000
```

This command displays the contents of 10 words (4 bytes each) starting from `0x20000000` in hexadecimal. `x/10gx` can display 64-bit integers (8 bytes), which is useful when viewing double-precision floating-point arrays.

In VSCode, you can enter the array name in the "WATCH" panel to view array contents, but if you want to view raw memory, execute this in the "DEBUG CONSOLE":

```text
-exec x/32xb 0x20000000
```

This displays 32 bytes of memory content in bytes (`b` stands for byte). This is very useful when debugging memory alignment issues or DMA transfer issues.

The third technique is about RTOS debugging. If you use an RTOS like FreeRTOS, you will find the call stack filled with functions like `vTaskSwitch` or `xPortPendSVHandler`, making it hard to find the real entry point of the current task. The Cortex-Debug extension supports RTOS-aware debugging, but it requires extra configuration. Add `rtos: "FreeRTOS"` to `launch.json`:

```json
"rtos": "FreeRTOS"
```

After configuration, the debug panel will display a "Threads" dropdown listing all currently created tasks. You can switch between different tasks just like debugging a multi-threaded program.

The last technique to mention is SWO (Serial Wire Output). SWO is a feature of ARM Cortex-M that can output debug information via a high-speed channel on the SWD interface. It does not occupy UART resources and is much faster than printf. However, SWO configuration is relatively complex, requiring setting baud rates, configuring the TRACETCK pin, and not all ST-Links support it (ST-Link V2 does). This content is quite independent, and I plan to write a separate post about it later.

---

## Troubleshooting Common Debugging Issues

Even if you follow the steps above one by one, you will inevitably encounter all sorts of strange problems. The debugging environment involves many links, and if any one place fails, debugging fails. I have organized the pits I've stepped into by symptom to help you quickly locate them.

The most common problem is `target not halted`. This error usually occurs when you execute the `load` command. The reason is that OpenOCD cannot flash the chip while it is running. The solution is to execute `monitor halt` before loading:

```text
monitor halt
load
```

The `monitor` prefix tells GDB to pass the following command to OpenOCD rather than executing it itself. The `halt` command stops the CPU and enters debug mode. If `halt` also reports an error, the chip might be in a low-power mode and needs more time to wake up, or the SWD connection is unstable.

The second common error is `timeout waiting for target halted`. I was also baffled when I encountered this error. Finally, checking the data revealed that the chip was in Sleep or Stop Mode, and the debugger could not wake it up normally. The solution is to disable debugger sleep before entering low-power mode, or press the reset button to force the chip to exit low-power state.

The third situation is that the breakpoint is set but the program doesn't stop there. There are several possible reasons. One is that you indeed exceeded the hardware breakpoint limit (6); try deleting a few useless breakpoints. Two, the code might not have been loaded to that address at all; check the output of the `load` command to ensure it was written to the correct Flash area. Three, the code was optimized away; the optimizer might have deleted the code where you set the breakpoint entirely. Try changing the compilation optimization to `-O0`.

The fourth problem is that variables display `<optimized out>` or the displayed values are obviously wrong. This is almost always caused by compiler optimization. In your debug build, you should use `-Og` (mode specifically optimized for debugging) or `-O0` (completely disable optimization), not `-O2` or `-O3`. In CMakeLists.txt, you can set the optimization level separately for the Debug configuration:

```cmake
set(CMAKE_CXX_FLAGS_DEBUG "-Og -g")
```

There is also the case of variables in inline functions. Because the code is inlined, the original "local variable" might have been optimized into a register or disappeared entirely, and GDB cannot track it. In this case, you can use `__attribute__((noinline))` to prevent inlining, or simply set a breakpoint at a higher level.

The fifth problem is that VSCode cannot connect to OpenOCD. The error message might be "Failed to connect to GDB" or "Could not connect to localhost:3333". First confirm that OpenOCD is not running elsewhere (for example, an instance you manually started earlier hasn't been closed), then use `netstat` to check if the port is occupied. If the port is occupied, either kill the occupying process or use a different port in `gdbPort` (but OpenOCD defaults to 3333, changing ports requires extra configuration and is not recommended).

If OpenOCD didn't start at all, check `openocdPath`. Execute `openocd` directly in a terminal. If the command doesn't exist, it means OpenOCD isn't installed or is installed elsewhere. Use `which openocd` to find the correct path, then update `openocdPath`.

WSL users also have a special issue: USB permissions. The error message is usually `Error: open failed` or `Libusb error`. First confirm that ST-Link has been forwarded to WSL by `usbipd` (`lsusb` should see the device), then use the script I mentioned earlier to fix permissions:

```bash
sudo ./fix-usb.sh
```

The last resort is to view OpenOCD's detailed logs. Add `debug_level: 3` to `configFiles`:

```text
debug_level 3
```

This causes OpenOCD to output the most detailed debugging information. Although you might not understand most of it, at least you'll know where it got stuck. You can also manually start OpenOCD in a terminal and observe the output; many error messages only appear there.

---

## And We're Done

If you've followed the previous posts all the way here, you should now possess a complete STM32 development toolchain: a cross-compiler, CMake build system, HAL library, OpenOCD flashing tool, and the GDB debugging environment we just configured. From compilation and flashing to debugging, the entire workflow can be completed under Linux, no longer relying on Windows-exclusive IDEs like Keil.

When you press F5 in VSCode for the first time, watching the program stop at the `main` function breakpoint, then single-stepping a few lines, modifying a variable's value, and watching the LED change its blinking frequency accordingly, that sense of control is unmatched. You are no longer blindly flashing, guessing, and flashing again; you can precisely observe every step of the program's execution. This is the experience embedded development should have.

Migrating from Keil to this toolchain, besides the cross-platform advantage, has many tangible benefits. You can write code with Vim/Neovim, use clangd to get code completion more powerful than any commercial IDE, use Git to manage versions (no more dealing with weird project files), and use CTest to run automated tests. More importantly, this toolchain is completely open source and fully customizable. When you encounter problems, you can read the source code and modify configurations instead of being trapped in a black box.

Next, we can finally start discussing the application of modern C++ in embedded systems. How do C++ features like templates, RAII, lambda expressions, and constexpr play a role on the resource-constrained STM32? How to write embedded code that is both modern and efficient? This is the true core of this tutorial. The previous toolchain setup was just preparation. But now, with this toolchain, we can focus on the code itself without being distracted by environment issues.
