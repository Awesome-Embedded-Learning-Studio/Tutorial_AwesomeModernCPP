---
title: First C++ Program
description: Write, compile, and run your first C++ program, and understand the main
  function, input/output, and the compilation process.
chapter: 0
order: 3
difficulty: beginner
reading_time_minutes: 12
platform: host
prerequisites:
- Linux 环境搭建
- Windows 环境搭建
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
cpp_standard:
- 11
- 14
- 17
- 20
---
# Your First C++ Program

Now that our environment is set up and the compiler is installed, it's time to get down to business—writing our first line of C++ code.

The first lesson of every programming language is always Hello, World. Whether it's C#, Rust, C++, C, Java, Kotlin... honestly, every tutorial starts by printing Hello World. I think this tradition probably comes from the legendary K&R book, *The C Programming Language*. The author, if I recall correctly, is the very person who created C. Enough said—respect!

But I can promise you that if we thoroughly dissect this tiny program, many concepts down the road will feel much more natural. So don't rush past this section; let's digest it line by line.

## Starting from Scratch—the Skeleton of hello.cpp

Open your favorite editor, create a new file called `hello.cpp`, and type in the following code exactly as shown. Note that I said *type* it in, not copy and paste (we often joke that programmers only use three keys: Ctrl, C, and V. Let me clarify—don't do this when you're actually learning. Save that for work you're not interested in but have to do anyway, like writing boring business code)—muscle memory really matters when learning to program.

```cpp
#include <iostream>

int main()
{
    std::cout << "Hello, C++!" << std::endl;
    return 0;
}
```

In the repository, I used CMake to organize the project's build, but actually, if you want to

```bash
g++ hello_world.cpp -o hello_world
./hello_world
```

I won't object, but I highly recommend using CMake:

```bash
cd /path/to/project
cmake -B build -S .
cmake --build build -j${nproc}
./build/hello_world
```

Output:

```text
Hello, C++!
```

Six lines of code—it looks ridiculously simple. But it actually hides several key concepts. Let's break it down line by line.

### Line One: `#include <iostream>`

This line tells the compiler: we need the "input/output stream" feature module. You can think of it as pulling a toolkit called iostream out of your toolbox—it contains `std::cout` (for output) and `std::cin` (for input), which are the most basic ways we interact with a program. The C++ standard library has tons of toolkits like this, such as `<vector>`, `<string>`, and `<cmath>`; just include what you need.

The angle brackets `< >` indicate that this is a system header file, and the compiler will look for it in the standard library paths. For header files you write yourself, use double quotes `"my_header.h"`, and the compiler will search the current directory first.

### Line Two: `int main()`

This is the entry point of the entire program. When the operating system starts our program, execution begins right here. `int` means this function returns an integer to the operating system—returning 0 means "everything is fine," while returning a non-zero value means "something went wrong." This return value can be accessed via `$?` in Linux scripts, and CI/CD pipelines often rely on it to determine whether a program executed successfully.

### Lines Three through Five: The Function Body

```cpp
std::cout << "Hello, C++!" << std::endl;
return 0;
```

`std::cout` stands for "character output" (c = character, out = output), and you can think of it as the screen. The `<<` operator is redefined here; its job is to "push" the content on the right into the output stream on the left. So `std::cout << "Hello, C++!"` pushes that text onto the screen.

`std::endl` is short for "end line." It does two things: it outputs a newline character, and then it flushes the buffer—meaning it ensures your text appears on the screen immediately rather than being stashed away in some corner.

Finally, `return 0` tells the operating system: I'm done, and everything is fine.

> ⚠️ **Pitfall Warning**: In some tutorials or old code, you might see the `void main()` syntax. This is wrong. The C++ standard explicitly states that the return type of `main` must be `int`. Although some older compilers might not flag it, that doesn't make it correct. Make it a habit to always write `int main()`.

You might have noticed that both `std::cout` and `std::endl` have a `std::` prefix. `std` is short for "standard," and it's a **namespace**—think of it as a brand label on a toolkit. Everything in the C++ standard library lives inside the `std` namespace to avoid name collisions. For example, if you write your own function called `cout`, it won't clash with the standard library's `std::cout` because they're in different namespaces. Some tutorials add a line like `using namespace std;` at the top and then just write `cout`, which确实 saves typing but easily causes naming conflicts in large projects. So let's get into the habit of using the `std::` prefix right from the start.

## Compiling and Running

The code is written, so let's get it running. Open a terminal, navigate to the directory containing `hello.cpp`, and run:

```bash
g++ -o hello hello.cpp
```

This command does two things: it uses the `g++` compiler to compile `hello.cpp` into an executable, and `-o hello` specifies the output file name as `hello` (if unspecified, it defaults to `a.out`, which isn't very meaningful). After a successful compilation, a `hello` file will appear in the current directory. Run it directly:

```bash
./hello
```

Output:

```text
Hello, C++!
```

Great, your first C++ program is running successfully.

If you've already read the environment setup chapter, you might remember how to use CMake. For a single-file tiny program like this, using the `g++` command directly is the fastest approach. But as the project grows and files multiply, manually typing compile commands every time will drive you crazy—that's where CMake proves its worth. We'll stick with `g++` for now and formally introduce CMake in later chapters.

## What Happens Behind the Scenes—the Compilation Pipeline

Ah, now this is something I can talk about. I've actually seen self-proclaimed computer "experts" argue with me—what do you mean, compilation, linking, and execution steps? Nowadays we just click the run button and it works.

I laugh every time I see this. Every time this topic comes up, I drag this person out as a cautionary tale. It's a classic case of learning a little bit about computers and then showing off. Come on, let me tell you just how complex things really are behind the scenes:

Every time you type `g++ -o hello hello.cpp`, a complete pipeline runs behind the scenes. We don't need to dive deep into the details of every stage, but we at least need to know this process exists—because when you run into compilation errors later, knowing which stage the error occurred in will help you pinpoint the problem quickly.

The entire process can be simplified into four steps. The first step is **preprocessing**, where the compiler handles all directives starting with `#`—replacing `#include <iostream>` with the actual content of the iostream header, expanding macro definitions, and handling conditional compilation. The second step is **compilation**, which translates the preprocessed C++ code into assembly language—this is where the compiler performs syntax checking and type checking, and any syntax errors you wrote will be caught here. The third step is **assembly**, which translates the assembly code into machine code, generating an object file (`.o` file). The fourth step is **linking**, which combines the object file with the required library files (like the C++ standard library) to produce the final executable.

```text
hello.cpp → [预处理] → [编译] → [汇编] → [链接] → hello
```

You might ask: why do we need to know this? Because later on, you will inevitably encounter all sorts of compilation errors—some are preprocessing issues (header file not found), some are compilation issues (syntax errors, type mismatches), and some are linking issues (duplicate definitions, unresolved symbols). Knowing which stage the error comes from gives you a clear direction for troubleshooting.

> ⚠️ **Pitfall Warning**: When the compiler reports errors, **always look at the first error message first**. Many beginners habitually start from the last error, but C++ compilers have a "cascading error" trait—a single error can trigger dozens of "false positive" errors afterward. Fix the first one, and the rest might disappear automatically. Make it a habit: read the first, fix the first, recompile, then read again.

## Pitfalls We've Fallen Into—Common Compilation Errors

Being able to write correct code isn't enough; we also need to learn how to read error messages. Let's intentionally create a few classic errors and see what the compiler says.

### Forgetting the Semicolon

Remove the semicolon from `hello.cpp`:

```cpp
#include <iostream>

int main()
{
    std::cout << "Hello, C++!" << std::endl  // 这里少了分号
    return 0;
}
```

Compile it:

```bash
g++ -o hello hello.cpp
```

```text
hello.cpp: In function 'int main()':
hello.cpp:5:5: error: expected ';' before 'return'
    5 |     return 0;
      |     ^~~~~~~
      |     ;
hello.cpp:4:42: note: ...after this token
    4 |     std::cout << "Hello, C++!" << std::endl
      |                                          ^
      |                                          ;
```

The compiler tells you: before `return` on line 5, it expected to see a semicolon. Although the error is flagged on line 5, the actual problem is at the end of line 4—this situation where "the error location and the reported location are off by one line" is extremely common in C++. Just remember this pattern.

### Forgetting to Include the Header File

Delete the `#include <iostream>` line and compile again:

```text
hello.cpp: In function 'int main()':
hello.cpp:3:5: error: 'cout' is not a member of 'std'
    3 |     std::cout << "Hello, C++!" << std::endl;
      |     ^~~
hello.cpp:3:5: note: suggested alternative: 'count'
hello.cpp:3:5: error: 'endl' is not a member of 'std'
    3 |     std::cout << "Hello, C++!" << std::endl;
      |                                    ^~~~
```

The compiler says "cout is not a member of std"—because it has no idea what `std::cout` is; nobody told it. The solution is to add `#include <iostream>` back. Interestingly, GCC will "helpfully" suggest whether you meant to write `count`, which can be pretty funny sometimes.

### Typos

Write `std::cout` as `std::couth`:

```text
hello.cpp:3:10: error: 'couth' is not a member of 'std'
    3 |     std::couth << "Hello, C++!" << std::endl;
      |          ^~~~~
```

The error message is straightforward—`couth` is not a member of `std`. Just double-check your spelling. This type of error is especially common in the beginner stage; `cout` and `cin` are often mistyped as `couth` and `cim` or similar. You'll get familiar with them after typing them a few times.

> ⚠️ **Pitfall Warning**: If you're using GCC, I recommend adding the `-Wall -Wextra` options when compiling, like this: `g++ -Wall -Wextra -o hello hello.cpp`. These two options enable a large number of warnings—while warnings don't stop compilation, they often point to potential problems. Treating warnings as errors is the first step to becoming a qualified C++ programmer.

## Going Further—Talking to the Program

Being able to output text isn't enough; let's make the program accept input. Create a new file called `calc.cpp` and implement a simple addition calculator.

Let's write the skeleton first, then fill it in step by step. First, we need to read two numbers from the user, so we'll use `std::cin` (c = character, in = input), which is `std::cout`'s trusty partner.

```cpp
#include <iostream>

int main()
{
    int a = 0;
    int b = 0;

    std::cout << "请输入第一个数字: ";
    std::cin >> a;

    std::cout << "请输入第二个数字: ";
    std::cin >> b;

    int sum = a + b;
    std::cout << a << " + " << b << " = " << sum << std::endl;

    return 0;
}
```

Compile and run:

```bash
g++ -o calc calc.cpp
./calc
```

```text
❯ ./build/calc
请输入第一个数字: 1
请输入第二个数字: 2
1 + 2 = 3
```

There are a few things worth noting here. `int a = 0;` declares an integer variable and initializes it to 0. The `>>` operator in `std::cin >> a;` works in the opposite direction of `<<`—it "extracts" data from the input stream and places it into the variable `a`. You can think of `<<` as "pushing out" (output) and `>>` as "pulling in" (input); the direction of the arrow is the direction of data flow.

The line `std::cout << a << " + " << b << " = " << sum << std::endl;` chains multiple `<<` operators together, which execute from left to right: first it outputs the value of `a`, then the string `" + "`, then the value of `b`, and so on. This "chaining" style is very common in C++; you'll get used to it.

For the variable declaration, we used `int a = 0;` instead of `int a;`, and this was intentional. C++ does not automatically initialize local variables—if you don't assign an initial value, the value of `a` will be whatever garbage data was left in memory. Even though `std::cin` will immediately overwrite it right after, building the habit of "initialize on declaration" is extremely important. It will help you avoid a whole category of hard-to-debug issues.

## Try It Yourself

At this point, we can write code, compile it, run it, and read error messages. Now it's time to test what you've learned—reading without practicing is the same as not learning at all. Here are three exercises with increasing difficulty. I recommend writing each one yourself.

### Exercise 1: Output Your Name

Modify `hello.cpp` so that the program outputs your name instead of "Hello, C++!". For example, output "大家好啊！我是说的道理！".

### Exercise 2: Read Age and Greet

Write a new program `age.cpp` that uses `std::cin` to read the user's age, then outputs a greeting that includes the age. The expected interaction looks like this:

```text
请输入你的年龄: 24
你好！你今年 24 岁了，是个学生。
```

### Exercise 3: Celsius to Fahrenheit

Write a `convert.cpp` that reads a Celsius temperature, converts it to Fahrenheit, and outputs the result. The conversion formula is `F = C * 9 / 5 + 32`. The expected interaction looks like this:

```text
请输入摄氏温度: 25
25°C = 77°F
```

These three exercises cover all the core knowledge points of this chapter: variable declaration, input and output, and basic arithmetic. If you can complete all three independently, it means you've fully mastered the content of this chapter.

## Summary

In this chapter, we started from scratch, wrote a complete C++ program, and dissected it piece by piece. Let's review the key points: `#include` is used to include standard library feature modules, `int main()` is the program entry point, `std::cout` and `std::cin` handle output and input respectively, `<<` and `>>` are the corresponding data flow operators, and compilation goes through four stages: preprocessing, compilation, assembly, and linking.

More importantly, we learned how to read the compiler's error messages—this is probably the most practical skill in this chapter. In your future learning, you will face compiler errors countless times. Don't be afraid: read the first, fix the first, recompile.

In the next chapter, we'll start learning about C++'s type system—how variables actually store data, what the difference is between integers and floating-point numbers, and why C++ is so obsessed with types. This knowledge is the foundation for writing any meaningful program later on.
