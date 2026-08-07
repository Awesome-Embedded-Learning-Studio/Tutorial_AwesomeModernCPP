---
title: "Install the Three Things You Need to Write C++"
description: "Set up vscode, the MinGW compiler, and CMake on Windows from scratch, with screenshots and verification at every step"
chapter: 14
order: 2
platform: host
difficulty: beginner
cpp_standard: [17, 20]
tags:
  - host
  - 入门
  - 基础
  - beginner
  - 工具链
reading_time_minutes: 12
---

# Install the Three Things You Need to Write C++

## Opening

Last time we agreed you need two pieces of software: an editor (vscode) and a compiler. There's actually a third thing you need: a build tool called CMake.

Let me explain what CMake does first. As we write more C++, a project won't be just one .cpp file. It might be five, six, a dozen files, spread across different folders. At that point, typing out the compile commands one by one will drive you crazy. CMake takes care of this stuff for you. You write one config file that tells it which files are in the project and what program to produce, and it handles the rest. We'll actually use it next time; for now we just need to install it.

This whole article is hands-on, step by step, with a screenshot for every step. Once these three are installed, we can write the first real, runnable program in the next article.

## The Windows Path (Recommended)

If your system is Windows 10 or Windows 11, follow this section. Three steps, in order.

### Step 1: Install vscode

vscode is a free editor from Microsoft. From here on, this is where we'll type our code.

Open your browser and go to <https://code.visualstudio.com>.

There's a big blue button in the middle of the page that says "Download for Windows". Click it. If your browser doesn't start downloading automatically, it'll take you to a download picker page. Pick the "Windows" option and you'll get a `.exe` installer.

Once it's downloaded, double-click `VSCodeUserSetup-x64-x.x.x.exe`. The installer looks like any other installer, just keep hitting Next. The screen to watch out for is this one:

Tick all of these (especially "Add to PATH", which you absolutely must check or you'll have headaches later):

- On the "Select Additional Tasks" screen, tick "Add 'Open with Code' action to Windows Explorer file context menu"
- Tick "Add 'Open with Code' action to Windows Explorer directory context menu"
- Tick "Register Code as an editor for supported file types"
- Tick "Add to PATH" (the most important one)

The remaining options (whether to put a shortcut on the desktop, some of the right-click menu items) are up to you.

::: details Click to see: What if I forgot to tick "Add to PATH"?
Don't panic. The easy way out is to add vscode's install folder to the system PATH by hand. But the even easier way is: uninstall and reinstall, and tick the box this time. Reinstalling takes two minutes, faster than wrestling with PATH.
:::

When it's done, press the Win key on your keyboard (the one with the Windows logo). You should see the vscode icon in the Start menu.

Click it. If you see a welcome page, you're done.

### Step 2: Install the Compiler (Going the MinGW-w64 Route)

The compiler is the program that translates the .cpp you write into a .exe. There are several C++ compilers that work on Windows. We're going with MinGW-w64, which is essentially the famous GCC compiler from Linux, ported to Windows.

Why pick this one? Two reasons. First, it's the same toolchain as the Linux environments we'll touch later in this series, so the command-line habits you build here carry over everywhere. Second, if you eventually want to go the embedded route (which this series also covers), GCC is the mainstream choice, so getting familiar with it early does no harm.

Microsoft has its own compiler called MSVC (the Visual Studio family), which is also perfectly good. The differences between the two are tucked into a collapsible box below; we won't dig in here, just get MinGW installed first.

The least painful way to install MinGW is through a tool called MSYS2. MSYS2 is basically a package manager. Think of it as an app store, like the one on your phone, except it installs command-line tools for programmers, and you operate it from the command line.

Open your browser and go to <https://www.msys2.org>.

There's a download link on the page pointing to an installer named something like `msys2-x86_64-xxxxxxxx.exe` (the filename contains a date, so it's normal if yours looks different). Download it and double-click to run.

The installer asks you to pick an install path. **I strongly recommend leaving it at the default `C:\msys64`**, don't change it. We're going to add things to the system PATH later, and having the path baked in is just easier. If you install somewhere else, every path from here on has to change too, and that's where mistakes creep in.

Keep hitting Next until it finishes. Once it's done, you'll see a few new MSYS2 entries in the Start menu.

::: warning Here's the trap beginners fall into most
There are several MSYS2 entries in the Start menu: "MSYS2 MINGW64", "MSYS2 UCRT64", "MSYS2 CLANG64", "MSYS2", and so on.

**Open "MSYS2 UCRT64" specifically**, don't open "MSYS2" (the plain one). We're installing the UCRT64 build of GCC, and it only works properly inside the UCRT64 terminal. Open the wrong one and after you install, the commands won't be found.
:::

After opening the UCRT64 terminal you'll see a command-line window with purple text. Type this line in (get the capitalization, spaces, and hyphens right), then hit Enter:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc
```

`pacman` is the command that drives the MSYS2 "app store". `-S` means "install (sync)", and that long string after it is the name of the package to install.

The first time you install something, pacman will ask whether to continue and whether the package is the right one. Type `Y` and Enter to confirm. It'll download a few tens of megabytes, give it a moment.

Once it's installed, we have to tell Windows about this new compiler. That means telling the system's PATH variable where it lives. What's PATH? Think of it as the system's "address book of frequently used folders". Any address written in there, the system can find the programs inside directly, without you spelling out the full path every time.

Press the Win key, search for "environment variable", and click "Edit the system environment variables".

In the window that pops up, there's an "Environment Variables" button in the bottom right. Click it. In the "System variables" list in the lower half, find the row named `Path` (note: `Path`, not `PATHEXT`), and double-click it.

In the list that appears, click "New" and enter this line (assuming you used the default path when installing MSYS2):

```text
C:\msys64\ucrt64\bin
```

Hit OK all the way out to close every window and save.

Now let's verify it worked. **You have to open a brand new command-line window for this.** You just changed PATH, and the old window won't pick up the change automatically, you must open a fresh one.

Press Win+R, type `cmd`, hit Enter, and a Command Prompt opens (the one with the black background). Type:

```bash
g++ --version
```

If you see output like this (the exact version number may be newer), you're set:

```text
g++ (Rev2, Built by MSYS2 project) 16.1.0
Copyright (C) 2025 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
```

If you get something like "'g++' is not recognized as an internal or external command", the PATH isn't set right. Go back and check three things: is the path written as `C:\msys64\ucrt64\bin` (a lot of people miss the `\ucrt64\` in the middle), is anything misspelled, and did you actually open a new cmd window.

### Step 3: Install CMake

Last one. Open your browser and go to <https://cmake.org/download>.

The page lists installers for several platforms. Under the Windows section, find `Windows x64 Installer` and download that `.msi` file (the filename looks like `cmake-x.y.z-windows-x86_64.msi`).

Double-click the `.msi`. Keep hitting Next through the installer, and pay attention on this screen:

It asks whether to add CMake to the system PATH. **Pick the second option, "Add CMake to the system PATH for all users"** (add it to the system PATH for everyone). The first option leaves it out by default, the third only adds it for the current user. The middle one is the least hassle.

Keep going and finish the install.

Verify it. **Same rule, open a fresh cmd window** (the old window's PATH hasn't refreshed). Type:

```bash
cmake --version
```

See a version number printed, and you're set:

```text
cmake version 4.4.2

CMake suite maintained and supported by Kitware (kitware.com/CMake).
```

::: details Click to see: Installing CMake from the command line works too
If you prefer the command line, you can also install CMake from inside the MSYS2 UCRT64 terminal with `pacman -S mingw-w64-ucrt-x86_64-cmake`. Done that way, CMake lands under `C:\msys64\ucrt64\bin`, right alongside the GCC you just installed, so you don't need to touch PATH separately. Pick one of the two methods, don't do both.
:::

## Install the C++ Extensions for vscode

The three main pieces are in place. Now let's give vscode two "extensions". An extension is basically a plugin for vscode that adds extra features.

Open vscode. In the column of icons on the far left, find the one made of four little squares (hovering over it shows "Extensions") and click it. Or just press `Ctrl+Shift+X`.

In the search box at the top, search for each of these two names, find the matching extension, and click "Install":

The first is C/C++. This is the official extension from Microsoft, and it gives you code completion, go-to-definition, error highlighting, that sort of thing. We won't touch its settings until article 5, but installing it now costs you nothing.

The second is CMake Tools. Also official from Microsoft, it's what makes vscode play nicely with CMake. We'll use it in the next article when we write our first program.

Once both are installed, the blue status bar at the bottom of the vscode window gets a few CMake-related buttons (the current build type, a build button, things like that). Seeing those means the extensions are live.

::: details Click to see: How to install on Linux (Ubuntu/Debian family)
That covers the Windows main line. If you're on a Linux machine, the whole thing installs with one command, far less fuss than Windows.

Open a terminal and type this (it installs the compiler, CMake, and debugger all at once):

```bash
sudo apt update && sudo apt install -y build-essential cmake ninja-build gdb
```

The `build-essential` package contains the GCC compiler, `cmake` is the build tool, `ninja-build` is a faster build backend that CMake often pairs with, and `gdb` is the debugger we'll need later for tracking down problems. `sudo` means "run with admin privileges" and it'll ask for your password.

For vscode, go to <https://code.visualstudio.com>, download the `.deb` package, and double-click to install (or from the command line, `sudo apt install ./code_*.deb`).

Verify the same way as on Windows:

```bash
g++ --version
cmake --version
```

If you see version numbers, you're set. The C/C++ and CMake Tools extensions still need to be installed inside vscode, that part has nothing to do with the OS.
:::

::: details Click to see: How are MSVC and MinGW really different, and which should you pick?
There are two main C++ compiler families on Windows: Microsoft's own MSVC (the Visual Studio family), and the MinGW route we're taking here (the Windows port of GCC).

Short version: both work, both can compile Windows programs, and for day-to-day learning the differences barely matter. But a few points are worth knowing.

The debugger differs. MSVC pairs with Microsoft's own debugger; MinGW pairs with GDB. This series uses GDB a lot later on, because the embedded track also uses GDB, so the habits line up.

C++ standard support and pace differ. MSVC ships ahead on some new features, GCC ahead on others, they trade the lead. For the beginner stage it makes no difference.

Size differs. The full Visual Studio install is a dozen-plus GB. MinGW plus MSYS2 is one or two GB and you're set. When you're just starting out, lighter is easier.

Command-line habits differ. MSVC leans toward the Windows-native world (the cl.exe compiler, linker setup that has nothing in common with Linux), while MinGW matches GCC on Linux and macOS. Every command and CMake config later in this series assumes GCC, so MinGW is the smoother path.

If later on you end up doing Windows desktop app development, or you need to call Windows-specific APIs (Direct3D, for instance), that's the time to install Visual Studio and pick up MSVC. The detailed comparison and how to switch between them lives in vol1/ch00, the article dedicated to setting up a Windows environment.
:::

Three things are now installed: the vscode editor, the MinGW compiler, and the CMake build tool, plus the two C++ extensions inside vscode. In the next article we'll write our first C++ program inside vscode, actually run it, and see how that line `Hello, World!` turns from code into letters on the screen.
