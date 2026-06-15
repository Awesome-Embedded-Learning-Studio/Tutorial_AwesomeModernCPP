---
chapter: 14
difficulty: beginner
order: 5
platform: stm32f1
reading_time_minutes: 24
tags:
- beginner
- cpp-modern
- stm32f1
title: 'Part 5: Advanced Debugging — From `printf` to a Complete GDB (GNU Debugger)
  Environment'
translation:
  engine: anthropic
  source: documents/vol8-domains/embedded/00-env-setup/05-debugging-guide.md
  source_hash: c360f75e524dcf5c92f15a767476d520e72f348f144be853cb12fe34781b5b66
  token_count: 3194
  translated_at: '2026-05-26T12:01:18.276291+00:00'
description: ''
---
# Part 5: Advanced Debugging — From printf to a Complete GDB Environment

> For everyone still debugging STM32 programs with printf, wondering "why can't I just single-step like a normal program."
> This article documents our process of building a complete debugging environment from scratch, including GDB Server principles, hands-on command-line debugging, VSCode graphical configuration, and how to troubleshoot those maddening debug issues.

---

## Why I Had to Write This Debugging Article

Think back to when you write a regular C++ program and want to know why a variable has the wrong value. What do you do? You simply set a breakpoint in your IDE, press F5 to run, the program stops there, you hover over the variable to see its value, and a few single-steps later you've located the problem. You've done this thousands of times; it requires zero conscious thought.

But when you switch to STM32 development, the world suddenly changes. Your code doesn't run on your computer; it runs on that cheap little board. You can't just "run" it — you have to flash the compiled binary into Flash. Once the program is running, the only feedback you can see is the blinking state of a few LEDs, or, if you're lucky, some characters printed over a serial port. If you want to know a variable's value at this point, your only option is to add a printf, recompile, flash, and observe the result. This workflow is maddeningly slow.

Worse still, printf debugging has severe limitations in embedded environments. First, it requires a serial port resource — what if all your UARTs are already used for communication? Second, printf consumes code space and time; timing-sensitive code might simply stop working once you add a printf. The most fatal issue is that some bugs only appear under specific conditions. Once you add a printf, the timing changes and the bug disappears — a classic "Heisenbug."

When I first started tinkering with STM32, I relied on this primitive approach. Every time I changed a little code, I'd reflash and stare at the serial output for ages. Once, I had a bug in an interrupt service routine (ISR). I added over a dozen print statements, flashed more than twenty times, and finally discovered it was an incorrect interrupt priority setting. With a complete debugging environment, I would have just needed to set a breakpoint in the ISR, glance at the call stack, and locate the problem.

So in this article, I'm going to walk you through setting up a complete debugging environment that lets you debug an STM32 just like a normal program: setting breakpoints, single-stepping, viewing variables, watching registers, and even directly modifying values in memory. Once this environment is up and running, your development efficiency will increase by an order of magnitude.

---

## Let's Clear This Up First: Why Can't We Debug Directly?

Before we get our hands dirty, we need to understand a core question: why can't STM32 programs be debugged directly like normal programs?

When you debug a normal x86 program, GDB and the debugged program run on the same machine, communicating through debugging interfaces provided by the operating system (ptrace). The operating system knows everything about the process: memory layout, register states, call stacks. GDB simply asks the OS for this information.

But the STM32 situation is completely different. Your program runs on an independent chip; its CPU, memory, and peripherals are physically isolated from your development machine. GDB cannot access these resources directly and needs a "middleman" to help. This middleman is the debug probe, such as the ST-Link V2.

The debug probe communicates with the STM32 via SWD (Serial Wire Debug). SWD is a protocol designed by ARM specifically for debugging, requiring only two wires (SWDIO and SWCLK) to implement full debugging capabilities: reading and writing memory, setting breakpoints, single-stepping, and viewing registers. Inside the ST-Link is a dedicated chip that communicates with your computer via USB on one side and with the STM32 via SWD on the other, acting as a "translator."

But that's not the end of it. The ST-Link is only a hardware-level bridge; we also need software to drive it and "translate" GDB's debug commands into the SWD protocol. This software is OpenOCD (Open On-Chip Debugger). OpenOCD can run in two modes: one is a direct command mode used for flashing firmware; the other is GDB Server mode, which listens on a TCP port waiting for a GDB connection.

When you start OpenOCD's GDB Server, the complete debugging chain looks like this: GDB (client) connects to OpenOCD (server) via TCP, OpenOCD communicates with the ST-Link via USB, and the ST-Link communicates with the STM32 via SWD. Every link in this chain is indispensable; if any single link fails, debugging cannot proceed.

Once you understand this architecture, you'll know why debugging requires so many steps, and you'll know which link to investigate when something goes wrong. By default, OpenOCD listens for GDB connections on localhost:3333, while simultaneously providing a Telnet console on localhost:4444 (which can be used to execute OpenOCD commands, such as manually halting or resuming).

---

## Starting from the Command Line: Hands-On GDB Debugging

Before configuring a graphical interface, I strongly recommend running through the complete debugging workflow from the command line first. There are two benefits to this: first, you understand the underlying principles and know what the graphical interface is actually doing behind the scenes; second, when the graphical interface has issues, you can use the command line to quickly determine whether it's a configuration problem or an environment problem.

First, start the OpenOCD server. Open a terminal, navigate to your project directory, and run:

```bash
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg
```

This command means: use stlink.cfg as the interface configuration (telling OpenOCD we are using an ST-Link), and use stm32f1x.cfg as the target configuration (telling OpenOCD we want to debug an STM32F1 series chip). If everything is fine, you'll see output similar to this:

```text
Open On-Chip Debugger 0.12.0
Licensed under GNU GPL v2
For bug reports, read
    http://openocd.org/doc/doxygen/bugs.html
Info : Listening on port 6666 for tcl connections
Info : Listening on port 4444 for telnet connections
Info : Listening on port 3333 for gdb connections
```

The last line tells us the GDB server is ready on port 3333. Keep this terminal running; do not close it.

Next, open another terminal, start GDB, and connect to OpenOCD:

```bash
arm-none-eabi-gdb build/stm32_demo.elf
```

Here we are using the ARM version of GDB (arm-none-eabi-gdb), not the regular GDB that comes with the system. The argument is our compiled ELF (Executable and Linkable Format) file, which contains debug symbol information, so GDB can know source code line numbers and variable names.

After entering the GDB command line, you'll see the `(gdb)` prompt. Now execute the following commands in order:

```text
(gdb) target remote localhost:3333
```

This command tells GDB to connect to the local port 3333, which is the OpenOCD GDB server. If the connection is successful, you'll see a prompt like "Remote debugging using localhost:3333".

```text
(gdb) load
```

This command flashes the code and data sections from the ELF file into the STM32's Flash and RAM. You'll see a progress bar and "Transfer rate XXX KB/s" output. If you get a "target not halted" error here, it means the chip is still running, and you need to execute the `monitor halt` command first to stop the chip.

```text
(gdb) break main
```

Set a breakpoint at the entry of the main function. GDB will reply "Breakpoint 1 at 0x...", telling you the breakpoint was set successfully and its address.

```text
(gdb) continue
```

Let the program continue running. The program will immediately stop at the breakpoint in the main function, and you'll see output like this:

```text
Continuing.

Breakpoint 1, main () at main.cpp:42
42        HAL_Init();
```

Now the program has stopped at the first line of the main function, and you can start single-stepping. The `step` command steps into a function (if the current line is a function call), while the `next` command executes the current line and stops at the next line (without entering the function). My personal habit is to primarily use `next`, only using `step` when I truly need to step into a function to see the details.

To view variables, use the `print` command:

```text
(gdb) print counter
```

If the variable is a basic type, GDB will display its value directly. If it's an array or struct, GDB will display the complete structure. You can also use `print/x` to display in hexadecimal, or `print/t` to display in binary.

To view register states, use `info registers`:

```text
(gdb) info registers
```

This displays the current values of all general-purpose registers (r0-r12), sp, lr, pc, and special registers (xPSR). In embedded debugging, sometimes you need to view the value of a specific peripheral register. For example, if you want to know the current state of GPIOC's ODR (Output Data Register), you can directly use the `x` command to view memory:

```text
(gdb) x/wx 0x4001080C
```

The meaning of `x/wx` is: display memory contents of one word (w, 4 bytes) in hexadecimal (x). 0x4001080C is the address of GPIOC's ODR register (you need to check the reference manual for this address). GDB will output a result like `0x4001080c:    0x00002000`, indicating the current value of this register is 0x2000, meaning bit 13 is set (GPIOC Pin 13 is the onboard LED).

If you want to directly modify a variable or memory value, you can use the `set` command:

```text
(gdb) set var counter = 100
```

This is extremely useful when testing certain boundary conditions. For example, if you want to verify the program's behavior when a counter overflows, you can directly set it to a value near overflow instead of mindlessly single-stepping hundreds of times.

When you're done debugging and want to exit, use the `quit` command. If the chip is still running, GDB will ask whether you want to stop it; just choose yes.

---

## Alright, Now Let's Move It into VSCode

Command-line debugging is indeed cool and makes you look like an old-school hacker, but honestly, in daily development I still prefer a graphical interface. Being able to see source code, variable lists, and call stacks, and being able to set breakpoints with a simple click — these conveniences can't be replaced by nostalgia.

To debug STM32 in VSCode, you need to install a plugin: Cortex-Debug. It's a debugging plugin designed specifically for ARM Cortex chips, supporting multiple debuggers including OpenOCD, J-Link, and ST-Link. After installation, we need to create a `.vscode/launch.json` file to configure the debugging behavior.

Let me give you a complete configuration first, and then explain it line by line:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "STM32 Debug",
            "type": "cortex-debug",
            "request": "launch",
            "servertype": "openocd",
            "cwd": "${workspaceRoot}",
            "executable": "build/stm32_demo.elf",
            "serverpath": "/usr/bin/openocd",
            "configFiles": [
                "interface/stlink.cfg",
                "target/stm32f1x.cfg"
            ],
            "searchDir": ["/usr/share/openocd/scripts"],
            "runToEntryPoint": "main",
            "device": "STM32F103C8T6",
            "interface": "swd",
            "serialNumber": ""
        }
    ]
}
```

The `name` field is the configuration name you see in the VSCode debug panel; you can change it to whatever you want, just pick something you'll remember. `type` must be "cortex-debug", which tells VSCode which plugin to use for this configuration. `request` uses "launch" to indicate we want to start debugging (if you already have a running OpenOCD server, you can also use "attach" mode).

`servertype` specifies the type of GDB server we are using; here we fill in "openocd". If you're using J-Link, you can change it to "jlink", but the corresponding configuration will also be different. `cwd` is the current working directory; using the `${workspaceRoot}` variable will automatically set it to your project root.

`executable` is the most important item; it points to your compiled ELF file. Note that you must use ELF here, not bin, because ELF contains debug symbols, while bin is just pure binary. The path can be relative (relative to workspaceRoot) or absolute.

`serverpath` specifies the full path to the OpenOCD executable. On Ubuntu and Arch, OpenOCD is usually installed at `/usr/bin/openocd`, but if you manually installed it elsewhere, you need to modify this accordingly. The Cortex-Debug plugin will automatically start this OpenOCD instance, so you don't need to start it manually yourself.

The `configFiles` array specifies OpenOCD's configuration files. The paths of these two files are relative to `searchDir`. `interface/stlink.cfg` tells OpenOCD we are using the ST-Link debugger, and `target/stm32f1x.cfg` tells it the target chip is the STM32F1 series. These configuration files come with OpenOCD and are located in the `/usr/share/openocd/scripts` directory (this is the path on most Linux distributions).

`searchDir` is the script directory I just mentioned. Cortex-Debug needs to know where to find those `.cfg` files, so you must specify OpenOCD's script directory here. If OpenOCD is installed elsewhere on your system (for example, compiled from source and installed to `/usr/local`), you might need to change this to `/usr/local/share/openocd/scripts`.

`runToEntryPoint` is a very convenient option. When set to "main", debugging will automatically stop at the entry of the main function, saving you the trouble of manually setting a breakpoint. If you want to debug starting from the reset vector (for example, to see the startup code and system initialization process), you can delete this option, and the program will stop at `Reset_Handler`.

The `device` field specifies the exact chip model. This information is mainly used by Cortex-Debug to display the correct register definitions and peripheral information. Filling in "STM32F103C8T6" will cover our Blue Pill development board.

`interface` specifies the debug interface type; on STM32 it's generally "swd" (Serial Wire Debug), which only needs two wires. Older debuggers might use "jtag", but that's rare now. `serialNumber` is used to specify a particular debugger (if you have multiple ST-Links connected simultaneously); in most cases, you can leave it blank.

After the configuration is complete, return to the VSCode main interface, press F5, or click the "Run and Debug" panel on the left, select "STM32 Debug", and debugging will start. You'll see OpenOCD startup information in the "Debug Console" at the bottom, and then the program will stop at the main function.

---

## Complete Debugging Workflow: Verifying Everything Is Ready

Now that we have the configuration, it's time to verify whether the entire workflow actually works. I'll walk you through a complete debugging process to ensure every step works as expected.

First, make sure your STM32 board is connected to your computer via ST-Link, and that OpenOCD has permission to access the USB device (WSL users remember to use usbipd attach to forward it). Then press F5 in VSCode to start debugging.

If all goes well, you should see output similar to this in the debug console:

```text
Open On-Chip Debugger 0.12.0
Info : Listening on port 3333 for gdb connections
...
Info : stm32f1x.cpu: hardware has 6 breakpoints, 4 watchpoints
```

The last line tells you the chip supports six hardware breakpoints and four watchpoints, which is the standard configuration for Cortex-M3. A few seconds later, the editor will automatically jump to the first line of the main function, and a yellow arrow on the left will indicate the current execution position.

Now try single-stepping. Pressing F10 (Step Over) will execute the current line and stop at the next line. If the first line of your main function is `HAL_Init()`, after pressing F10 the yellow arrow will move to the next line, but it won't step into the HAL_Init function. If you want to step into the function, press F11 (Step Into).

The "Variables" panel on the left will automatically display all local variables in the current scope and their values. If a variable displays `<optimized out>`, it means the compiler optimized it away; you need to change the optimization level in CMakeLists.txt to `-O0` or `-Og` (debug optimization).

In the "Watch" panel, you can manually enter expressions you want to monitor. For example, entering `*GPIOC` will show all register values of the GPIOC peripheral; entering `SystemCoreClock` will show the current system clock frequency. This is very useful when debugging clock configurations.

Now let's try a real-world scenario: monitoring GPIO registers. Suppose your program is blinking an LED, and you want to know when GPIOC's ODR register changes. Enter `*(volatile uint32_t*)0x4001080C` (the address of the ODR register) in the "Watch" panel, then press F5 (Continue) to let the program run. You'll find the watched value changes with the LED state, going from 0x2000 to 0x0000 and back again.

If you want to directly modify a variable's value to test a certain condition, you can right-click the variable in the "Variables" panel and select "Set Value", or enter a GDB command in the "Debug Console":

```text
-exec set var counter = 1000
```

The `-exec` prefix tells VSCode to pass the following content to GDB for execution. This trick is especially useful when you want to test boundary conditions.

During debugging, you might want to view the call stack. For example, if the program stops in some interrupt service routine (ISR) and you want to know where it was triggered from, the "Call Stack" panel on the left will display the complete call chain, tracing back from the current function all the way to `Reset_Handler`. Clicking any level will make the editor jump to the corresponding source code location, and the context variables will also switch to that level.

When you're done debugging, press Shift+F5 to stop debugging. VSCode will automatically close the OpenOCD server and disconnect from the ST-Link. At this point, your debugging environment is fully verified. From compilation and flashing to debugging, the entire toolchain is ready. You can now focus on writing code instead of being plagued by environment issues.

---

## Advanced Debugging Techniques: Hardware Breakpoints and Memory Viewing

The content above already covers 90% of daily debugging needs, but sometimes you'll encounter trickier situations that require some advanced techniques.

The first thing to discuss is hardware breakpoints vs. software breakpoints. You may have heard that Cortex-M3 only supports six hardware breakpoints, but software breakpoints can be set in unlimited numbers. What's the difference? Software breakpoints are implemented by writing a special instruction (BKPT) at the target address; when the CPU executes this instruction, it triggers a debug exception. But Flash is read-only memory, and you can't modify its contents at runtime, so software breakpoints can only be used for code running in RAM. Hardware breakpoints are implemented through comparison circuits inside the CPU and don't require modifying code, so they can be set anywhere in Flash, but their quantity is limited by hardware (six for Cortex-M3).

In practice, this means when you set a seventh breakpoint, GDB will report "cannot set breakpoint" or the breakpoint simply won't take effect. There are two solutions: first, delete unnecessary breakpoints to keep active breakpoints within six; second, run a piece of code in RAM (for example, copy a frequently debugged function to RAM for execution), so you can use software breakpoints.

In GDB, you can use `info breakpoints` to view the status of all current breakpoints:

```text
(gdb) info breakpoints
Num     Type           Disp Enb Address            What
1       hw breakpoint  keep y   0x080001a8 in main at main.cpp:42
```

Pay attention to the `Type` column: if it shows `hw breakpoint`, it means a hardware breakpoint is being used; `breakpoint` indicates a software breakpoint.

The second advanced technique is memory viewing. Sometimes you want to view the contents of a large contiguous block of memory, such as an entire DMA buffer or an array of structs. You can achieve this with the `x` command:

```text
(gdb) x/10wx 0x20000000
```

This command displays the contents of 10 words (4 bytes each) starting from 0x20000000 in hexadecimal. `x/10gx` can display 64-bit integers (8 bytes), which is useful when viewing double-precision floating-point arrays.

In VSCode, you can enter an array name in the "Watch" panel to view its contents, but if you want to view raw memory, you can execute this in the "Debug Console":

```text
-exec x/32xb 0x20000000
```

This displays 32 bytes of memory content, one byte at a time; `b` means byte. This is very useful when debugging memory alignment issues or DMA transfer problems.

The third technique concerns RTOS debugging. If you're using an RTOS like FreeRTOS, you'll find the call stack filled with functions like `xTaskResumeAll` and `vTaskSwitchContext`, making it hard to find the real entry point of the current task. The Cortex-Debug plugin supports RTOS-aware debugging, but it requires additional configuration. Add this to `launch.json`:

```json
"rtos": "FreeRTOS",
"rtosConfigFile": "${workspaceRoot}/third_party/FreeRTOS/FreeRTOS/Source/include/FreeRTOS.h"
```

After this configuration, the debug panel will display a "Threads" dropdown listing all currently created tasks, and you can switch between different tasks just like debugging a multithreaded program.

The last technique to discuss is SWO (Serial Wire Output). SWO is a feature of ARM Cortex-M that can output debug information through a high-speed channel on the SWD interface, without occupying UART resources, and it's much faster than printf. However, SWO configuration is relatively complex: it requires setting the baud rate, configuring the TRACETCK pin, and not all ST-Links support it (only the ST-Link V2 does). This topic is fairly independent, and I plan to cover it in a separate article later.

---

## Troubleshooting Common Debugging Issues

Even if you follow the steps above one by one, you'll inevitably run into all sorts of weird problems. The debugging environment involves many components, and a failure in any single place will cause debugging to fail. I've compiled the pitfalls I've encountered, categorized by symptom, in the hope of helping you quickly pinpoint the issue.

The most common problem is `Error: target not halted`. This error usually appears when you execute the `load` command, and the reason is that OpenOCD cannot flash Flash while the chip is running. The solution is to execute `monitor halt` before load:

```text
(gdb) monitor halt
(gdb) load
```

The `monitor` prefix tells GDB to pass the following command to OpenOCD instead of executing it itself. The `halt` command stops the CPU and puts it into debug mode. If halt also errors out, the chip might be in a low-power mode and needs more time to wake up, or the SWD connection might be unstable.

The second common error is `Error: undefined debug reason 8`. I was also baffled when I encountered this error; I finally looked it up and found it was because the chip was in Sleep or Stop Mode, and the debugger couldn't wake it up normally. The solution is to disable debugger sleep before entering low-power mode, or press the reset button to force the chip out of its low-power state.

The third scenario is when a breakpoint is set but the program doesn't stop there. There are a few possible reasons. First, you might have indeed exceeded the hardware breakpoint limit (six); try deleting a few unused breakpoints. Second, the code might not have been loaded to that address at all; check the output of the `load` command to ensure it was actually written to the correct Flash region. Third, the code might have been optimized away — the optimizer might have deleted the code where you set the breakpoint entirely; try changing the compilation optimization to `-O0`.

The fourth problem is variables displaying `<optimized out>` or showing obviously incorrect values. This is almost always caused by compiler optimization. In your debug build, you should use `-Og` (a mode specifically optimized for debugging) or `-O0` (optimization completely disabled), not `-O2` or `-O3`. In CMakeLists.txt, you can set the optimization level separately for the Debug configuration:

```cmake
add_compile_options(
    $<$<CONFIG:Debug>:-Og>
    $<$<CONFIG:Release>:-O2>
)
```

There's also the case of variables in inlined functions. Because the code has been inlined, the original "local variables" might have been optimized into registers or disappeared entirely, and GDB cannot track them. In this case, you can use `-fno-inline` to disable inlining, or simply set a breakpoint at a higher level.

The fifth problem is VSCode being unable to connect to OpenOCD. The error message might be "Failed to connect to GDB" or "Could not connect to localhost:3333". First, confirm that OpenOCD isn't running elsewhere (for example, a manually started instance from earlier that hasn't been closed), then use `netstat -tlnp | grep 3333` to check if the port is occupied. If the port is occupied, either kill the occupying process or use a different port in `launch.json` (but OpenOCD defaults to 3333, and changing the port requires extra configuration, which is not recommended).

If OpenOCD isn't starting at all, check whether `serverpath` is correct. Run `/usr/bin/openocd --version` directly in the terminal; if the command doesn't exist, it means OpenOCD isn't installed or is installed elsewhere. Use `which openocd` to find the correct path, then update `launch.json`.

WSL users also have a special problem: USB permissions. The error message is usually `LIBUSB_ERROR_ACCESS` or `could not open device`. First, confirm that the ST-Link has been forwarded to WSL by usbipd (you should be able to see the device with `lsusb | grep -i stlink`), then use the script I mentioned earlier to fix permissions:

```bash
sudo chmod 666 /dev/bus/usb/001/XXX
```

The last-resort trick is to check OpenOCD's detailed logs. Add this to `launch.json`:

```json
"openOCDLaunchCommands": ["debug_level 3"]
```

This will make OpenOCD output the most detailed debug information. Even if you can't understand most of it, at least you'll know which step it's getting stuck on. You can also start OpenOCD manually in the terminal and observe the output; many error messages only appear there.

---

## And With That, We're Done

If you've followed along through the previous articles, by now you should have a complete STM32 development toolchain: a cross-compiler, a CMake build system, the HAL (Hardware Abstraction Layer) library, the OpenOCD flashing tool, and the GDB debugging environment we just configured. From compilation and flashing to debugging, the entire workflow can be completed under Linux, no longer dependent on Windows-exclusive IDEs like Keil.

When you press F5 in VSCode for the first time, watch the program stop at the main function breakpoint, single-step a few lines, modify a variable's value, and see the LED's blinking frequency change accordingly — that sense of control is unparalleled. You're no longer blindly flashing, guessing, and flashing again; instead, you can precisely observe every step of the program's execution. This is the experience embedded development should offer.

Migrating from Keil to this toolchain offers many tangible benefits beyond cross-platform advantages. You can write code with Vim/Neovim, get code completion more powerful than any commercial IDE using clangd, manage versions with Git (no more dealing with those weird project files), and run automated tests with CTest. More importantly, this toolchain is completely open source and fully customizable. When you encounter problems, you can read the source code and modify configurations instead of being trapped in a black box.

Next, we can finally start talking about the application of modern C++ in embedded systems. How do C++ features like templates, RAII (Resource Acquisition Is Initialization), lambda expressions, and constexpr play a role on the resource-constrained STM32? How do you write embedded code that is both modern and efficient? This is the true core of this tutorial; the toolchain setup we've done so far was just preparation. But now that we have this toolchain, we can focus on the code itself without being distracted by environment issues.
