---
title: "Large Objects Return Through a Hidden Pointer: The sret Route and the Routing Criteria"
chapter: 8
order: 2
tags: [host, cpp-modern, intermediate]
difficulty: intermediate
platform: host
cpp_standard: [11, 14, 17, 20]
description: "A 24-byte Big is written straight into the caller's stack slot through the hidden pointer in rdi, ABI classification cuts a type into eight-byte chunks with two as the limit, and non-trivial copy control switches the return to the sret route regardless of size"
reading_time_minutes: 16
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/08-return-value-journey/02-sret-and-routing-criteria.md
  source_hash: 62b55999018d491603f6b37f664d709633130552aa482672ee6721a18c5bdea5
  translated_at: '2026-10-14'
  engine: manual
current_status:
  title: Being polished
  detail: Freshly written in this batch; still being read through and polished. Wording and measured claims may change.
  variant: polishing
---

# Large Objects Return Through a Hidden Pointer: The sret Route and the Routing Criteria

In the previous piece we verified the first two routes: small values that fit in one chunk travel through the rax family as the value itself, and sixteen-byte aggregates get cut into eight-byte chunks, each claiming its own register. The environment here is the same one as in [the previous piece](01-rax-family-and-register-pairs.md), so follow that piece if you want to rerun anything.

## Large Objects Take a Hidden Pointer: The sret Route End to End

The twenty-four-byte `struct Big { long a, b, c; }` doesn't fit in two registers, and the third route was built for exactly this case. You have seen the whole mechanism once already, when piece 03 walked through make_big: the caller sets up a buffer on its stack and hands the buffer's address to the callee through rdi, which treats it exactly like a first argument; the function writes its fields straight into that memory, and rax carries that same address back on return. Toolchain documentation usually calls this sret, short for struct return, and that is the word this piece borrows. Piece 03 gave a plain-language version of the two-chunk rule; the next section brings in the rule's original text and the register roster for the return side, plus one exception 03 never mentioned.

```cpp
// The sret route end to end: returning a 24-byte Big
struct Big { long a, b, c; };
static_assert(sizeof(Big) == 24, "Big must be 24 bytes");

Big make_big(long x) __attribute__((noinline));
Big make_big(long x) { return {x, x + 1, x + 2}; }

int main() {
    Big b = make_big(100);
    volatile long sink = b.a + b.b + b.c;   // consumed, so it can't be optimized away
    return sink == 303 ? 0 : 1;
}
```

The compile command is `g++ -O0 -S -fno-asynchronous-unwind-tables r3_sret.cpp -o r3_sret.s`; below we paste both function bodies verbatim, with the `.cfi_*` lines omitted:

```asm
_Z8make_bigl:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, -8(%rbp)        ; spills the hidden return-slot pointer
	movq	%rsi, -16(%rbp)       ; spills the real argument x (which has been bumped to rsi!)
	movq	-8(%rbp), %rax
	movq	-16(%rbp), %rdx
	movq	%rdx, (%rax)          ; a -> slot+0
	movq	-16(%rbp), %rax
	leaq	1(%rax), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, 8(%rax)         ; b -> slot+8
	movq	-16(%rbp), %rax
	leaq	2(%rax), %rdx
	movq	-8(%rbp), %rax
	movq	%rdx, 16(%rax)        ; c -> slot+16
	movq	-8(%rbp), %rax        ; rax = the slot address, handed back
	popq	%rbp
	ret

main:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$48, %rsp
	leaq	-32(%rbp), %rax       ; the buffer is main's own local b
	movl	$100, %esi            ; the real argument goes into esi (the second slot)
	movq	%rax, %rdi            ; the hidden pointer goes into rdi (the first slot)
	call	_Z8make_bigl
	movq	-32(%rbp), %rdx       ; after the call, b is read in place, no copying
	movq	-24(%rbp), %rax
	addq	%rax, %rdx
	movq	-16(%rbp), %rax
	addq	%rdx, %rax
	movq	%rax, -40(%rbp)
	...
```

In main, the `.cfi_*` lines, the stack-protector check, and the tail that compares the return value are omitted as well. Let's line the assembly up against four stages. The caller sets up the slot: `leaq -32(%rbp), %rax` takes the address of the local b, so the buffer is b's own stack slot, with no second location in between. The argument steps aside: the real argument x=100 lands in esi, because rdi's spot has been taken by the address of the return buffer; that is the hidden first argument you met in 03. Writing the slot directly: three movq instructions in make_big put 100, 101, and 102 into the slot at offsets 0, 8, and 16, and at no point is there a twenty-four-byte bulk copy. Handing the address back: at the exit, `movq -8(%rbp), %rax` places the incoming address into rax exactly as it arrived.

Does the route really work this way? Let's run it under gdb with breakpoints; the debug build command is `g++ -O0 -g r3_sret.cpp -o r3_sret_dbg`:

```text
$ gdb -q -batch \
    -ex 'break make_big' \
    -ex 'run' \
    -ex 'printf "ENTRY  : rdi (hidden sret ptr) = %p\n", $rdi' \
    -ex 'printf "ENTRY  : rsi (real arg x)      = %ld\n", $rsi' \
    -ex 'up' \
    -ex 'printf "CALLER : &b  (main 栈上局部)   = %p\n", &b' \
    -ex 'down' \
    -ex 'finish' \
    -ex 'printf "RETURN : rax (还回的地址)      = %p\n", $rax' \
    -ex 'printf "RETURN : 缓冲里三个字段        = %ld %ld %ld\n", b.a, b.b, b.c' \
    ./r3_sret_dbg
Breakpoint 1 at 0x1145: file r3_sret.cpp, line 8.

This GDB supports auto-downloading debuginfo from the following URLs:
	<https://debuginfod.archlinux.org>
Enable debuginfod for this session? (y or [n]) [answered N; input not from terminal]
Debuginfod has been disabled.
To make this setting permanent, add 'set debuginfod enabled off' to .gdbinit.
[Thread debugging using libthread_db enabled]
Using host libthread_db library "/usr/lib/libthread_db.so.1".

Breakpoint 1, make_big (x=100) at r3_sret.cpp:8
8	Big make_big(long x) { return {x, x + 1, x + 2}; }
ENTRY  : rdi (hidden sret ptr) = 0x7fffffffd0a0
ENTRY  : rsi (real arg x)      = 100
#1  0x000055555555519e in main () at r3_sret.cpp:11
11	    Big b = make_big(100);
CALLER : &b  (main 栈上局部)   = 0x7fffffffd0a0
#0  make_big (x=100) at r3_sret.cpp:8
8	Big make_big(long x) { return {x, x + 1, x + 2}; }
main () at r3_sret.cpp:12
12	    volatile long sink = b.a + b.b + b.c;   // consumed, so it can't be optimized away
Value returned is $1 = {a = 100, b = 101, c = 102}
RETURN : rax (还回的地址)      = 0x7fffffffd0a0
RETURN : 缓冲里三个字段        = 100 101 102
```

The three 0x7fffffffd0a0 values read out in that session are one and the same address: rdi at entry, the value of `&b` inside main, and rax at exit. The callee's store instructions drop the data directly into the receiving variable's own stack slot, so no data is ever copied along this route; the only thing traveling back and forth in registers is an address. The authority for this flow is the Returning of Values section of the ABI document, and here is its step 2, word for word:

```text
2. If the type has class MEMORY, then the caller provides space for the return
value and passes the address of this storage in %rdi as if it were the first
argument to the function. In effect, this address becomes a "hidden" first
argument. ... On return %rax will contain the address that has been passed in
by the caller in %rdi.
```

In plain terms: once a type lands in class MEMORY, the caller sets aside space for the return value and puts that space's address in rdi as if it were the function's first argument, and the address becomes a hidden first argument. On return, what rax holds is exactly the address the caller handed in through rdi. What the document spells out is what we just watched happen, item by item, in the assembly and in gdb. This is the same document piece 03 introduced: Version 1.0, dated March 12, 2025, with Returning of Values on page 29 of the PDF. The repository lives in the same place as before, [gitlab.com/x86-psABIs/x86-64-ABI](https://gitlab.com/x86-psABIs/x86-64-ABI), and we checked it on October 12, 2026.

## The Routing Criteria: Classify by Eight-Byte Chunks, Two at Most

We have now seen all three routes, so it's time to collect the criteria in one place. The ABI's approach is to run the return type through the classification algorithm: the type is cut into eight-byte chunks, each chunk gets a class, integer-flavored content such as int and pointers goes to INTEGER, double goes to SSE, and then each chunk draws its own registers according to its class. Three more steps of that algorithm, in the original words:

```text
1. Classify the return type with the classification algorithm.

3. If the class is INTEGER, the next available register of the sequence %rax,
%rdx is used.

4. If the class is SSE, the next available vector register of the sequence
%xmm0, %xmm1 is used.
```

Step 1 says the return value goes through the same classification algorithm that arguments use; steps 3 and 4 mean, in plain words, that INTEGER chunks draw rax and then rdx, while SSE chunks draw xmm0 and then xmm1. Pair8, with two INTEGER chunks, took rax plus rdx; Mixed's INTEGER chunk took rax and its SSE chunk took xmm0; every measurement earlier in the series was just these two sentences at work. So where does the MEMORY threshold sit? Rule (c), the closing rule of the classification, reads:

```text
(c) If the size of the aggregate exceeds two eightbytes and the first
eightbyte isn't SSE or any other eightbyte isn't SSEUP, the whole argument is
passed in memory.
```

In plain terms: once an aggregate's size exceeds two eightbytes, if the first chunk isn't SSE, or any later chunk isn't SSEUP, the whole argument switches to being passed in memory. Big's twenty-four bytes fold into three chunks, all of them integer, so the limit is exceeded, the whole thing turns into MEMORY, and it takes the hidden pointer of the previous section. Check it against the argument-passing rules and you'll find that parameters and return values share one classification scheme; only the register roster changes.

So far the criteria have only spoken about size and class. There is one more exception, one that pays no attention to size at all, and the copy-counting experiment below walks right into it.

## Add a Copy Counter and the Route Changes

Next we want to count copy constructions, and to count them we have to give the type a user-provided copy constructor. The moment we add one, the type's identity in the ABI's eyes changes. A paired experiment makes this easy to see: in the source, P16 and Pair8 are both sixteen bytes and both made of two longs, and the only difference is that P16 carries a copy constructor of its own:

```cpp
// Sixteen bytes, but the copy constructor is non-trivial - do registers still apply?
struct P16 {
    long a, b;
    static long copies;
    P16(long a_, long b_) : a(a_), b(b_) {}
    P16(const P16& o) : a(o.a), b(o.b) { ++copies; }
};
long P16::copies = 0;

P16 make_p16(long a, long b) __attribute__((noinline));
P16 make_p16(long a, long b) { return {a, b}; }

int main() {
    P16 p = make_p16(1, 2);
    volatile long sink = p.a + p.b;
    return sink == 3 ? 0 : 1;
}
```

The compile command is `g++ -O0 -S -fno-asynchronous-unwind-tables r4a_nontrivial16.cpp -o r4a_nontrivial16.s`. Here is the function body verbatim, with the `.cfi_*` lines omitted:

```asm
_Z8make_p16ll:
	pushq	%rbp
	movq	%rsp, %rbp
	subq	$32, %rsp
	movq	%rdi, -8(%rbp)        ; the hidden sret pointer
	movq	%rsi, -16(%rbp)       ; argument a (bumped to rsi)
	movq	%rdx, -24(%rbp)       ; argument b (pushed on to rdx)
	movq	-24(%rbp), %rdx
	movq	-16(%rbp), %rcx
	movq	-8(%rbp), %rax
	movq	%rcx, %rsi
	movq	%rax, %rdi            ; the constructor's this = the sret slot
	call	_ZN3P16C1Ell          ; P16::P16(long,long) writes straight into the slot
	movq	-8(%rbp), %rax
	leave
	ret
```

And the call site on main's side:

```asm
	leaq	-32(%rbp), %rax
	movl	$2, %edx
	movl	$1, %esi
	movq	%rax, %rdi
	call	_Z8make_p16ll
```

The program's exit code is 0. The route we see is identical to Big's: rdi takes the hidden pointer, the two arguments step aside into rsi and rdx, the constructor's this is the sret slot and writes the data directly into the caller's memory, and rax hands the address back at the exit. Size doesn't get a vote here; there is one more rule sitting in the criteria, and it is about a type's category. Rule 2 of the ABI classification reads:

```text
2. If a C++ object is non-trivial for the purpose of calls, as specified in the
C++ ABI, it is passed by invisible reference (the object is replaced in the
parameter list by a pointer that has class INTEGER)
```

Footnote 7 supplies the reason:

```text
An object whose type is non-trivial for the purpose of calls cannot be passed
by value because such objects must have the same address in the caller and the
callee. Similar issues apply when returning an object from a function.
```

In plain terms: a C++ object that is non-trivial for the purpose of calls is passed by invisible reference, and a pointer takes the object's place in the parameter list. Non-trivial means the type carries user-provided copy control, code of that kind. The footnote's reason is blunter: such an object must have the same address in the caller and the callee. Your copy constructor and destructor run against the object itself, and if the two sides held different addresses, that code would be looking at two different objects, so the convention simply keeps the object behind a pointer the whole way through.

The plain-language version of the trivial test is this: a type with no custom copy control is moved by moving bytes, and the compiler may move it however it likes. Once you write a copy constructor, even one whose body just copies the two members over, the type becomes non-trivial and its register-splitting treatment is withdrawn. This also settles, ahead of time, something you might puzzle over in the next piece's counting experiment: Point is also sixteen bytes, and it goes through sret. Don't read that as the compiler regressing; it is the copy counter that switched the route. The three routes and the criteria are now complete. The next piece turns to the C++ language's own rules, to watch the copies on the return path vanish entirely.
