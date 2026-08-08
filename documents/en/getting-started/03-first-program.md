---
title: "Your First C++ Program: Getting Hello to Run in vscode"
description: "Build a project from scratch in vscode, write main.cpp and CMakeLists.txt, configure, build, and run, until Hello actually prints to the screen"
chapter: 14
order: 3
platform: host
difficulty: beginner
cpp_standard: [17, 20]
tags:
  - host
  - 入门
  - 基础
  - beginner
  - CMake
reading_time_minutes: 15
---

# Your First C++ Program: Getting Hello to Run in vscode

## Opening

Environment all set up, right? (If not, go back to article 2 — vscode, MinGW, CMake, and those two extensions all have to be installed.) This time we'll do something with a bit of ceremony: write your first C++ program by hand, actually run it, and make it spit out `Hello, C++!` on the screen.

The whole thing is clicking buttons inside vscode. You won't type a single command. (The command-line version is in a collapsible box at the end — open it if you're curious.) We'll walk the full mini-project loop: make a folder, write code, write the CMake config, configure, build, run. Sounds like a lot of steps, but each one is a single click. Follow along once and you'll have the routine down.

## Step 1: Make a project folder

First, find somewhere to put the code you write. Don't just dump files on the desktop or the root of the C drive — within two days it'll be a mess. Give each project its own folder.

On the desktop (or wherever you like, something like `D:\code\`), right-click and create a new folder. Name it `hello`. Short, all lowercase, no spaces. Those three rules apply to every name you'll ever give a file in code, so get used to them now.

Once the folder is there, open vscode. Click the menu `File → Open Folder`, in the popup pick the `hello` folder you just made, and click "Select Folder".

After it opens, vscode shows an Explorer panel on the left, with `hello` as its title and nothing underneath — empty, because it's an empty folder. That's exactly right. We'll fill it from scratch.

::: tip "Open Folder" isn't a pointless step
vscode isn't like Notepad. It thinks in terms of "projects". You have to tell it "I'm going to work inside the hello folder from now on", and only then does it wire up extensions, CMake, debugging, and everything else to that folder. You can drag a `.cpp` into vscode and edit it, sure, but the CMake pipeline further on won't work. So every time you start a new project, the first step is always "Open Folder".
:::

## Step 2: Create main.cpp

To the right of the `hello` title in the Explorer panel on the left, there's a row of small icons. Hover over them. The first one, which looks like a blank page with a plus sign, is "New File" (the tooltip says `New File`). Click it.

After you click, a small input box appears in the panel asking for a filename. Type `main.cpp` and press Enter.

Why `main`, and why the `.cpp` extension? `main` is the conventional name — the entry point of a C++ program (where execution starts) lives in this file, and everyone names it that way, so when you talk to other people nothing gets lost in translation. `.cpp` is the standard suffix for C++ source files; the moment a compiler sees `.cpp` it knows to compile it as C++.

After Enter, the main editing area opens `main.cpp` (empty, of course), and `main.cpp` also shows up as a new entry in the Explorer on the left.

## Step 3: Paste the code in

Copy this whole snippet and paste it into `main.cpp`:

```cpp
#include <iostream>

int main() {
    std::cout << "Hello, C++!\n";
    return 0;
}
```

Once it's pasted, the code in the editor turns colorful — keywords like `int`, `return`, `#include` get one color, and the string `"Hello, C++!\n"` gets another. That's syntax highlighting, which we mentioned last time. It's the editor doing its job.

A quick word on what this code does. You don't have to memorize any of it yet — just get a feel for the shape.

The first line, `#include <iostream>`, pulls in C++'s built-in "input/output" toolkit. The name `iostream` splits into input output stream, and it's what handles "read stuff from the keyboard" and "write text to the screen".

The `int main()` in the middle is the entry point. When a C++ program runs, it always starts executing from the first line of the `main` function, no exceptions. The braces `{}` wrap what the program actually does.

`std::cout << "Hello, C++!\n";` writes text to the screen. You can think of `std::cout` as the codename for "the screen" object, and `<<` as an arrow that says "push this in", pushing the text on the right into the screen to be displayed. The `\n` is a newline character — after printing, it moves the cursor to the next line.

`return 0;` tells the operating system "this program finished normally, no errors". 0 means OK, anything non-zero means something went wrong. We'll use that convention later; for now just remember 0 is good.

## Step 4: You also need a CMakeLists.txt

Code's written. You might be thinking, "now I just hit that triangle run button and it'll go, right?"

No. This step trips up a lot of beginners, so let me explain why first.

The run button that comes with vscode (or pressing `F5`) doesn't know which compiler you want to use, which file to compile, or what to name the resulting program — it knows nothing. Hand it a `main.cpp` and it just stares at you blankly. We need to write a separate "instruction manual" that tells it all of this. The file that manual goes in is called `CMakeLists.txt`, and CMake is what reads it.

(Some of you will ask: didn't article 1 say the compiler can compile directly? Right — calling `g++ main.cpp` on the command line does work. But vscode's graphical buttons go through the CMake pipeline. Since we're going to click buttons in vscode, we follow CMake's rules. CMake also handles multi-file projects, as we'll see in article 4.)

Next to `main.cpp`, use the same "New File" icon from Step 2 to create another file named `CMakeLists.txt` (mind the capitalization: `C`, `M`, `a`, `k`, `e` are uppercase, the `L` in `Lists` is uppercase, the rest lowercase, with the `.txt` suffix). CMake dictates this name exactly. One letter off and it won't recognize it.

Paste this in:

```cmake
cmake_minimum_required(VERSION 3.20)
project(hello LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(hello main.cpp)
```

What these five lines mean, translated line by line:

The first line, `cmake_minimum_required(VERSION 3.20)`, says "the CMake running this project must be at least version 3.20". 3.20 is a fairly old floor — almost every machine meets it. CMake uses this line to check whether your installed CMake is new enough.

The second line, `project(hello LANGUAGES CXX)`, says this project is named `hello` and the language is C++ (`CXX` is CMake's codename for C++; plain C is `C`, C++ is `CXX`).

The fourth line, `set(CMAKE_CXX_STANDARD 17)`, says "use the C++17 version of the standard". C++ has kept evolving over the years — there's C++11, 14, 17, 20, 23, each newer one adding more features. 17 is a stable version that almost any project can use, so we start with 17.

The fifth line, `set(CMAKE_CXX_STANDARD_REQUIRED ON)`, says "the standard above isn't a suggestion, it's a hard requirement". If the compiler doesn't support C++17, it errors out instead of quietly dropping back to an older standard — because if it silently downgraded, you wouldn't know, and debugging the mess later would be a nightmare.

The last line, `add_executable(hello main.cpp)`, is the most important one. `add_executable` means "produce an executable program". The first argument inside the parentheses, `hello`, is the name of the program to produce, and the second, `main.cpp`, is the source file to compile. Read as a whole: compile `main.cpp` into an executable program named `hello` (on Windows that's `hello.exe`).

## Step 5: Pick a kit

Remember that extra chunk that showed up in vscode's bottom status bar after you installed the two extensions in article 2? Time to use it.

Click the bit in the status bar that says `No Kit Selected`. A small list pops up. Or press `Ctrl+Shift+P` to open the command palette, type `CMake: Select a Kit`, and press Enter — same thing.

The list shows every compiler vscode managed to find on your machine. You should see an entry something like `GCC 16.1.0 x86_64-w64-mingw32` or `GCC x.x.x ucrt64` (the exact version depends on which MinGW you installed). Pick that GCC one.

Once selected, the text in that part of the status bar changes to something like `GCC 16.1.0`, showing which compiler is currently active.

"Kit" is the CMake Tools extension's term. You can think of it as a "toolbox" — it tells CMake Tools "use this compiler from now on". You only have to pick once; the next time you open this project it remembers.

::: warning What if there's no GCC in the list
If the list has no GCC at all, just entries like `Visual Studio`, that means the MinGW step in article 2 didn't install properly, or it installed but the PATH isn't set right and CMake Tools can't find it. Go back and check Step 2 of article 2. Focus on whether `C:\msys64\ucrt64\bin` was actually added to the system PATH, and whether you restarted vscode after changing the PATH (PATH changes need a vscode restart to take effect).

There's usually also an `[Unspecified]` entry at the very bottom of the list, meaning "don't specify". Don't pick that one — we want to explicitly select GCC.
:::

## Step 6: Configure

With the kit picked, the next step is called "configure". Click the `Configure` text in the status bar, or run `CMake: Configure` from the command palette.

After you click, an output panel pops up at the bottom of vscode and text starts scrolling, something like this:

It runs for a bit and stops. As long as there's no red text and you see `Configuring done` and `Generating done` at the end, it worked.

What does configure do? CMake reads your `CMakeLists.txt` and, following the instructions inside, generates a pile of "build files" (a `build` subfolder appears under `hello`, and everything goes in there). This step **has not compiled your code yet** — it's CMake doing prep work, lining up which compiler to use, which files to compile, and what to produce. The actual compile happens next.

::: tip When to re-run configure
From now on, any time you change `CMakeLists.txt` (say, adding a new source file), you have to re-run configure so CMake picks it up. Just editing a `.cpp` file does not require reconfiguring — CMake notices that on its own.
:::

## Step 7: Build

Configured. Now click the `Build` button in the status bar, or run `CMake: Build` from the command palette.

The output panel scrolls again, but with different content this time — the compiler is actually working now. You'll see lines like `Building CXX object ... main.cpp.o` and `Linking CXX executable hello.exe`. The last line shows a success message.

At this point your `main.cpp` has actually been translated into `hello.exe`, sitting in the `hello\build\` folder. Next step: run it.

::: warning If it errored out
The most common error is "compiler not found" or a wrong compiler path — go back to Step 5 and pick the kit again. Another common one is mistyping the `main.cpp` filename (something like `mian.cpp`), which CMake then can't find. Error messages usually tell you which line went wrong, so read them and match them up. After fixing, click `Build` again.
:::

## Step 8: Run

There's a triangle play button in the status bar — that's the `Run` button. Be careful not to click the one next to it with the little bug icon; that's the `Debug` button, which drops you into debug mode. We don't need that yet.

Click the run button. A terminal panel pops up at the bottom of vscode (if it doesn't, press `` Ctrl+` `` to bring it up), and inside it prints a single line:

```text
Hello, C++!
```

There it is — your first C++ program is actually running. That one line went from code to characters on the screen, through the whole "write code → configure → build → run" pipeline. Every C++ program you write from here on uses this same routine.

## Step 9: Tweak it and run again

Getting it to run once doesn't mean you're fluent. Let's change the code and run it again, to lock the loop in.

Go back to `main.cpp`, and change that `Hello, C++!` line to whatever you want to say, like this:

```cpp
#include <iostream>

int main() {
    std::cout << "我学会了写 C++！\n";
    return 0;
}
```

Save it (`Ctrl+S`). After saving, the little white dot next to the filename in the status bar disappears, which means the change is on disk.

Then just click the `Run` button in the status bar. CMake Tools automatically rebuilds first (it noticed the `.cpp` changed) and then runs. This time the terminal prints:

```text
我学会了写 C++！
```

From now on, editing code is just these moves: change, save, run. The configure and build steps in between are wired up for you automatically by CMake Tools.

::: details Click to see: How to do it on the command line
Those buttons you've been clicking are really just running a few commands underneath. Let's run them by hand in the vscode terminal so you can see what the buttons are doing.

Open the vscode terminal (menu `Terminal → New Terminal`, or the shortcut `` Ctrl+` ``). The first build takes three steps:

```bash
cmake -B build
cmake --build build
.\build\hello.exe
```

The first, `cmake -B build`, is "configure" — it generates the build files in the `build` folder (`-B` specifies the output directory).

The second, `cmake --build build`, is "build" — it actually calls the compiler and turns `main.cpp` into `hello.exe`.

The third, `.\build\hello.exe`, is "run" — it just executes that `.exe`.

After that, whenever you change code, you only rerun the last two (the second step automatically recompiles only the files that changed, then you run the third).

If you're on Linux (the apt route from the collapsible box in article 2), the run command is slightly different:

```bash
cmake -B build
cmake --build build
./build/hello
```

Two differences: on Linux, executables don't have to carry the `.exe` suffix (CMake produces `hello` instead of `hello.exe` by default), and when you run it the path separator is a forward slash `/` with a `./` prefix.
:::

Your first C++ program is running. From code to that line on the screen, you've walked the whole pipeline once. This loop — make a project, write code, write CMakeLists, configure, build, run — is something you'll use over and over. Run it a few more times and it'll be second nature.

Next time we'll grow the project: one `.cpp` isn't enough anymore, so we'll look at how to organize several files and make them work together.
