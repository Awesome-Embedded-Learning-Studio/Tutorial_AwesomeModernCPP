---
title: "Four Levels of Tables: Splitting a 48-bit Address and Reading the PTE from pagemap"
chapter: 6
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 17]
description: "Walks the four tables PML4, PDPT, PD and PT in order, splits three real addresses into four 9-bit fields with a program, then reads the page table entry out of /proc/self/pagemap bit by bit and watches the present bit flip from 0 to 1 at the first touch"
reading_time_minutes: 23
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/06-page-table-walk/02-four-level-walk-and-pagemap.md
  source_hash: 21b3275dcb1b3d4ca880fb864648baca8c701693b4362e8c0e5b4c8a4339a138
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Four Levels of Tables: Splitting a 48-bit Address and Reading the PTE from pagemap

The previous piece put a hard price on the flat single-level table: 512 GiB, ten times more than the machine's memory. So the way out is to split the table into levels. In this piece we walk x86-64's four-level page table, watch a 48-bit address come apart into four 9-bit fields, follow the four tables from CR3 all the way down to the page table entry, and then read that entry out of /proc/self/pagemap bit by bit. The environment is the same one the [previous piece](01-virtual-address-and-4k-pages.md) used; follow it if you want to rerun anything.

## Four Levels of Tables: Splitting a 48-bit Address into Four 9-bit Fields

x86-64's paged table has four levels. Each table is sized to exactly one page: a 4 KiB table holds 512 entries, each entry is 8 bytes, and 512 times 8 comes to exactly one page's worth. 512 is 2 to the 9th power, so each level spends 9 bits of the virtual address as its index. Four levels of indexes spend 36 bits altogether; add the 12-bit page offset and 48 bits are used up precisely. The four tables carry the names PML4, PDPT, PD and PT from the top down, names with history baked into them, and we can just as well call them by position: top-level table, upper table, middle table, last-level table. The experiment output in this piece uses names like PT index and PD index. The entry for the top level sits in the CPU's CR3 register, which holds the physical address of the PML4 table. Every process has its own CR3 value, and switching processes is really just loading a different CR3, so this register is the hardware pivot each process's independent address space rests on. Intel's manual says it in its own words: `CR3 is used to locate the first paging-structure, the PML4 table`. Rendered into plain language, what CR3 locates is the first paging structure, the top-level PML4 table. The version I checked is Intel 64 and IA-32 Architectures SDM, Vol 3A, section `4.5`, retrieved on October 12, 2026.

Let's take three real addresses apart with a program. The field layout is fixed: bits 47 to 39 index the PML4, bits 38 to 30 index the PDPT, bits 29 to 21 index the PD, bits 20 to 12 index the PT, and the low 12 bits are the page offset:

```cpp
// f4_split.cpp -- split a 48-bit virtual address into a 12-bit offset plus four 9-bit indexes
#include <cstdio>
#include <cstdint>
#include <cstdlib>

int main(int argc, char** argv) {
    uint64_t addr = 0x7fffffffd0dcULL;   // the &stack_var from the F1 setarch -R run
    if (argc > 1) addr = strtoull(argv[1], nullptr, 0);

    printf("address            = 0x%016llx\n", (unsigned long long)addr);
    printf("bits used (48-bit) = 0x%012llx\n", (unsigned long long)addr);

    unsigned off  = addr & 0xFFF;              // bits  0-11
    unsigned l1   = (addr >> 12) & 0x1FF;      // bits 12-20  PT index
    unsigned l2   = (addr >> 21) & 0x1FF;      // bits 21-29  PD index
    unsigned l3   = (addr >> 30) & 0x1FF;      // bits 30-38  PDPT index
    unsigned l4   = (addr >> 39) & 0x1FF;      // bits 39-47  PML4 index
    unsigned top  = (addr >> 48) & 0xFFFF;     // bits 48-63  canonical hole

    printf("page offset (bits  0-11, 12 bits) = 0x%03x\n", off);
    printf("L1/PT   index (bits 12-20,  9 bits) = 0x%03x  (%u)\n", l1, l1);
    printf("L2/PD   index (bits 21-29,  9 bits) = 0x%03x  (%u)\n", l2, l2);
    printf("L3/PDPT index (bits 30-38,  9 bits) = 0x%03x  (%u)\n", l3, l3);
    printf("L4/PML4 index (bits 39-47,  9 bits) = 0x%03x  (%u)\n", l4, l4);
    printf("bits 48-63 (canonical sign ext.)   = 0x%04x\n", top);
    printf("check: %llx == %llx ? %s\n",
           (unsigned long long)(((((uint64_t)l4 << 9 | l3) << 9 | l2) << 9 | l1) << 12 | off),
           (unsigned long long)(addr & 0xFFFFFFFFFFFFULL),
           (((((uint64_t)l4 << 9 | l3) << 9 | l2) << 9 | l1) << 12 | off) == (addr & 0xFFFFFFFFFFFFULL) ? "yes" : "NO");
    return 0;
}
```

```text
$ g++ -O0 -g -Wall -Wextra -o f4_split f4_split.cpp
$ ./f4_split 0x7fffffffd0dc
address            = 0x00007fffffffd0dc
bits used (48-bit) = 0x7fffffffd0dc
page offset (bits  0-11, 12 bits) = 0x0dc
L1/PT   index (bits 12-20,  9 bits) = 0x1fd  (509)
L2/PD   index (bits 21-29,  9 bits) = 0x1ff  (511)
L3/PDPT index (bits 30-38,  9 bits) = 0x1ff  (511)
L4/PML4 index (bits 39-47,  9 bits) = 0x0ff  (255)
bits 48-63 (canonical sign ext.)   = 0x0000
check: 7fffffffd0dc == 7fffffffd0dc ? yes
===
$ ./f4_split 0x5724fca5a169
address            = 0x00005724fca5a169
bits used (48-bit) = 0x5724fca5a169
page offset (bits  0-11, 12 bits) = 0x169
L1/PT   index (bits 12-20,  9 bits) = 0x05a  (90)
L2/PD   index (bits 21-29,  9 bits) = 0x1e5  (485)
L3/PDPT index (bits 30-38,  9 bits) = 0x093  (147)
L4/PML4 index (bits 39-47,  9 bits) = 0x0ae  (174)
bits 48-63 (canonical sign ext.)   = 0x0000
check: 5724fca5a169 == 5724fca5a169 ? yes
===
$ ./f4_split 0x7ffd89c0f6ac
address            = 0x00007ffd89c0f6ac
bits used (48-bit) = 0x7ffd89c0f6ac
page offset (bits  0-11, 12 bits) = 0x6ac
L1/PT   index (bits 12-20,  9 bits) = 0x00f  (15)
L2/PD   index (bits 21-29,  9 bits) = 0x04e  (78)
L3/PDPT index (bits 30-38,  9 bits) = 0x1f6  (502)
L4/PML4 index (bits 39-47,  9 bits) = 0x0ff  (255)
bits 48-63 (canonical sign ext.)   = 0x0000
check: 7ffd89c0f6ac == 7ffd89c0f6ac ? yes
```

The check line at the end of the program reassembles the four indexes into an address and compares. All three addresses close, which tells us splitting and reassembling are inverse operations of each other, and that this field layout is self-consistent on this machine. The Intel manual states each level's bit field in writing too. For the last-level table the words are `A page table comprises 512 64-bit entries (PTEs). ... Bits 11:3 are bits 20:12 of the linear address`: the last-level table holds 512 entries, and its index comes from bits 20:12 of the linear address. For the top level the words are `Bits 11:3 are bits 47:39 of the linear address`, taking bits 47:39. In our experiment l4 takes bits 39 through 47 and l1 takes bits 12 through 20, matching the manual's fields one for one.

Let's narrate the walk for the first address. The CPU takes the PML4 table's physical address from CR3 and uses slot 255 (the value of bits 47:39) to fetch one PML4 entry, and that entry names the physical address of a PDPT table. Slot 511 (bits 38:30) then fetches the PD's physical address out of the PDPT, slot 511 (bits 29:21) fetches the PT's physical address out of the PD, and slot 509 (bits 20:12) fetches the final entry out of the PT, the entry we keep calling the page table entry, the PTE. The PTE carries a physical page number; splice the low 12 bits, `0x0dc`, onto it and the physical address is in hand. One memory access touches four tables on the way. Note that cost down, because the next piece comes back to price it when we cover the TLB (translation lookaside buffer).

There is also a neat little find hiding in the output. Two of the addresses I picked are stack addresses, `0x7fffffffd0dc` with ASLR off and `0x7ffd89c0f6ac` with ASLR on. They sit a randomization gap apart, yet both split into the same PML4 index, 255. That is not a coincidence. The x86-64 convention is that user space lives in PML4 slots 0 through 255 and the kernel lives in 256 through 511, and slot 255 is the very top cell of the user half. The stack already lives at the highest end of user space, so no matter where randomization moves it, it stays pressed up under the slot-255 ceiling. The code address splits into PML4 index 174, also inside the user half.

One corner of the address width still needs a word. The manual says IA-32e paging translates 48-bit linear addresses to physical addresses of up to 52 bits, but this machine's cpuinfo reports 48 physical bits. The 52 is only what the architecture permits at most; on this machine we go by 48. As for the 16 bits above 48, they are filled by sign extension from bit 47, and the top 16 bits of all three of our addresses are 0, so this piece does not go further into that.

I am leaving a slot here for a diagram of the four-level walk; the figure is still to come. It should show the whole chain from the PML4 that CR3 points at, down level by level to the physical page, marking which bits of the virtual address each of the four index fields 47:39, 38:30, 29:21 and 20:12 comes from, and how the page offset attaches unchanged behind the physical page number.

Why does splitting save that much? A rough count settles it. The top-level table always exists, exactly one of them, and it takes 4 KiB. How much ground does one of its entries cover? The manual's words: `Because a PML4E is identified using bits 47:39 of the linear address, it controls access to a 512-GByte region of the linear-address space`. One PML4E corresponds to a 512 GiB region of the linear address space. For a 512 GiB region nothing uses, the top-level entry sits empty and none of the three whole tables beneath it need exist at all. The addresses a normal process actually touches spread across a few 512 GiB regions, the tables along each path add up to a few dozen, and each one costs 4 KiB, so the total drops from 512 GiB to the order of a few dozen KiB. The f6_rss experiment in the next piece gives a measured number for this: map a 1 GiB region and the page tables plus management structures cost only 64 kB in total.

## Reading the Page Table Entry Out of /proc/self/pagemap

What actually goes into a PTE? We do not have to guess, because Linux spreads it out for us. /proc/self/pagemap is an interface file the kernel provides; every virtual page takes one 8-byte entry in it, laid out in virtual page number order. So to read the PTE for some address, divide the address by the page size to get the page number, then multiply by 8 to get the byte offset within the file. Let's write a program to read a stack variable:

```cpp
// f3_pagemap.cpp -- read a stack variable's PTE from /proc/self/pagemap and decode its bits
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

static void dump_pte(const char* label, uintptr_t vaddr) {
    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t vpn = vaddr / page_size;          // virtual page number
    uint64_t off  = vpn * sizeof(uint64_t);     // byte offset into pagemap

    int fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) { perror("open /proc/self/pagemap"); exit(1); }

    uint64_t pte = 0;
    if (lseek(fd, (off_t)off, SEEK_SET) == (off_t)-1) { perror("lseek"); exit(1); }
    if (read(fd, &pte, sizeof(pte)) != (int)sizeof(pte)) { perror("read"); exit(1); }
    close(fd);

    uint64_t pfn     = pte & ((1ULL << 55) - 1);  // bits 0..54
    int soft_dirty   = (pte >> 55) & 1;
    int exclusive    = (pte >> 56) & 1;           // "exclusively mapped"
    int file_page    = (pte >> 61) & 1;
    int swapped      = (pte >> 62) & 1;
    int present      = (pte >> 63) & 1;

    printf("%s vaddr=0x%012lx\n", label, (unsigned long)vaddr);
    printf("  vpn (vaddr>>12)            = %lu (0x%lx)\n", (unsigned long)vpn, (unsigned long)vpn);
    printf("  pagemap byte offset        = %lu (0x%lx)\n", (unsigned long)off, (unsigned long)off);
    printf("  raw 64-bit entry           = 0x%016llx\n", (unsigned long long)pte);
    printf("  bit63 present              = %d\n", present);
    printf("  bit62 swapped              = %d\n", swapped);
    printf("  bit61 file-backed          = %d\n", file_page);
    printf("  bit56 exclusively-mapped   = %d\n", exclusive);
    printf("  bit55 soft-dirty           = %d\n", soft_dirty);
    printf("  bits0-54 PFN               = %llu (0x%llx)\n",
           (unsigned long long)pfn, (unsigned long long)pfn);
    printf("  -> phys addr (PFN<<12)     = 0x%013llx%s\n",
           (unsigned long long)(pfn << 12),
           pfn == 0 ? "   [PFN field is ZERO - not readable at this privilege]"
                    : "");
}

int main(int argc, char** argv) {
    int on_stack = 0x1234;
    uintptr_t stack_addr = (uintptr_t)&on_stack;

    printf("uid = %d\n", getuid());
    if (argc > 1) printf("argv[1] = %s\n", argv[1]);

    printf("--- before touching dedicated mmap page ---\n");
    dump_pte("stack-before", stack_addr);

    on_stack = on_stack + 1;
    printf("--- after touching stack var ---\n");
    dump_pte("stack-after ", stack_addr);
    return 0;
}
```

```text
$ g++ -O0 -g -Wall -Wextra -o f3_pagemap f3_pagemap.cpp
$ ./f3_pagemap
uid = 1000
--- before touching dedicated mmap page ---
stack-before vaddr=0x7ffcce28160c
  vpn (vaddr>>12)            = 34356388481 (0x7ffcce281)
  pagemap byte offset        = 274851107848 (0x3ffe671408)
  raw 64-bit entry           = 0x8180000000000000
  bit63 present              = 1
  bit62 swapped              = 0
  bit61 file-backed          = 0
  bit56 exclusively-mapped   = 1
  bit55 soft-dirty           = 1
  bits0-54 PFN               = 0 (0x0)
  -> phys addr (PFN<<12)     = 0x0000000000000   [PFN field is ZERO - not readable at this privilege]
--- after touching stack var ---
stack-after  vaddr=0x7ffcce28160c
  (identical to the before section: raw 0x8180000000000000, present=1, PFN=0)
```

One label in the output needs explaining. The line `before touching dedicated mmap page` is a fixed string hardcoded in the printing function. At the f3 stage it actually corresponds to the page holding the stack variable; the dedicated, freshly mmap'd clean page does not enter the story until f3b. The after line says stack variable, and that one does line up.

Let's verify the raw value `0x8180000000000000` by hand, so we do not trust the program too quickly. The top 16 bits of this number are `0x8180`, whose binary is `1000 0001 1000 0000`. Counting down from bit 63, the bits standing up are bit 63, bit 56 and bit 55, and every other high bit is 0. The three 1s the program printed for present, exclusively-mapped and soft-dirty agree with the hand count.

The kernel documentation, the [pagemap manual page](https://docs.kernel.org/admin-guide/mm/pagemap.html) (the file `Documentation/admin-guide/mm/pagemap.rst` in the kernel source tree, retrieved October 12, 2026), defines these bits one by one. A few key lines. `Bits 0-54 page frame number (PFN) if present`: bits 0 through 54 hold the physical page number. `Bit 55 pte is soft-dirty`: bit 55 marks a dirty page. `Bit 56 page exclusively mapped`: bit 56 says the page is not shared. `Bit 63 page present`: bit 63 says the page is in physical memory. Now set what we read against these definitions. The stack variable's page has present at 1, so it is already in physical memory. exclusive reads 1, so no other mapping shares it. soft-dirty reads 1, so it has been written.

The real drama is in the PFN. By the documentation's bit layout, bits 0 through 54 should hold the physical page number, and what we read is 0. The experiment has not actually failed at this point; the masking is something the kernel does on purpose. The documentation is blunt about it. `Since Linux 4.0 only users with the CAP_SYS_ADMIN capability can get PFNs`: from Linux 4.0 on, only users holding the CAP_SYS_ADMIN capability can get physical page numbers. `Starting from 4.2 the PFN field is zeroed if the user does not have CAP_SYS_ADMIN`: from 4.2 on, readers without that capability see the PFN field zeroed out. Why hide it? The documentation gives the reason too: `information about PFNs helps in exploiting Rowhammer vulnerability`. Physical page numbers are useful to exploits of the Rowhammer kind. Rowhammer is a family of hardware attacks against DRAM that work by reading and writing the same row over and over to disturb the rows next to it, and an attacker who knows which pages sit adjacent in physical memory has a far easier time aiming. So as ordinary users we can read whether a page is in physical memory, but not where it lives. The other half of address translation is open only to readers with privilege.

We ran two comparisons along the way, and both are worth writing down. The first puts the program inside a fresh user namespace with `unshare -Ur`; the program sees uid 0, and to itself it counts as root:

```text
$ unshare -Ur ./f3_pagemap 2>&1 | head -20
uid = 0
--- before touching dedicated mmap page ---
stack-before vaddr=0x7ffe2c0cc24c
  vpn (vaddr>>12)            = 34357821644 (0x7ffe2c0cc)
  pagemap byte offset        = 274862573152 (0x3fff160660)
  raw 64-bit entry           = 0x8180000000000000
  bit63 present              = 1
  bit62 swapped              = 0
  bit61 file-backed          = 0
  bit56 exclusively-mapped   = 1
  bit55 soft-dirty           = 1
  bits0-54 PFN               = 0 (0x0)
  -> phys addr (PFN<<12)     = 0x0000000000000   [PFN field is ZERO - not readable at this privilege]
--- after touching stack var ---
stack-after  vaddr=0x7ffe2c0cc24c
  vpn (vaddr>>12)            = 34357821644 (0x7ffe2c0cc)
  pagemap byte offset        = 274862573152 (0x3fff160660)
  raw 64-bit entry           = 0x8180000000000000
  bit62 swapped              = 0
  bit61 file-backed          = 0
exit=0
```

(`head -20` cut off the last lines of the after section, and I kept the truncation just as it happened.) The uid is plainly 0 already, yet the PFN field is still 0. The reason is that the kernel checks CAP_SYS_ADMIN in the initial user namespace, and the uid your process sees is not what it inspects; self-appointed root in a new namespace does not count in its eyes. The masking follows privilege, not the number on the uid, and this comparison shows that boundary clearly. For the second comparison we wanted real privilege and tried sudo:

```text
$ sudo -n ./f3_pagemap
sudo: a password is required
exit=1
```

My terminal for these experiments is a non-interactive session and cannot supply a password, so the numbers for the privileged case did not come out this round. I will not invent them. Not one real PFN value appears in this piece, on purpose. If you have an interactive terminal at hand, one run of `sudo ./f3_pagemap` shows what it looks like when the masking lifts; the kernel documentation's promise is that the PFN field is then filled in with the actual physical page number.

## When the present Bit Flips, the Page Fault Handler Is Filling In the Entry

One more thing to think back over: in f3's output, the before and after sections are exactly the same. The plan was to read the PTE once before touching the variable and once after, and watch the present bit go from 0 to 1. But printf had already touched the stack variable before the first read, so by the time we looked, present was long since 1, and the moment of the flip was never captured. That moment is exactly what we want to film, so we need a clean page that nobody has touched for sure. We mmap a fresh page of anonymous memory, read its PTE, write one byte into it, and read again:

```cpp
// f3b_present.cpp -- watch the present bit flip from 0 to 1 on a fresh anonymous page nobody has touched
#include <cstdio>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

static uint64_t read_pte(uintptr_t vaddr) {
    long page = sysconf(_SC_PAGESIZE);
    uint64_t off = (vaddr / page) * 8;
    int fd = open("/proc/self/pagemap", O_RDONLY);
    uint64_t pte = 0;
    lseek(fd, (off_t)off, SEEK_SET);
    if (read(fd, &pte, 8) != 8) { perror("read"); _exit(1); }
    close(fd);
    return pte;
}

int main() {
    long page = sysconf(_SC_PAGESIZE);
    char* p = (char*)mmap(nullptr, page, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    uint64_t before = read_pte((uintptr_t)p);
    p[0] = 7;                                  // first touch: page faults in
    uint64_t after = read_pte((uintptr_t)p);

    printf("fresh anonymous page at %p\n", (void*)p);
    printf("PTE before touch = 0x%016llx  (bit63 present = %llu)\n",
           (unsigned long long)before, (unsigned long long)(before >> 63 & 1));
    printf("PTE after  touch = 0x%016llx  (bit63 present = %llu)\n",
           (unsigned long long)after, (unsigned long long)(after >> 63 & 1));
    return 0;
}
```

```text
$ g++ -O0 -g -Wall -Wextra -o f3b_present f3b_present.cpp
$ ./f3b_present
fresh anonymous page at 0x7ea607119000
PTE before touch = 0x0080000000000000  (bit63 present = 0)
PTE after  touch = 0x8180000000000000  (bit63 present = 1)

$ ./f3b_present
fresh anonymous page at 0x721d30fcc000
PTE before touch = 0x0080000000000000  (bit63 present = 0)
PTE after  touch = 0x8180000000000000  (bit63 present = 1)

$ ./f3b_present
fresh anonymous page at 0x767a3d2ec000
PTE before touch = 0x0080000000000000  (bit63 present = 0)
PTE after  touch = 0x8180000000000000  (bit63 present = 1)
```

We ran it three times, and all three runs show the same flip. While the page is untouched, the PTE reads `0x0080000000000000`, bit 63 holds 0, and the only bit standing is bit 55 (soft-dirty), the mark the kernel uses for dirty-page tracking. Once we have written one byte, the PTE reads `0x8180000000000000` and bit 63 stands up. When mmap finished, the page table entry held no registration for this page yet; the first write triggered one page fault, and the work the kernel did inside that fault was to allocate a physical page and fill the PFN and the flag bits into the PTE, and only then did our write instruction actually land in memory. The pair of raw values above is a direct observation of a page table entry being filled in by the page fault handler. How a page fault sorts into its two paths onward is where the next piece picks up.
