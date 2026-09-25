---
chapter: 0
cpp_standard:
- 11
- 14
- 17
- 20
description: Write, compile, and run your first C++ program, and understand the main function, input/output, and the compilation pipeline
difficulty: beginner
order: 3
platform: host
prerequisites:
- Linux Environment Setup or Windows Environment Setup (pick either one)
reading_time_minutes: 19
tags:
- cpp-modern
- host
- beginner
- 入门
- 基础
title: Your First C++ Program
translation:
  source: documents/vol1-fundamentals/ch00/03-first-program.md
  source_hash: a395f151f4ea391d10b36b3582cebb56f0461503c70d151f18e7eb63bd334d80
  translated_at: '2026-09-25T09:46:58+00:00'
  engine: anthropic
  token_count: 10550
---
# Your First C++ Program

The environment is set up and the compiler is installed—time to get down to real business: writing our first line of C++ code.

Of the languages I've learned, the first lesson in every one was Hello, World. Small as this example is, it happens to string editing, compiling, and running together: when exactly does the text we type in turn into output on the screen? This article walks that road end to end. And I can promise: if we take this little program apart properly, plenty of later concepts will fall into place naturally. So don't be in a hurry to skip it—let's digest it line by line.

## From Scratch — The Skeleton of hello.cpp

Open your favorite editor, create a new file called `hello.cpp`, and type the following code in exactly as it is. Note that I said *type* it, not copy-paste (we often joke that programmers only have three keys—Ctrl, C, V—and let me clarify: don't do that when you're seriously learning. Save that for work you're not interested in but have to do anyway, like writing business code I couldn't care less about.)

Muscle memory really does matter when you're learning to program.

```cpp
// Platform: host | Standard: C++17
#include <iostream>

int main()
{
    std::cout << "Hello, C++!" << std::endl;
    return 0;
}
```

Throughout this article we'll stick with the filename `hello.cpp` and demonstrate with GCC commands under Linux / WSL; the code uses C++17. On native Windows, keep using the toolchain you set up in the previous article—the built program is usually called `hello.exe`, launched in PowerShell with `./hello.exe`. Don't rush to switch build tools yet: let's get this single-file example running first, and later let CMake manage more files for us.

### Including the Header: `#include <iostream>`

This line tells the compiler: we need the "input/output stream" module. You can think of it as pulling a toolkit named iostream out of the toolbox. Inside are `std::cout` (for output) and `std::cin` (for input)—the most basic means we have of interacting with a program. The C++ standard library has a huge number of such toolkits, like `<vector>`, `<string>`, and `<cmath>`; you include whichever one you need.

### The Program Entry Point: `int main()`

This is the designated entry point for a host-environment program like the one in this article. `int` says the function returns an integer as its termination status; 0 means success, and in this article's Linux command line, a non-zero status usually reports failure. In Linux scripts you can read this return value through `$?`, and CI/CD pipelines often depend on it to tell whether the program ran successfully.

### The Function Body: Output and Return

```cpp
std::cout << "Hello, C++!" << std::endl;
return 0;
```

`std::cout` is the standard output stream; when we run it in a terminal the way we do here, its output usually lands on the screen, though it can also be redirected to a file. The `<<` operator is redefined here: its job is to "push" whatever is on its right into the output stream on its left. So `std::cout << "Hello, C++!"` pushes that text onto the screen.

`std::endl` is short for "end line", and it does two things: outputs a newline character, then flushes the buffer—requesting that the contents of the stream buffer be committed to the underlying output device. It does not guarantee the text shows up on the screen, because the output might be redirected. When all you need is the newline, `'\n'` works, and it doesn't flush on every line.

Finally, `return 0` tells the operating system: I finished normally, nothing to worry about.

> Some tutorials or old code will show you `void main()`. **It's wrong. It's wrong. It's wrong! It does not conform to the ISO C++ standard!!!**
>
> For the host environment of this article (C++11–C++23), the return type of `main` must be `int`. Some old compilers might not complain, but that doesn't make it right. Build the habit: always write `int main()`.

You may have noticed the `std::` prefix in front of both `std::cout` and `std::endl`. `std` is short for "standard", and it is a **namespace**.

I'd suggest thinking of it as the brand label on a toolkit. The `cout`, `cin`, and `endl` we use in this article all belong to the `std` namespace, which is what prevents name collisions. If you write your own function called `cout`, it won't fight with the standard library's `std::cout`, because they live in different namespaces. Some tutorials add a line `using namespace std;` at the top and then just write `cout`—it does save typing, but in large projects it easily causes name conflicts, so let's get used to keeping the `std::` prefix from day one.

## Compiling and Running

The code is written; now let's make it run. Open a terminal, navigate to the directory containing `hello.cpp`, and run:

```bash
g++ -std=c++17 -Wall -Wextra hello.cpp -o hello
```

This command does two things: the `g++` compiler compiles `hello.cpp` into an executable, and `-o hello` names the output file `hello` (if you don't specify, the default is `a.out`, a name that means nothing). After a successful compile, a `hello` file appears in the current directory—run it directly:

```bash
./hello
```

Output:

```text
Hello, C++!
```

Nice—your first C++ program has run successfully.

If you already read the environment-setup chapter, you may remember how CMake works. For a small single-file program like this, calling `g++` directly is the fastest. But as the project grows and files multiply, retyping compile commands by hand will drive you up the wall—that's when CMake starts earning its keep. We'll use `g++` here and bring in CMake properly in later chapters.

## What Happens Behind the Scenes — The Compilation Pipeline

If you've been clicking "Run" in an IDE all along, it's easy to conflate building a program with launching it. In reality, the button just chains the commands for us. Now let's take it apart and see where the source code, the intermediate files, and the final output each appear; then when errors show up later, you'll know which stretch of the pipeline to inspect.

We'll walk through the four build stages as the common GCC toolchain presents them. It's a division that helps us understand how the tools work—it is not the C++ standard demanding that implementations produce four separate files on disk.

The whole process boils down to four steps. Step one is **preprocessing**: the compiler handles every directive that starts with `#`—replacing `#include <iostream>` with the actual contents of the iostream header, expanding macro definitions, and processing conditional compilation. Step two is **compilation**: the preprocessed C++ code is translated into assembly language—this is where the compiler performs syntax and type checks, and the syntax errors you write get caught. Step three is **assembly**: the assembly code is translated into machine code, producing an object file (a `.o` file). Step four is **linking**: the object files are combined with the library files needed (for example, the C++ standard library) to produce the final executable.

![GCC build step by step: hello.cpp is preprocessed into hello.ii, compiled into hello.s, assembled into hello.o, and linked into hello; the output only appears after running ./hello](./assets/03-first-program/compilation-pipeline.drawio)

Heh, and there's a little animation below—go take a look!

<Anim id="compilation-pipeline" />

### Run the Commands from the Animation by Hand

In the directory holding `hello.cpp`, run the following commands one after another, deliberately keeping every intermediate file. This flow uses GCC, Linux, and C++17:

```bash
g++ -std=c++17 -E hello.cpp -o hello.ii
g++ -std=c++17 -S hello.ii -o hello.s
g++ -c hello.s -o hello.o
g++ hello.o -o hello
./hello
```

`-E` means stop after preprocessing, `-S` means stop after emitting assembly, and `-c` means stop after producing the object file, before linking. Each command here picks up the previous command's output and keeps processing; the final build command drives the link with `g++`, pulling in the C++ libraries usually required. Libraries can be linked statically or dynamically—it's not the case that all the library code gets copied into `hello`.

Running these commands on Linux with GCC 16.1.1, the final standard output is:

```text
Hello, C++!
```

Now look back at the one-liner `g++ -std=c++17 -Wall -Wextra hello.cpp -o hello`: it chains the build steps together and usually leaves no `hello.ii` or `hello.s` in the current directory. The point of this little experiment is to see the intermediate artifacts clearly—not to make you type four commands by hand for every program from now on.

You might ask: why do I need to know this? Because someday you will meet every kind of compile error—some belong to the preprocessing stage (a header can't be found), some to the compilation stage (syntax errors, type mismatches), and some to the linking stage (duplicate definitions, missing symbols). Knowing which stage the error lives in gives your troubleshooting a direction.

> When the compiler reports errors, **always read the first error message**. Many beginners habitually start from the last one, but C++ compilers have a "cascading diagnostics" trait—**one error can trigger dozens of "false positive" errors after it. Fix the first one, and the rest may vanish on their own. So build the habit: read the first, fix the first, recompile, look again.**

## The Pitfalls We've Stepped In — Common Compilation Errors

Writing correct code alone isn't enough—we also have to learn to read error messages. Below we'll deliberately create a few classic mistakes and see what the compiler says. The diagnostics come from GCC 16.1.1, using C++17 with English diagnostic messages; change only one thing at a time, and after each experiment restore the correct program from the beginning before starting the next one.

### Forgetting the Semicolon

Remove the semicolon from `hello.cpp`:

```cpp
#include <iostream>

int main()
{
    std::cout << "Hello, C++!" << std::endl  // missing semicolon here
    return 0;
}
```

Compile it:

```bash
g++ -std=c++17 -Wall -Wextra hello.cpp -o hello
```

```text
hello.cpp: In function 'int main()':
hello.cpp:5:44: error: expected ';' before 'return'
    5 |     std::cout << "Hello, C++!" << std::endl  // missing semicolon here
      |                                            ^
      |                                            ;
    6 |     return 0;
      |     ~~~~~~
```

The compiler is telling you: before `return`, it expected to see a semicolon. The reported line numbers and exact layout shift with your source and GCC version; the key is to go back and check the end of the previous output statement. Sometimes the marker lands on the next line—don't stare only at that spot.

### Forgetting to Include the Header File

Delete the `#include <iostream>` line and compile again:

```text
hello.cpp: In function 'int main()':
hello.cpp:5:10: error: 'cout' is not a member of 'std'
    5 |     std::cout << "Hello, C++!" << std::endl;
      |          ^~~~
hello.cpp:1:1: note: 'std::cout' is defined in header '<iostream>'; this is probably fixable by adding '#include <iostream>'
  +++ |+#include <iostream>
    1 | // Platform: host | Standard: C++17
hello.cpp:5:40: error: 'endl' is not a member of 'std'
    5 |     std::cout << "Hello, C++!" << std::endl;
      |                                        ^~~~
hello.cpp:1:1: note: 'std::endl' is defined in header '<ostream>'; this is probably fixable by adding '#include <ostream>'
  +++ |+#include <ostream>
    1 | // Platform: host | Standard: C++17
```

The compiler says "cout is not a member of std"—because it has no idea what `std::cout` is; nobody ever told it. The fix is to add `#include <iostream>` back. Some GCC versions will even tell you directly which header is missing—we just follow that lead.

### Typos

Write `std::cout` as `std::couth`:

```text
hello.cpp: In function 'int main()':
hello.cpp:6:10: error: 'couth' is not a member of 'std'; did you mean 'cout'?
    6 |     std::couth << "Hello, C++!" << std::endl;
      |          ^~~~~
      |          cout
```

The error message is blunt—`couth` is not a member of `std`. Just check the spelling carefully. This kind of mistake is especially common at the beginner stage: `cout` and `cin` get typed as `couth` or `cim` and the like; a few more rounds of typing and you'll know them cold.

> If you're on GCC, **it's recommended to add the `-Wall -Wextra` options when compiling**, i.e. `g++ -std=c++17 -Wall -Wextra hello.cpp -o hello`. These two options turn on a wealth of warnings—warnings don't stop the compile, but they often point at latent problems. **Treating warnings as errors is step one on the road to becoming a proper C++ programmer.**

## One Step Further — Talking with the Program

Output alone isn't enough—let's give the program a way to accept input. Create a new file called `calc.cpp` and build a simple addition calculator.

We'll write the skeleton first and fill it in step by step. First, we need to read two numbers from the user, so we turn to `std::cin`, `std::cout`'s trusty partner.

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
g++ -std=c++17 -Wall -Wextra calc.cpp -o calc
./calc
```

```text
请输入第一个数字: 1
请输入第二个数字: 2
1 + 2 = 3
```

A few things here deserve attention. `int a = 0;` declares a variable of integer type and initializes it to 0. The `>>` operator in `std::cin >> a;` points the opposite way from `<<`—it "extracts" data from the input stream and puts it into the variable `a`. Read `<<` as "push out" (output) and `>>` as "pull in" (input); the direction of the arrow is the direction the data flows.

The line `std::cout << a << " + " << b << " = " << sum << std::endl;` chains several `<<` operators together, executed left to right: first the value of `a`, then the string `" + "`, then the value of `b`, and so on. This "chained" style is extremely common in C++—you'll get used to it.

For the variable declaration we wrote `int a = 0;` rather than `int a;`, and that's deliberate. For an ordinary local `int` like the ones here, a bare `int a;` does not give it a determined initial value; in this article's C++17, reading such an indeterminate value before assigning it is undefined behavior—it is not "just some random number you happened to read". True, `std::cin` will write into it when input succeeds, but building the "initialize at declaration" habit matters a lot: it keeps you away from a whole class of hard-to-debug problems.

## Getting std:: Straight — Namespaces and `::`

Back in the skeleton we dropped one line—"std is a namespace"—and moved right along. By now you have written `std::cout` and `std::cin` all the way down the page, so it's time to explain this prefix properly: every volume from here on will have you dealing with it, and once you write projects of your own, namespaces are the first tool for organizing names.

Start with the problem it solves. In C projects, name collisions are a chronic pain: link three third-party libraries, each with its own `init()`, and the link stage immediately sprays a pile of `multiple definition` errors. The C convention is to prefix names—`sensor_init()`, `uart_init()`, `display_init()`. It works, but it's long and unwieldy to type, and it still can't stop two libraries from both naming theirs `network_buffer_create()` and crashing into each other. C++ namespaces solve this at the language level: essentially, the compiler automatically gives every name a "surname" at compile time. The substitution happens during compilation, with **zero runtime overhead**—the final generated symbols are no different from hand-written long prefixes; it's just that you don't have to roll that long, ugly qualified name yourself.

Definitions use the `namespace` keyword; let's use a sensor module as the example:

```cpp
// sensor.hpp — declarations
namespace sensor {
    const int MAX_READINGS = 100;

    struct Reading {
        float temperature;
        float humidity;
    };

    void init();
    Reading get_reading();
}

// sensor.cpp — the implementation goes back into the same namespace; the compiler merges the two automatically
namespace sensor {
    void init()
    {
        // initialize the sensor hardware
    }

    Reading get_reading()
    {
        Reading r{};
        // read the sensor data
        return r;
    }
}
```

One namespace can be spread across multiple files: declarations in the header, implementation in the `.cpp`, each wrapped in its own `namespace sensor { ... }`. When using it, there are three styles, from most explicit to most relaxed:

```cpp
int main()
{
    // Style one: fully qualified names — more typing, but never ambiguous
    sensor::init();
    sensor::Reading data = sensor::get_reading();

    // Style two: a using declaration — brings in only specific names
    using sensor::Reading;
    Reading data2 = sensor::get_reading();

    // Style three: a using directive — pours the whole namespace in
    using namespace sensor;
    init();
    Reading data3 = get_reading();

    return 0;
}
```

If you use style three inside a function body in a `.cpp` file (say `using namespace std`), most people won't say a word. But one rule is non-negotiable: **never write `using namespace` in a header file**. It cannot be undone—once a header globally drags in a namespace, every piece of code that `#include`s it is forced to accept that namespace's full set of symbols, without knowing a thing about it. When two libraries' same-named symbols collide in the last place you'd think to look, the ambiguity errors will have you questioning your life choices.

Namespaces can nest, and mirroring your module hierarchy with namespace levels is natural—say, a hardware abstraction layer:

```cpp
namespace hardware {
    namespace gpio {
        enum PinMode { INPUT, OUTPUT, ALTERNATE };
        void set_mode(int pin, PinMode mode);
    }
    namespace uart {
        void init(int baudrate);
        void send(const char* data);
    }
}

// Using it
hardware::gpio::set_mode(5, hardware::gpio::OUTPUT);
hardware::uart::init(115200);

// Too long? Give it an alias
namespace hw = hardware;
hw::gpio::set_mode(5, hw::gpio::OUTPUT);
```

An alias is only visible in the current scope, so different functions can give the same namespace different short names without stepping on each other. C++17 also offers an even handier nesting syntax:

```cpp
// Since C++17; equivalent to the two-level nesting above
namespace hardware::gpio {
    void set_mode(int pin, PinMode mode);
}
```

Here's a detail we actually ran into: by the standard this is a C++17 capability, but our local GCC 16 accepts it even under `-std=c++14` (treating it as an extension); only adding `-pedantic-errors` stops it, with the error message `nested namespace definitions only available with '-std=c++17' or '-std=gnu++17'`. So don't use "does it compile" as your yardstick—if your project is pinned to C++11/14, write the nesting one level at a time.

There's also a practical feature people easily overlook: the **anonymous namespace**. Things defined inside `namespace { ... }` are visible only to the current `.cpp` file—the effect is file-level visibility like `static` in C, but it covers far more:

```cpp
namespace {
    const int BUFFER_SIZE = 256;

    void internal_helper()
    {
        // internal helper; invisible to other translation units
    }
}
```

`static` can't even wrap a class definition—`static class Foo { ... };` gets you, in GCC's own words, `a storage class can only be specified for objects and functions` (we tested it); the anonymous namespace happily wraps classes, structs, enums, and templates alike. So for new code, prefer anonymous namespaces across the board; if you spot `static` in old code, there's no rush to change it.

Finally, let's nail down `::` itself. Its semantics fit in one sentence: **take the name on the right from the scope on the left**. The left side can be a namespace (`math::PI`), a class (`UARTConfig::DEFAULT_BAUDRATE`, a static class member—see the chapter on classes), or even empty—`::value` means the `value` in the global scope, your appeal when a local variable shadows a same-named global:

```cpp
int value = 100;  // global

void function()
{
    int value = 50;  // local; shadows the global value
    printf("Local: %d\n", value);     // 50
    printf("Global: %d\n", ::value);  // 100
}
```

In C, once a local shadows a global, the function can never reach the global version again; C++ patches that hole with `::`. Of course, the best practice is still to avoid same-name shadowing in the first place—`::` solves the syntax problem, not readability.

<OnlineCompilerDemo
  title="Namespaces and Scope Resolution"
  source-path="code/examples/vol1/14_namespace_reference.cpp"
  description="Run online and observe how namespace nesting, reference parameters, and :: scope resolution actually behave."
  allow-run
/>

## Try It Yourself

At this point we can write code, compile it, run it, and read error messages. Now comes the test of what you've learned—reading without practicing is the same as not learning. Here are three exercises in rising difficulty; I suggest writing each one by hand.

### Exercise 1: Print Your Name

Modify `hello.cpp` so the program prints your name instead of "Hello, C++!". For example, print "大家好啊！我是说的道理！".

### Exercise 2: Read an Age and Greet

Write a new program `age.cpp` that reads the user's age with `std::cin` and then prints a greeting that includes the age. The expected interaction:

```text
请输入你的年龄: 24
你好！你今年 24 岁了，是个学生。
```

### Exercise 3: Celsius to Fahrenheit

Write a `convert.cpp` that reads a Celsius temperature, converts it to Fahrenheit, and prints the result. The conversion formula is `F = C * 9 / 5 + 32`. Expected interaction:

```text
请输入摄氏温度: 25
25°C = 77°F
```

These three exercises cover several core points of this chapter: variable declaration, input/output, and basic arithmetic. If you can finish all three on your own, try explaining in your own words the difference between "compiled successfully" and "the program ran".

## Run Online

Try editing and running this code online; change the output and see what happens:

<OnlineCompilerDemo
  title="Your First C++ Program: Hello World and Simple Calculation"
  source-path="code/examples/vol1/01_first_program.cpp"
  description="Edit and run your first C++ program in the browser and observe the output."
  allow-run
/>

## References

- [GCC: Overall Options](https://gcc.gnu.org/onlinedocs/gcc/Overall-Options.html): what `-E`, `-S`, `-c`, and `-o` mean.
- [GCC: Header Search Path](https://gcc.gnu.org/onlinedocs/cpp/Search-Path.html): the difference between the angle-bracket and double-quote forms.
- [cppreference: the main function](https://en.cppreference.com/w/cpp/language/main_function.html): this article discusses the host environment; the examples use C++17.
- [cppreference: std::endl](https://en.cppreference.com/w/cpp/io/manip/endl.html): newline and stream flushing.
- [cppreference: default initialization](https://en.cppreference.com/w/cpp/language/default_initialization.html): the remarks on the local `int` in this article are scoped to C++17.
