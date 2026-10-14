---
title: "Two Fates of a Page Fault: Demand Paging and the TLB"
chapter: 6
order: 3
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "Legal page faults get their page filled in by the kernel, illegal ones escalate into SIGSEGV; we measure both routes with the min_flt counter and a gdb session, then watch VmSize and VmRSS separate registration from allocation, and see how the TLB absorbs the four-table cost of translation"
reading_time_minutes: 27
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/06-page-table-walk/03-page-fault-demand-paging-and-tlb.md
  source_hash: 6bd070d5357468b66064d643f6aa2c147869430c12b7041bd5b9f21006ece464
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Two Fates of a Page Fault: Demand Paging and the TLB

The previous piece ended on that pair of raw PTE values, the present bit flipping from 0 to 1, and the hand that filled in the entry belongs to the page fault handler. This piece keeps walking: a legal page fault gets its page filled in by the kernel, an illegal one escalates into a signal, a mapping is registration while the page itself is only handed over on first touch, and the four-table cost has the TLB standing behind it. We use the same environment as the [first piece](01-virtual-address-and-4k-pages.md); follow it if you want to rerun anything.

## Two Fates of a Page Fault: Fill In a Page, or Raise a Signal

The present-bit story from the previous piece leads straight into this piece's main character: the page fault. The hardware in the CPU that runs translation is called the MMU (memory management unit). While it walks the page table, if it hits an entry whose present bit is 0, or an entry whose permissions don't match the access, the memory instruction gets stopped, and the kernel receives a page fault report. The fault in page fault here means something is missing; it doesn't mean the program is written wrong. Once the report lands, two fates are possible. If the mapping is legal and the page simply isn't there, the kernel fills the page in and the program keeps running as if nothing had happened; the present-bit flip we watched last time went down this same road. If the mapping doesn't exist at all, or the permissions forbid the access, the kernel escalates the report into a signal, and that's your segmentation fault. Let's run both routes for real.

For the legal-fault route we watch a counter. Field 10 of `/proc/self/stat` is min_flt, the process's count of minor faults, meaning faults that were filled without any disk I/O. Parsing this file has a small twist: field 2 is the process name, wrapped in parentheses that may themselves contain spaces, so the safe way to count fields is to find the last right parenthesis and start counting after it. We cross-checked the counting independently with awk:

```text
$ awk '{print "awk sees comm=", $2, " field10(min_flt)=", $10}' /proc/self/stat
awk sees comm= (awk)  field10(min_flt)= 240
```

Here is the program. Between the four readings we wedge in three actions: mmap four pages, touch the first page, then touch the third. At the end we go back and touch the first page a second time:

```cpp
// f5_minflt.cpp -- watching demand paging's one-fault-per-page through the min_flt counter
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

static long min_flt() {
    FILE* f = fopen("/proc/self/stat", "r");
    if (!f) { perror("fopen"); return -1; }
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = 0;
    fclose(f);
    char* rp = strrchr(buf, ')');
    if (!rp) return -1;
    long fld;
    int consumed;
    // tokens after ')': state(3) ppid(4) pgrp(5) session(6) tty_nr(7)
    //                   tpgid(8) flags(9) minflt(10)
    int rc = sscanf(rp + 1, "%*s %*s %*s %*s %*s %*s %*s %ld %n", &fld, &consumed);
    if (rc != 1) return -1;
    return fld;
}

int main() {
    long page = sysconf(_SC_PAGESIZE);

    long before = min_flt();
    printf("min_flt before mmap : %ld\n", before);

    char* p = (char*)mmap(nullptr, 4 * page, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    long after_mmap = min_flt();
    printf("min_flt after  mmap : %ld   (delta %ld)\n", after_mmap, after_mmap - before);

    p[0] = 1;  // first touch: demand paging fault #1
    long after_touch1 = min_flt();
    printf("min_flt after 1st touch (p[0]) : %ld   (delta %ld)\n", after_touch1, after_touch1 - after_mmap);

    p[2 * page] = 2;  // third page: fault #2
    long after_touch3 = min_flt();
    printf("min_flt after 3rd page touch   : %ld   (delta %ld)\n", after_touch3, after_touch3 - after_touch1);

    p[0] = 3;  // re-touch already-resident page: expect NO new fault
    long after_retouch = min_flt();
    printf("min_flt after re-touch p[0]    : %ld   (delta %ld)\n", after_retouch, after_retouch - after_touch3);
    return 0;
}
```

```text
$ g++ -O0 -g -Wall -Wextra -o f5_minflt f5_minflt.cpp
$ ./f5_minflt
min_flt before mmap : 191
min_flt after  mmap : 196   (delta 5)
min_flt after 1st touch (p[0]) : 197   (delta 1)
min_flt after 3rd page touch   : 198   (delta 1)
min_flt after re-touch p[0]    : 198   (delta 0)
=== run 2 ===
min_flt before mmap : 191
min_flt after  mmap : 196   (delta 5)
min_flt after 1st touch (p[0]) : 197   (delta 1)
min_flt after 3rd page touch   : 198   (delta 1)
min_flt after re-touch p[0]    : 198   (delta 0)
=== run 3 ===
(identical to run 1 and run 2, word for word)
```

Let's read the staircase line by line. Touching the first page added 1 to the counter, touching the third added 1 more: the first touch of each new page produces exactly one fault, and the work the kernel does in that moment is to hand out a physical page and fill in the PTE. When we go back and touch the first page again, the counter stops moving; the page is resident, translation goes through unhindered. The three runs produced identical numbers. Same binary, same dynamic-loading sequence, and the counter reproduced even its starting point.

That delta 5 in the middle deserves a word of its own. The mmap call itself ate 5 faults, but they are not data-page faults: the kernel has to set up a VMA for the new mapping (a record of one contiguous mapped region, which the kernel keeps for every mapping) and hand out page-table pages level by level, and this bookkeeping touches memory too. So the accurate way to describe mmap is that it doesn't allocate memory for data pages; registration is registration, and not a single data page moves. Don't stretch that statement into "mmap causes zero faults"; those 5 were really counted.

For the illegal-fault route we stage the scene ourselves. The program dereferences an address that lands in no mapping:

```cpp
// f5_segv.cpp -- dereference an address with no mapping behind it and catch the SIGSEGV in the act
#include <cstdio>

int main() {
    int* bad = (int*)0xdeadbeef;
    printf("about to dereference %p\n", (void*)bad);
    fflush(stdout);
    int v = *bad;          // not backed by any page table entry
    printf("unreachable, v=%d\n", v);
    return 0;
}
```

```text
$ g++ -O0 -g -Wall -Wextra -o f5_segv f5_segv.cpp
$ gdb -q -batch -ex 'run' \
  -ex 'info program' -ex 'x/i $pc' \
  -ex 'info proc mappings' ./f5_segv
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".
about to dereference 0xdeadbeef

Program received signal SIGSEGV, Segmentation fault.
0x0000555555555188 in main () at f5_segv.cpp:8
8	    int v = *bad;          // not backed by any page table entry

--- signal ---
Last stopped for thread 1 (Thread 0x7ffff7f66780 (LWP 196477)).
	Using the running image of child process 196477.
Program stopped at 0x555555555188.
It stopped with signal SIGSEGV, Segmentation fault.

--- faulting instruction ---
=> 0x555555555188 <main()+63>:	mov    (%rax),%eax

--- info proc mappings ---
process 196477
Mapped address spaces:

Start Addr         End Addr           Size               Offset             Perms File
0x0000555555554000 0x0000555555555000 0x1000             0x0                r--p  /tmp/cpu-book-06/f5_segv
0x0000555555555000 0x0000555555556000 0x1000             0x1000             r-xp  /tmp/cpu-book-06/f5_segv
0x0000555555556000 0x0000555555557000 0x1000             0x2000             r--p  /tmp/cpu-book-06/f5_segv
0x0000555555557000 0x0000555555558000 0x1000             0x2000             r--p  /tmp/cpu-book-06/f5_segv
0x0000555555558000 0x0000555555559000 0x1000             0x3000             rw-p  /tmp/cpu-book-06/f5_segv
0x0000555555559000 0x000055555558c000 0x33000            0x0                rw-p  [heap]
(the mapping ranges of libc / libm / libstdc++ / libgcc_s / ld.so are omitted here)
...
0x00007ffffffdd000 0x00007ffffffff000 0x22000            0x0                rw-p  [stack]
```

(We cut the thirty-odd lines of library file mappings out of the middle of the mapping table, keeping only the program's own five ranges plus heap and stack; not a single instruction was touched.)

One more gdb session pulls out the registers and the signal details as the kernel reported them:

```text
$ gdb -q -batch -ex 'set debuginfod enabled off' \
    -ex 'run' -ex 'p/x $rax' -ex 'x/i $pc' \
    -ex 'p/x $_siginfo._sifields._sigfault.si_addr' \
    -ex 'p $_siginfo.si_signo' -ex 'p $_siginfo.si_code' ./f5_segv
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".
about to dereference 0xdeadbeef

Program received signal SIGSEGV, Segmentation fault.
0x0000555555555188 in main () at f5_segv.cpp:8
8	    int v = *bad;          // not backed by any page table entry
$1 = 0xdeadbeef
=> 0x555555555188 <main()+63>:	mov    (%rax),%eax
$2 = 0xdeadbeef
$3 = 11
$4 = 1
```

Let's line the scene up item by item. The instruction that took the fall is `mov (%rax),%eax`, a perfectly ordinary load from memory, and rax holds exactly `0xdeadbeef`. The MMU walked the page table to its end and found no legal mapping; once the fault report went up, the kernel checked it, found this address sits in no mapped region, and the signal followed. Look at `$3`: the signal number is 11, and its name is SIGSEGV. Look at `$4`: the si_code is 1, and its name is SEGV_MAPERR, meaning the address isn't in any mapped region at all. Look at `$2`: si_addr is `0xdeadbeef`, and the faulting address the kernel reported matches the failing instruction's operand exactly. Then look back at the mapping table: the lowest mapping in the whole process starts at `0x555555554000`, and `0xdeadbeef` lies far below it, in the hole, with nowhere to land. Run it once straight from the shell and the exit code is 139, exactly 128 plus 11, which lines up with the signal number too:

```text
$ ./f5_segv; echo "exit=$?"
about to dereference 0xdeadbeef
exit=139
```

Put the two routes side by side and the page fault is one mechanism with two exits. A fault inside a legal mapping gets its page filled in by the kernel; the present-bit flip in the previous piece's f3b and the plus-one-per-page count in this piece's f5_minflt are both that story. A fault with no mapping escalates straight into SIGSEGV; the segmentation-fault scenes we watched in pieces 02 and 04 trace back to exactly here. The crash forensics we chased while studying stack overflow in 02 bottoms out at the same event: the page walk failed, the kernel sent the report out, and the process took the signal.

## A Mapping Is Registration; the Page Arrives on First Touch

The most direct ruler for the power of demand paging (pages are truly allocated only the first time they get used) is a pair of fields in /proc/self/status: VmSize is how much virtual address space is claimed, and VmRSS records the resident set, the physical memory the process is actually holding at this moment. Our experiment takes a 1 GiB anonymous mapping and touches only a few pages of it:

```cpp
// f6_rss.cpp -- mmap a 1 GiB anonymous region, touch only the first N pages,
// watch how far VmRSS and VmSize each move
// usage: f6_rss <npages>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

static long vm_field(const char* name) {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) { perror("fopen"); exit(1); }
    char line[256];
    long result = -1;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, name, strlen(name)) == 0) {
            sscanf(line + strlen(name), ": %ld kB", &result);
            break;
        }
    }
    fclose(f);
    return result;
}

int main(int argc, char** argv) {
    long page = sysconf(_SC_PAGESIZE);
    long npages = (argc > 1) ? atol(argv[1]) : 3;
    size_t sz = (size_t)1 << 30;   // 1 GiB

    printf("touching %ld page(s) of a %zu-byte (1 GiB) anonymous mapping\n", npages, sz);

    long rss0 = vm_field("VmRSS");
    long vsz0 = vm_field("VmSize");
    printf("before mmap : VmRSS=%ld kB  VmSize=%ld kB\n", rss0, vsz0);

    char* p = (char*)mmap(nullptr, sz, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    long rss1 = vm_field("VmRSS");
    long vsz1 = vm_field("VmSize");
    printf("after  mmap : VmRSS=%ld kB (delta %+ld)  VmSize=%ld kB (delta %+ld)\n",
           rss1, rss1 - rss0, vsz1, vsz1 - vsz0);

    for (long i = 0; i < npages; i++)
        p[i * page] = 1;

    long rss2 = vm_field("VmRSS");
    printf("after touch : VmRSS=%ld kB (delta %+ld)  VmSize=%ld kB\n",
           rss2, rss2 - rss1, vsz1);
    printf("pages touched=%ld -> in theory %ld kB (%.2f MiB)\n",
           npages, npages * page / 1024, npages * page / 1048576.0);
    return 0;
}
```

```text
$ g++ -O0 -g -Wall -Wextra -o f6_rss f6_rss.cpp
$ ./f6_rss 3
touching 3 page(s) of a 1073741824-byte (1 GiB) anonymous mapping
before mmap : VmRSS=3968 kB  VmSize=7296 kB
after  mmap : VmRSS=4032 kB (delta +64)  VmSize=1055872 kB (delta +1048576)
after touch : VmRSS=4044 kB (delta +12)  VmSize=1055872 kB
pages touched=3 -> in theory 12 kB (0.01 MiB)

$ ./f6_rss 3     (2nd run)
touching 3 page(s) of a 1073741824-byte (1 GiB) anonymous mapping
before mmap : VmRSS=4000 kB  VmSize=7296 kB
after  mmap : VmRSS=4064 kB (delta +64)  VmSize=1055872 kB (delta +1048576)
after touch : VmRSS=4076 kB (delta +12)  VmSize=1055872 kB
pages touched=3 -> in theory 12 kB (0.01 MiB)

$ ./f6_rss 1000
touching 1000 page(s) of a 1073741824-byte (1 GiB) anonymous mapping
before mmap : VmRSS=4004 kB  VmSize=7296 kB
after  mmap : VmRSS=4068 kB (delta +64)  VmSize=1055872 kB (delta +1048576)
after touch : VmRSS=8068 kB (delta +4000)  VmSize=1055872 kB
pages touched=1000 -> in theory 4000 kB (3.91 MiB)

$ ./f6_rss 1000     (2nd run)
touching 1000 page(s) of a 1073741824-byte (1 GiB) anonymous mapping
before mmap : VmRSS=4000 kB  VmSize=7296 kB
after  mmap : VmRSS=4064 kB (delta +64)  VmSize=1055872 kB (delta +1048576)
after touch : VmRSS=8064 kB (delta +4000)  VmSize=1055872 kB
pages touched=1000 -> in theory 4000 kB (3.91 MiB)
```

Let's read the two columns separately. The moment the mmap is done, VmSize has grown by 1048576 kB, 1 GiB to the byte, and the registration is complete. VmRSS grew by only 64 kB; that overhead is the page-table pages and management structures like VMAs, echoing the hierarchy dividend we computed in the previous piece: with one big flat table, the table alone would eat 512 GiB of memory. Then comes the touching. Touch 3 pages and VmRSS grows 12 kB; touch 1000 pages and it grows 4000 kB. Physical memory grows strictly as pages touched times 4 KiB. At 3 pages, the 64 kB of management structures plus the 12 kB of data pages together add up to 76 kB, against 1 GiB of mapped space; what sits between the two numbers is registration and allocation being separate things. One environmental precondition to disclose here: transparent huge pages on this machine are in madvise mode, so anonymous mmap runs entirely on 4 KiB base pages. If your machine is in always mode, huge pages get mixed in and the increments stop tracking the page count this neatly. There's plenty more to say about mmap itself, how to set its arguments and how file mappings work, but that goes beyond this piece.

## Translation Needs a Cache Too: the TLB

Remember the cost we noted while walking the four levels in the previous piece? Every address translation touches four tables, which means four extra memory accesses, more expensive than the access to the data itself. If the CPU really walked them on every access, no program would be usable. The help that actually arrives is the TLB, full name translation lookaside buffer, a small unit inside the CPU that caches recent translation results: the recently translated virtual-page-number to physical-page-number pairs live in it, the next access to the same page takes its translation straight from there, and the four tables don't get visited at all. The TLB is finite; translations that don't fit have to walk the page tables again, and we'll run a lightweight experiment to feel what that overflow costs. The design: touch one byte per page, so the amount of data stays small enough to live in the caches the whole time and the timing difference lands mainly on the translation layer:

```cpp
// f7_tlb.cpp -- TLB pressure demo (excerpt: the core of the three timings and
// the array setup; now_ms reads CLOCK_MONOTONIC and converts to milliseconds,
// make_perm shuffles a random permutation of 0..n-1 from a fixed seed; neither
// helper does anything else, so both are omitted here)
//   A) 1 GiB footprint, walked in page order
//   B) 1 GiB footprint, the same pages in random order
//   C) 8 MiB footprint, random order (the footprint fits inside TLB coverage)
static unsigned char* make_array(size_t sz) {
    unsigned char* p = (unsigned char*)mmap(nullptr, sz, PROT_READ | PROT_WRITE,
                                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(1); }
    madvise(p, sz, MADV_NOHUGEPAGE);   // keep 4 KiB base pages
    return p;
}

int main() {
    const long PS = sysconf(_SC_PAGESIZE);
    const size_t BIG = (size_t)1 << 30;        // 1 GiB
    const size_t SMALL = (size_t)8 << 20;      // 8 MiB
    const size_t nb = BIG / PS, ns_ = SMALL / PS;

    unsigned char* a_big = make_array(BIG);
    unsigned char* a_small = make_array(SMALL);
    auto perm_big = make_perm(nb, 42);
    auto perm_small = make_perm(ns_, 43);

    for (size_t i = 0; i < nb; i++) a_big[i * PS] = 1;      // warmup
    for (size_t i = 0; i < ns_; i++) a_small[i * PS] = 1;

    volatile unsigned long sink = 0;
    double t0;

    t0 = now_ms();
    for (int r = 0; r < 100; r++)
        for (size_t i = 0; i < nb; i++) sink += a_big[i * PS];
    double seq_big = now_ms() - t0;

    t0 = now_ms();
    for (int r = 0; r < 100; r++)
        for (size_t i = 0; i < nb; i++) sink += a_big[perm_big[i] * PS];
    double rnd_big = now_ms() - t0;

    t0 = now_ms();
    for (int r = 0; r < 1000; r++)
        for (size_t i = 0; i < ns_; i++) sink += a_small[perm_small[i] * PS];
    double rnd_small = now_ms() - t0;

    printf("A) 1 GiB, %zu pages, sequential order : %7.1f ms  (%5.1f ns/access)\n",
           nb, seq_big, seq_big * 1e6 / (100.0 * nb));
    printf("B) 1 GiB, %zu pages, random order     : %7.1f ms  (%5.1f ns/access)\n",
           nb, rnd_big, rnd_big * 1e6 / (100.0 * nb));
    printf("C) 8 MiB, %zu pages, random order     : %7.1f ms  (%5.1f ns/access)\n",
           ns_, rnd_small, rnd_small * 1e6 / (1000.0 * ns_));
    printf("B/A = %.1fx, B/C = %.1fx   (sink=%lu)\n",
           rnd_big / seq_big, rnd_big / (rnd_small * (100.0 * nb) / (1000.0 * ns_)),
           (unsigned long)sink);
    return 0;
}
```

The disclaimer comes before the numbers: this is a demo-grade measurement. We ran it in a WSL2 environment, with no perf on the machine, single runs, no process pinning; the numbers speak to orders of magnitude, and this is no benchmark. Compiled with -O2, run three times in a row:

```text
$ g++ -O2 -Wall -Wextra -o f7_tlb f7_tlb.cpp
$ ./f7_tlb
A) 1 GiB, 262144 pages, sequential order :   228.0 ms  (  8.7 ns/access)
B) 1 GiB, 262144 pages, random order     :   370.6 ms  ( 14.1 ns/access)
C) 8 MiB, 2048 pages, random order     :     8.6 ms  (  4.2 ns/access)
B/A = 1.6x, B/C = 3.4x   (sink=54476800)

$ ./f7_tlb
A) 1 GiB, 262144 pages, sequential order :   219.8 ms  (  8.4 ns/access)
B) 1 GiB, 262144 pages, random order     :   369.2 ms  ( 14.1 ns/access)
C) 8 MiB, 2048 pages, random order     :     8.7 ms  (  4.2 ns/access)
B/A = 1.7x, B/C = 3.3x   (sink=54476800)

$ ./f7_tlb
A) 1 GiB, 262144 pages, sequential order :   220.5 ms  (  8.4 ns/access)
B) 1 GiB, 262144 pages, random order     :   367.6 ms  ( 14.0 ns/access)
C) 8 MiB, 2048 pages, random order     :     8.8 ms  (  4.3 ns/access)
B/A = 1.7x, B/C = 3.3x   (sink=54476800)
```

The numbers hold steady across the three runs. Read them group by group. B runs about 1.6 to 1.7 times slower than A: over the same 262144 pages, walking sequentially also lands the page table's own memory accesses sequentially in the page-table pages, where hardware prefetching keeps up. Switch to random order and 262144 pages of translations go far past what the TLB covers (our 9700X is AMD's Zen 5; its L2 TLB holds a few thousand entries, which at 4 KiB pages covers a dozen-odd MiB), so most accesses have to walk the tables again. The most telling group is C: the jumping pattern is identical to B, but the footprint shrinks to 8 MiB, all 2048 pages fit inside TLB and lower-cache coverage, and the per-access time drops from 14.1 ns to 4.2 ns, 3.3 times faster than B. We should split C's winnings honestly, though: its 2048 cache lines total 128 kB, all of it small enough to live in L1 and L2, while B's 262144 lines amount to 16 MiB scattered across L3. So the 3.3x of B/C bundles two contributions, TLB hit-or-miss and cache hierarchy, and we can't pull them apart; we can't attribute it precisely to the TLB. A second version of the experiment rules out one more confounder: my first draft used a 256 MiB array whose touched lines total 4 MiB, all resident in L3, and sequential was still about 1.6 times slower than random (measured 58.6 ms vs 93.0 ms, which works out to 8.9 vs 14.2 ns per access), and that says the gap persists even when the data sits entirely in cache, so it can safely be charged to access order at the translation layer rather than to memory bandwidth. The full treatment of TLB capacity, and of widening coverage with 2 MiB huge pages, is in the [vol6 piece on the TLB](../../../vol6-performance/ch02-cpu-microarchitecture/02-04-tlb-hugepage-and-cpu-families.md); its view is far more complete than this demo.

## Replaying the Chain

Now that the road is walked, let's read that `%p` from the opening all the way through. What printf prints is a 48-bit virtual address. Its low 12 bits are the page offset, and their value survives translation untouched. The high 36 bits split into four nine-bit chunks, the relay markers for the four tables. The CPU gets the physical address of the PML4 from CR3, and we descend the four tables level by level; the PTE in the last table yields the physical page number, and adding the offset pins down the real location on the memory chips. A PTE carries flag bits along with the page number: present says whether the page is in physical memory. No page means a page fault. A legal fault gets its page filled in by the kernel, and the `0x0080`-to-`0x8180` flip in the previous piece's f3b is that fill caught in the act; an illegal fault escalates into SIGSEGV, with f5_segv's si_code as the verdict. Mapping and allocation are separate: 1 GiB of registration moves only 64 kB of management structures, and physical memory arrives in proportion to pages touched. Translation has the TLB standing guard; accesses outside its coverage pay the four-table cost, and the 1.6x gap under random jumping is how you measure it.

With this stretch the series' chain closes. 01 and 02 built up instructions and stack frames, 03 showed how arguments reach their registers, 04 and 05 put the consequences of running past an array's end and the true body of an object in front of you, and these last pieces connected pointer values to real locations on the memory chips. From here on, when you're debugging and see an address starting `0x7ffd...`, you know it lives in the stack region. When you hit a segmentation fault, the root of a failed page walk is in your hands. And when the question is how much memory a program takes, VmSize measures how much space was registered while VmRSS records what's truly occupied, and the two won't blur again.

We'll leave the cheapest self-verification to you: build f6_rss and run it once passing 3 and once passing 1000. You'll see VmSize grow 1048576 kB both times while VmRSS grows 12 kB in one run and 4000 kB in the other. Touch however many pages, and physical memory hands you exactly that many pages, not one byte more.
