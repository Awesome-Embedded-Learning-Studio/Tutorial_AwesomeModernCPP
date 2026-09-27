---
title: 'Null Pointer Dereference: The Crash That Hides Nothing'
description: 'find_value returns nullptr when nothing matches, and the caller dereferences *result on the spot — exit 139 (SIGSEGV) on Linux, GDB stopping right on the crashing line, print showing (int*)0x0, ASan reporting SEGV on the zero page. The null pointer is one of the few flavors of UB that "always crashes": the OS maps the zero page inaccessible and the MMU intercepts on the spot; yet a member function called through this==nullptr, or a machine without an MMU, can still play innocent. Fixes run from plain null checks all the way to std::optional bringing "might be absent" into the type system.'
chapter: 15
order: 1
difficulty: beginner
platform: host
reading_time_minutes: 10
tags:
  - host
  - cpp-modern
  - beginner
  - 内存管理
  - optional
  - 类型安全
prerequisites:
  - Pointer Basics
related:
  - 'Use-After-Free: The Pointer Outlives the Memory'
cpp_standard: [11, 17]
translation:
  source: documents/crash-lab/a-memory-safety/01-null-deref.md
  source_hash: cdc694429bc131b283a3a3973c9e8312280f55975c8215c77db7145071d28726
  translated_at: '2026-09-27T03:03:24+00:00'
  engine: anthropic
  token_count: 3400
---

# Every Endeavor Has a Bootstrap — Good News, We Have One Too: Null Pointer Dereference

If you ask me which damn crash is the easiest to investigate and the quickest to fix, I vote for the null pointer dereference. The project I work on has quite a few users, so crash dumps fly in every single day. When I open one of those Windows dmp files and see a low-address dereference, I go straight to reverse-mapping the pdb to see which function was up to no good. Usually adding a null check settles it... right? Come back — of course it is not necessarily so, but it really can keep your software from dying on the spot.

> On whether software should crash at all, everyone has their own view. Some say crashing is fine — at least it does not hide the problem; others say do not crash — at least it looks better. We are not joining that debate here; it depends on your scenario. If production software crashes, you are in for some busy days (getting chewed out by the boss a couple of times, or your users abandoning the software outright — both are delightful).

Digressed — back on track. A null pointer access — **or rather, a low-address access** — is quietly hinting at the real problem behind it: you have just touched an object you **deliberately** did not initialize. Alright, let us go.

## First, Let Us Manufacture One

Let us not start with anything complicated — complicated cases would probably drag up your own nightmare memories of hunting crashes. Picture this: you are the developer behind module A, and the owner of module B comes to you — hey big bro, we have a bit of business here, let us align on the interface and use your code. The two of you happily settle the interface, and you say you will provide a `find_value`:

```cpp
int* find_value(int* arr, int size, int target) {
    for (int i = 0; i < size; i++) {
        if (arr[i] == target)
            return &arr[i];
    }
    return nullptr;  // not found, return a null pointer
}
```

Unfortunately, the habits were not great: nobody ever said what to do if the value is not found. Your integration colleague, presumably dizzy from overtime, just went ahead and used it like this:

```cpp

int main() {
    int check_value = ... // your good colleague reads the user's input
    int data[] = {10, 20, 30, 40, 50}; // your backend buddy tells you these come back
    int* result = find_value(data, 5, 999);   // shit, the user's cute little trick input: 999, sorry, does not exist — enjoy your nullptr
    printf("*result = %d\n", *result);        // ← Boom! dereferenced without checking
}
```

The reviewer was presumably also dizzy from overtime (guess why I keep mentioning overtime), and it muddled through review like that. Things looked fine at launch — then the production alerts suddenly fired: crash rate spiking. Congratulations, you are about to get roasted.

The code above is a bit contrived, but plenty of us have genuinely written something like it ourselves — thankfully, in my case the feedback came from QA, not from users. On my Linux box (GCC 16.1.1), it dies very cleanly:

```text
find_value returned: (nil)
exit code: 139
```

139 = 128 + 11, and signal 11 is SIGSEGV (segmentation fault). The same code on Windows / MSVC reaches the same ending with different wording: `exit code: -1073741819(0xC0000005 = STATUS_ACCESS_VIOLATION)`. Different platforms, different signal names, but it is all the same event: **the CPU went for address 0 and got stopped.** A low address is clearly not a legitimate object in any sense; your MMU goes "heh, little buddy, what exactly are you trying to access? Get lost!" — and the process gets killed.

## Why It Crashes So Honestly

`nullptr` is simply address 0. If you come from the C world, knowing it is the same idea as NULL is enough.

When modern operating systems lay out a process's address space, they deliberately mark the entire page around 0 (commonly called the "zero page") as inaccessible — precisely to guard against this kind of slip. That is why the chain from a null dereference to death is pitifully short: the moment `*result` is evaluated, the CPU goes for address 0; the MMU checks the page table, the page has no permissions, and a hardware exception fires on the spot; the OS converts it into a signal and sends it back — SIGSEGV on Linux, 0xC0000005 on Windows — and the process terminates right away.

No Schrödinger, no delayed detonation. Whichever line holds the error is the line that crashes.

## Catching It: Three Tools, One Verdict

Let us get GDB up and running! It stops right at the scene of the crime, and while we are there we can confirm that the "weapon" really was a null pointer:

```text
(gdb) run
Program received signal SIGSEGV, Segmentation fault.
0x0000555555555245 in main () at crash.cpp:27
27     printf("*result = %d  <-- null deref!\n", *result);
(gdb) print result
$1 = (int *) 0x0
(gdb) bt
#0  0x0000555555555245 in main () at crash.cpp:27
```

`print result` gives `(int *) 0x0` — hard evidence. Run it with ASan on, and the report adds one extra line in plain human words:

```text
==28078==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000000
==28078==The signal is caused by a READ memory access.
==28078==Hint: address points to the zero page.
```

`Hint: address points to the zero page` — ASan is telling you outright: this is a null pointer. Truth be told, the null pointer case hardly needs these heavy weapons; it crashes clearly, and reading the stack is enough. But when we reach the dangling pointer case, you will understand: the very same SIGSEGV can be worlds apart in how hard it is to hunt down.

## And of Course, We Have Embedded Content Too — With Some Special Cases

| Scenario                                           | Behavior                | Why                                                                 |
| -------------------------------------------------- | ----------------------- | ------------------------------------------------------------------- |
| Dereferencing `nullptr` directly                   | Crashes, basically always | The zero page is inaccessible                                     |
| `p->func()` with `p == nullptr`, member function touches no member data | Very likely survives | The member function never dereferences `this`; it is just a plain call |
| Bare metal without an MMU (MCU)                    | No crash; reads all zeros | Address 0 is real, physically present Flash/ROM (in the STM32F103 memory map: the interrupt vectors, Reset_Handler) |

The second row is worth unpacking. I wrote a minimal two-function verification for it (call, through a null pointer, a function that touches no members, then one that does):

```text
safe_func called, this=(nil) (no member access)
survived safe_func
exit: 139
```

To the compiler, `p->member_func()` is really `member_func(p)`: `this=(nil)` gets passed in, and as long as the function body never touches a member, the null pointer slips through just like that — until the next line performs a member access and the landmine finally goes off. The third row matters directly to this site's embedded readers: on a Cortex-M, address 0 is the vector table, so dereferencing a "null pointer" reads the value of the initial stack pointer. The program does not crash; it is silently wrong — a classic source of "it runs, but the bugs are eerie" in embedded work.

## Curing the Root: Writing "Possibly Absent" into the Type

The most rudimentary fix is a null check, but null checks run on self-discipline — you remember this time and forget the next. The road that truly cures the root is encoding "there may be a value, or there may not" into the type system, and C++17's answer is `std::optional`:

```cpp
// Returning a pointer: the caller may forget to check, and it blows up at runtime
int* find_value(int* arr, int size, int target);

// Returning optional: the type itself is the reminder that "there may be nothing here"
std::optional<int> find_value(const int* arr, int size, int target);

auto result = find_value(data, 5, 999);
if (result.has_value()) {          // checking is part of the flow
    printf("%d\n", *result);
}
int val = result.value_or(-1);     // or just give a default value
```

See that? Write the hint into the type system, and everything gets better! The culprit in the next case will not be nearly this polite: **after the free, the pointer is still alive**. And if you have accidentally guessed it already — yes, it is the big-name one that has made my scalp tingle countless times: Use After Free!

## References

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [C++ Core Guidelines: Don't pass nullptr](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Ri-null)
