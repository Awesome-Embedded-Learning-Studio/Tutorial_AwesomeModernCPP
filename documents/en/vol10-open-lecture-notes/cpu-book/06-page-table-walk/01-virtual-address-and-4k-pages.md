---
title: "Virtual Addresses and 4 KiB Pages: ASLR and the Arithmetic of the Flat Table"
chapter: 6
order: 1
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "Starts from the same variable printing a different address on every run, confirms that pointers hold virtual addresses, watches memory get cut into 4 KiB pages and the low 12 bits stay out of the translation, then works out why a single flat table would cost 512 GiB"
reading_time_minutes: 7
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/06-page-table-walk/01-virtual-address-and-4k-pages.md
  source_hash: 2943df62607b2c69c2de2c90510ad548454fa818a4a480256ff6e4a695e7e7d9
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Virtual Addresses and 4 KiB Pages: ASLR and the Arithmetic of the Flat Table

In the [previous piece](../05-classes-in-assembly/01-this-pointer-and-member-offsets.md) we put classes back into assembly and went through this, the constructors, and the vtable one at a time, and the closing piece of that topic already pointed at where we were headed next: over to the memory side, at the page tables. This piece makes good on that. Every address we have met across the first five pieces, whether the rbp of a stack frame back in 02 or the vptr at the head of an object in 05, carries the same identity: it is a virtual address. The number inside a pointer has never been the real location on the RAM. Getting to the real location takes a translation: we hand the pointer value to the CPU, it goes and looks through the page tables, and only after four tables have run their relay does the instruction land on a page of physical memory. This piece walks that translation from one end to the other: how an address is cut up by page, why one big table cannot carry the job, how four tables relay, what happens when a page walk fails, and what comes to the rescue when the walk is too slow.

Every example and experiment in this piece is original to this site. Every /proc dump and every line of terminal output you see below comes verbatim from real runs on my machine.

As usual, the environment comes first. This piece looks different from the first five: no assembly this time, system interfaces in its place, and besides g++ and gdb the ones doing the work are a set of /proc files. Versions first, along with the settings that bear directly on this piece:

```text
$ g++ --version
g++ (GCC) 16.2.1 20260810
Copyright (C) 2026 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

$ uname -a
Linux DESKTOP-65DBAA7 6.18.33.2-microsoft-standard-WSL2 #1 SMP PREEMPT_DYNAMIC Thu Jun 18 21:54:43 UTC 2026 x86_64 GNU/Linux

$ id -u
1000

$ whoami
charliechen
```

Then four settings. The first governs address randomization, the second transparent huge pages, and the last two the width of addresses:

```text
$ cat /proc/sys/kernel/randomize_va_space
2
```

```text
$ cat /sys/kernel/mm/transparent_hugepage/enabled
always [madvise] never
```

```text
$ grep -i 'address sizes' /proc/cpuinfo | sort | uniq -c
     16 address sizes	: 48 bits physical, 48 bits virtual
```

```text
$ grep -o 'la57' /proc/cpuinfo | sort -u      # (no output)
$ grep -m1 'model name' /proc/cpuinfo
model name	: AMD Ryzen 7 9700X 8-Core Processor
```

Let's translate them into plain words. `randomize_va_space` is 2, so address randomization is fully on. Transparent huge pages run in madvise mode, which means anonymous memory that ordinary code mmaps stays on the 4 KiB base page and never quietly gets swapped for a 2 MiB huge page; this is what keeps the numbers in the RSS experiment later in the topic clean. All 16 logical CPUs in cpuinfo report the same address sizes, 48 bits physical and 48 bits virtual. The `la57` grep came back empty, so this 9700X does not advertise the five-level page table capability with 57-bit linear addresses. Four-level paging is the only shape this machine has, no five-level variant exists, and when we take an address apart later there is only one kind to take apart. The machine has 52 GiB of memory in total, 43 GiB of it available as I write this, and the gdb version is 17.2.

## The Same Variable, a Different Address on Every Run

Let's start from the most familiar line of code there is: print the address of a local variable. The program also prints what malloc returned and the address of main itself, so the three addresses stand in for the stack, the heap, and the code segment:

```cpp
// f1_aslr.cpp -- print the addresses of a stack local, malloc, and the code segment, to watch ASLR
#include <cstdio>
#include <cstdlib>

int main() {
    int stack_var = 42;
    void* heap_ptr = malloc(64);
    printf("stack local : %p\n", (void*)&stack_var);
    printf("malloc      : %p\n", heap_ptr);
    printf("code (main) : %p\n", (void*)&main);
    free(heap_ptr);
    return 0;
}
```

```text
$ g++ -O0 -g -Wall -Wextra -o f1_aslr f1_aslr.cpp
$ ./f1_aslr     (x6)
stack local : 0x7ffd89c0f6ac
malloc      : 0x5725120bf020
code (main) : 0x5724fca5a169
---
stack local : 0x7ffc83e62ffc
malloc      : 0x5eecdf603020
code (main) : 0x5eecbe2b2169
---
stack local : 0x7ffddd90286c
malloc      : 0x592d46b1b020
code (main) : 0x592d23b7b169
---
stack local : 0x7ffdf5620b8c
malloc      : 0x5aeb577f3020
code (main) : 0x5aeb4e6b6169
---
stack local : 0x7fff885ed72c
malloc      : 0x56f9e37e3020
code (main) : 0x56f9e287d169
---
stack local : 0x7ffc826d8b3c
malloc      : 0x60e1c5ad6020
code (main) : 0x60e1931e2169
---
```

Six runs, and all three addresses changed every time. Each kind of address has its own territory to roam: the stack address always starts with `0x7ff...`, while the malloc return value and the code segment crowd into the `0x55..` to `0x61..` range. Look a little closer and you can see that within a single run, the code and malloc prefixes share an origin: in the first run `0x5724f...` next to `0x57251...`, in the second `0x5eec...` next to `0x5eec...`. The code segment moves with the base address of the program image, and malloc's starting point hugs that same base. The background here is that GCC on Linux builds PIE by default (position-independent executable: a program image that can run from any base address), and the kernel shifts that base by a random amount when it loads the program.

This mechanism of shifting addresses at random is called ASLR (address space layout randomization), and it exists for security's sake: if an attacker could know the exact addresses of the stack, the heap, and the code ahead of time, plenty of attack techniques could aim straight at fixed targets. When the addresses move on every run, a plan aimed at fixed addresses finds nothing to aim at. Here is the control group to prove it: this time randomization is turned off with `setarch -R`, six runs in a row:

```text
$ setarch $(uname -m) -R ./f1_aslr   (x6, the output is identical every time)
stack local : 0x7fffffffd0dc
malloc      : 0x55555556b020
code (main) : 0x555555555169
---
(the next 5 runs match the above, word for word)
---
```

All six runs matched to the last character, and `0x555555555169` is exactly the classic load base everybody knows for PIE programs with randomization off (it is usually consistent across distros, though I won't treat that as a promise). Same variables, same program; the only thing that changed is where they land.

So what address is `%p` actually printing? A virtual address. Every process gets a complete address space of its own, with the stack, the heap, the code segment, and the shared libraries each claiming a piece, and the `0x7ffd89c0f6ac` that process A sees and the same string of digits seen by process B correspond to two unrelated physical locations. Those `0x7fff` stack addresses we met in the gdb sessions of piece 02 live in this region. Handing each process an independent set of numbers is the base isolation is built on, and translating those numbers into real locations on the RAM is the page table's job.

## Memory Is Cut into 4 KiB Pages, and the Low 12 Bits Are Not Translated

The value of a pointer looks byte-addressed, but the unit of translation is not the single byte. What gets translated is a whole page. Physical memory and the virtual address space are both cut into pages of the same size, and translation happens page by page: the high bits of the virtual address say which page this is, and the low bits say how far into the page. Let's confirm the page size first:

```text
$ getconf PAGESIZE
4096
```

The page is 4096 bytes, the 4 KiB we already know, which is 2 to the 12th power. Twelve low bits are exactly enough to enumerate any offset within a page, so the low 12 bits of a virtual address are the offset within the page. One observation backs this up: the kernel hands out addresses a whole page at a time, so the start of any new mapping has to be page-aligned, and the low 12 bits we see have to be all zeros. Let's write a program that mmaps eight times in a row, returning each page the moment it is done with it, and look at the addresses it gets back:

```cpp
// f2_pagesize.cpp -- the page size and the 12-bit alignment of the addresses mmap returns
#include <cstdio>
#include <sys/mman.h>
#include <unistd.h>

int main() {
    long ps = sysconf(_SC_PAGESIZE);
    printf("sysconf(_SC_PAGESIZE) = %ld\n", ps);

    for (int i = 0; i < 8; i++) {
        void* p = mmap(nullptr, ps, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) { perror("mmap"); return 1; }
        unsigned long addr = (unsigned long)p;
        printf("mmap #%d -> 0x%012lx   low 12 bits = 0x%03lx   page-aligned: %s\n",
               i, addr, addr & 0xfff, (addr & 0xfff) == 0 ? "yes" : "NO");
        munmap(p, ps);
    }
    return 0;
}
```

```text
$ ./f2_pagesize     (run 1)
sysconf(_SC_PAGESIZE) = 4096
mmap #0 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
mmap #1 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
mmap #2 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
mmap #3 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
mmap #4 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
mmap #5 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
mmap #6 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
mmap #7 -> 0x7ab12c6d8000   low 12 bits = 0x000   page-aligned: yes
=== run 2 ===
sysconf(_SC_PAGESIZE) = 4096
mmap #0 -> 0x77251c360000   low 12 bits = 0x000   page-aligned: yes
(run 2's #1..#7 match #0, all 0x77251c360000, low 12 bits = 0x000)
=== run 3 ===
sysconf(_SC_PAGESIZE) = 4096
mmap #0 -> 0x7586296f9000   low 12 bits = 0x000   page-aligned: yes
(run 3's #1..#7 match #0, all 0x7586296f9000, low 12 bits = 0x000)
```

Three runs, 24 mappings in total, and all 24 came back with zeros in the low 12 bits. The output also hides two small observations worth a look. Within one process, an address that was just handed back gets handed right out again: eight times we received the same address, because the kernel is reusing the hole that was just freed. And the base differs from run to run, which is ASLR at work again. Take the stack address `0x7fffffffd0dc` from the start of this piece: its low 12 bits are `0x0dc`. During translation the page number gets swapped for a physical page number, and the tail `0x0dc` is appended behind the physical page number exactly as it stands, without one bit being touched. The offset within the page needs no translation, because the layout inside a page is identical on the virtual and the physical side.

## The Arithmetic of One Flat Table, and Why the Number Is Absurd

Now let's think about how to build such a translation mechanism. The most direct design is one big flat table: every virtual page gets an entry, the entry holds a physical page number, and once you compute the page number from the virtual address you index into the table and you are done. Nothing is wrong with the design itself. Let's work out its cost with this machine's numbers.

Our virtual addresses are 48 bits wide, so the address space is 2 to the 48th power bytes, a volume of 256 TiB. Cut into 4 KiB pages, that is 2 to the 48th divided by 2 to the 12th, which is 2 to the 36th: 68719476736 pages. Every one of those pages needs an entry, and an entry is 8 bytes (later in this topic you will see with your own eyes in pagemap that an entry is exactly 8 bytes), so the total comes to 2 to the 39th power bytes, or 549755813888 bytes: a table the size of 512 GiB.

Put that number back against the machine. This box has 52 GiB of memory, and one process's flat page table alone wants 512 GiB, ten times more table than the machine has memory. Most of those 512 GiB of entries would be wasted on top of that: the six runs of f1 already showed us the pattern, the stack lives around `0x7ff...`, the program image lives in the `0x55..` to `0x61..` range, and the giant stretch from `0x0000` to `0x5555` in between has no user at all. A normal process's address space is extremely sparse, a few regions in actual use separated by vast holes. Reserve a table row for the holes too, and the flat design is dead.

The way out is to break the big table into levels. The idea is the same as a library's catalog: a thin directory at the top that records only where the next level's directories sit, that next level recording the level below it, and only the bottommost tables recording real physical page numbers. Tables get built only where memory is in use; an unused region is just an empty entry at the top, and the entire subtree under it never needs to exist. Next we look at how x86-64 does this concretely; the four-table relay is the next piece's business.
