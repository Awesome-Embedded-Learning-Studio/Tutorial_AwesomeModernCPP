---
title: "Null Pointer Dereference: The Crash That Hides Nothing"
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
  - "Vol.1 ch04: Pointer Basics"
related:
  - "Use-After-Free: The Pointer Outlives the Memory"
cpp_standard: [11, 17]
description: "find_value returns nullptr when nothing matches, and the caller dereferences *result right away—exit 139 (SIGSEGV) on Linux, GDB stops on the crashing line and prints (int*)0x0, ASan reports SEGV on zero page. The null pointer is one of the few 'guaranteed to crash' UBs: the OS maps the zero page inaccessible and the MMU intercepts on the spot; yet a member-function call on this==nullptr, or a machine without an MMU, can still play innocent. Fixes range from plain null checks to std::optional bringing 'might be absent' into the type system."
translation:
  source: documents/crash-lab/a-memory-safety/01-null-deref.md
  source_hash: cdc694429bc131b283a3a3973c9e8312280f55975c8215c77db7145071d28726
  translated_at: '2026-08-28T00:00:00+00:00'
  engine: manual
---

# Every Endeavor Has a Bootstrap — Good News, We Have One Too: Null Pointer Dereference

If you ask me which damn crash is the easiest to hunt down and the fastest to fix, I'll say null pointer dereference. The project I work on has quite a few users, so crash dumps fly in every day. When I see a low-address dereference in a Windows dump, I go straight to reverse-mapping the PDB to find which function was being cute. Add a null check, done... ish? Wait, come back—of course that's not the whole story, but it does at least keep your software from dying on the spot.

> On whether it should crash at all, everyone has opinions. Some say crashing is fine—at least it doesn't hide the problem. Others say don't crash, at least it looks better. I'm not joining that fight here; pick by your scenario. When production software goes down, you'll be busy enough either way—two rounds of getting chewed out by your boss, or users quietly leaving. Both are great.

Digressed. Back on track. A null pointer access—**or rather, a low-address access**—is quietly hinting at the real problem behind it: you just touched an object you **deliberately** never initialized. Alright, let's go.

## First, Let's Manufacture One

Let's not start with anything fancy—fancy would just drag up your own crash-debugging nightmares. Say you're the developer of module A, and the owner of module B comes to you—hey big bro, got a little feature here, let's align on the interface and use your code. You shake hands happily, and you say you'll provide a `find_value`:

```cpp
int* find_value(int* arr, int size, int target) {
    for (int i = 0; i < size; i++) {
        if (arr[i] == target)
            return &arr[i];
    }
    return nullptr;  // not found, return null
}
```

Too bad nobody's habits were any good: nobody ever said what happens when the value isn't found. Your integration colleague, presumably dizzy from overtime, used it like this:

```cpp

int main() {
    int check_value = ... // your good colleague reads the user's input
    int data[] = {10, 20, 30, 40, 50}; // your backend buddy says these come back
    int* result = find_value(data, 5, 999);   // shit, the user's cute little input: 999, sorry, doesn't exist — congrats on the nullptr
    printf("*result = %d\n", *result);        // ← Boom! dereferenced without checking
}
```

The reviewer was presumably dizzy from overtime too (guess why I keep saying overtime), and it slipped through. Things ran fine after launch—for a while. Then the online alerts fired: crash rate spiking. Congrats, you're about to get roasted.

The code above is contrived, but most of us have genuinely written something like it. In my case the report thankfully came from QA, not from users. On my Linux box (GCC 16.1.1), it dies very cleanly:

```text
find_value returned: (nil)
exit code: 139
```

139 = 128 + 11; signal 11 is SIGSEGV (segmentation fault). The same code on Windows / MSVC meets the same end with different paperwork: `exit code: -1073741819(0xC0000005 = STATUS_ACCESS_VIOLATION)`. Different platforms, different signal names, but it's the same event: **the CPU went for address 0 and got stopped.** A low address is obviously not a legitimate object in any sense. Your MMU: heh, little buddy, what exactly are you touching? Get out! And the process gets put down.

## Why It Crashes So Honestly

`nullptr` is address 0. If you come from C, knowing it's the same as NULL is enough.

When laying out a process's address space, modern operating systems deliberately mark the entire page around 0 (often called the "zero page") as inaccessible—precisely to guard against this slip. That's why the chain from dereference to death is laughably short: the moment `*result` gets evaluated, the CPU goes for address 0; the MMU checks the page table, finds no permission for that page, and a hardware exception fires on the spot; the OS catches it and hands it back as a signal—SIGSEGV on Linux, 0xC0000005 on Windows—and the process dies right there.

No Schrödinger, no delayed detonation. Whichever line is wrong is the line it dies on.

## Catching It: Three Tools, One Story

So—let's get GDB up and running! It stops right at the crime scene, and you can verify on the spot that the "weapon" really is a null pointer:

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

`print result` gives `(int *) 0x0`. Hard evidence. Run it under ASan, and the report adds one line of plain human speech:

```text
==28078==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000000
==28078==The signal is caused by a READ memory access.
==28078==Hint: address points to the zero page.
```

`Hint: address points to the zero page`—ASan is telling you outright: this is a null pointer. Honestly, the null-pointer case hardly needs heavy weaponry like this. It dies clearly; read the stack and you're done. But when we get to the dangling-pointer case, you'll understand: same SIGSEGV, worlds apart in how hard it is to hunt down.

## And of Course, the Embedded Angle, Where Things Get Weird

| Scenario                                           | Behavior        | Why                                                                  |
| -------------------------------------------------- | --------------- | -------------------------------------------------------------------- |
| Dereferencing `nullptr` directly                   | Crashes, basically always | The zero page is inaccessible                              |
| `p->func()` with `p == nullptr`, member touches no data | Very likely survives | The member function never dereferences `this`; it's just a plain call |
| Bare metal without an MMU (MCU)                    | No crash; reads zeros | Address 0 is real, existing Flash/ROM (on the STM32F103 memory map: the interrupt vector table and Reset_Handler) |

Row two deserves a closer look. I wrote a two-function minimal verification for it (call a member function that touches nothing on a null pointer, then one that touches a member):

```text
safe_func called, this=(nil) (no member access)
survived safe_func
exit: 139
```

To the compiler, `p->member_func()` is `member_func(p)`: `this=(nil)` walks in as an argument, and as long as the body never touches a member, the null pointer slips right through—until the next line performs a member access and the mine goes off. Row three is directly relevant to this site's embedded readers: on a Cortex-M, address 0 is the vector table, so "dereferencing a null pointer" reads the value of the initial stack pointer. The program doesn't crash; it's just silently wrong—a classic source of "it runs, but with weird bugs" in embedded work.

## The Real Fix: Write "Might Be Absent" Into the Type

The most basic fix is a null check, but null checks run on discipline—you remember this time, you forget next time. The real cure encodes "there may or may not be a value" into the type system, and C++17's answer is `std::optional`:

```cpp
// Returning a pointer: the caller may forget to check, and it blows up at runtime
int* find_value(int* arr, int size, int target);

// Returning optional: the type itself is the reminder that "there may be nothing here"
std::optional<int> find_value(const int* arr, int size, int target);

auto result = find_value(data, 5, 999);
if (result.has_value()) {          // checking becomes part of the flow
    printf("%d\n", *result);
}
int val = result.value_or(-1);     // or just provide a default
```

See? Put the hint into the type system and everything gets better. The culprit in the next case won't be nearly this polite: **the memory is freed, but the pointer lives on.** And I suspect you've already guessed it—it's the big-name one that's made my scalp numb more times than I can count: Use-After-Free!

## References

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [C++ Core Guidelines: Don't pass nullptr](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Ri-null)
