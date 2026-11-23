---
title: "The RAII Paradigm for OS Resources: One Skeleton for fds, HANDLEs, and Mappings"
description: "First article of the systems-programming concepts track: fds, HANDLEs, and memory mappings are all OS resources that must be returned once acquired. This article writes close/CloseHandle/munmap into destructors and gives the series' single definition site for the three move-only skeletons unique_fd, unique_handle, and mapped_region; measurements show a raw fd leaking 1000 times on the exception path while the RAII version leaks none, the close return value in a destructor discarded per man 2 close (no retry, no throw; fsync before close if you care about I/O errors), Windows keeping two failure values -1 and NULL side by side, CloseHandle silently returning TRUE on pseudo-handles with GetProcessHandleCount testifying throughout, the release/reset/swap hand-back semantics, vector relocation moving 7 times while shuffle moves zero, and a measured correction: the claim that a move constructor without noexcept falls back to copying does not hold for move-only types"
chapter: 8
order: 1
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 25
prerequisites:
  - "系统编程总纲:用户态、内核与两大阵营的地图"
related:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "mmap 内存映射:把文件贴进地址空间"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - Win32
  - RAII
  - 移动语义
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/thinking/01-raii-paradigm.md
  source_hash: f3adf0f9298496de50ea2a3ed3c33829274ba28ab87137ba664939cf60b24f89
  translated_at: '2026-11-22T15:44:36+00:00'
  engine: glm-5.3
  token_count: 24251
---

# The RAII Paradigm for OS Resources: One Skeleton for fds, HANDLEs, and Mappings

In the [overview](../00-overview.md) we made the acquaintance of the two camps; this article is where the real work begins. For the first piece of real work, the author would rather not start from any specific API, but from a trouble shared by all of them: fds (file descriptors), HANDLEs (Windows handles), and memory mappings — plus the processes and semaphores to come later — are all things you **must return once acquired**. An fd opened by open needs close, a handle from CreateFileW needs CloseHandle, and the stretch of address space mmap pasted in needs munmap. Forget to return any one of them and the resource hangs on the process for good; only when the process exits does the kernel tidy up after us.

The root of the trouble lies in C's calling conventions: returning the resource is a separate call with no syntactic binding whatsoever to the acquiring action. You open an fd inside a function; that function may have a normal return, three error branches, and possibly moments where an exception tips it over — every path leaving the scope needs us to remember one more close. Everyone knows it while writing; three months later the person adding the fourth branch is probably still you, and once it slips, it leaks. Talking about leaks in the abstract is pointless — let us lay the wound out directly.

Before laying hands on the keyboard, we state both labs' baselines once, since every later number is checked against them. The Linux-side experiments all come from the author's WSL2: kernel 6.18.33.2-microsoft-standard-WSL2, g++ 16.2.1, strace 7.2, an AMD Ryzen 9700X CPU, everything compiled with `g++ -std=c++20 -Wall -Wextra -O2`, zero warnings. The Windows side comes from the author's Win11 (build 26200.9457, system language Chinese), compiler MSYS2 UCRT64's g++ 16.1.0. We compiled and ran inside WSL via its interop channel, which lets you invoke Windows programs directly from the Linux side. Three practical notes: we cd into the source directory and turned every argument to `g++.exe` into a relative path; artifacts landing on WSL's filesystem carry no execute bit and need one `chmod +x` before they run; and all code and raw output live in the repository under `code/volumn_codes/vol8/systems-programming/` in the `thinking/01-raii-paradigm/` folders on both the linux and windows sides, captured 2026-10-02 — open them for verification whenever you like.

## How a Raw fd Leaks: One Thousand Exceptions, One Thousand Unclosed fds

We write a minimal leak scene: loop N times, each iteration opening a file inside a try block and then throwing. When the exception leaves the scope, nobody tidies up a raw fd, and in the catch we deliberately do not compensate:

```cpp
// leak_raw.cpp 的核心循环:裸 fd,异常路径每次漏一个
for (int i = 0; i < n; ++i) {
    try {
        int fd = ::open(kPath, O_RDONLY);   // 裸 fd,没有包装
        if (fd == -1) { ++open_failures; continue; }
        (void)fd;
        throw std::runtime_error("error path leaves the scope now");
    } catch (const std::exception&) {
        ++caught;   // catch 里也没有 close:fd 就这么留下了
    }
}
```

The program reads `/proc/self/fd` once before and once after (that directory spreads the process's currently open fds out as files; we used it in the overview), and subtracting one from the other yields the leak count. The numbers we actually measured:

```text
$ ./leak_raw 1000
RLIMIT_NOFILE: soft=1048576 hard=1048576
fds before (3): 0 1 2
fds after  (1003): 0 1 2 3 4 5 6 7 ... 999 1000 1001 1002
loop=1000 caught=1000 open_failures=0 leaked=1000
```

The loop ran a thousand times and leaked exactly a thousand: beyond the 0/1/2 standard streams the process starts with, the fd table climbed all the way to 1003. You might object that this program is a straw man — who throws right after open? The form is extreme, but the leaking pattern is universal: replace the throw with any early return, a continue, or a break in the loop, and the same batch leaks. The exception merely leaks the most silently, because the point where the scope is left is not written anywhere in your code.

Two environmental facts noted in passing. The first is about quota: `RLIMIT_NOFILE` governs the cap on fd count — what `ulimit -n` shows — and the author's WSL currently sits at soft=hard=1048576, so all 1000 opens succeeded. In an older environment with a 1024 quota, raising N and rerunning would hit EMFILE (errno 24, quota exhausted) the moment the fd table fills. That scene is familiar: [POSIX File I/O](../linux/file-io/01-posix-file-io.md)'s exp5 demonstrated it, pressing the quota down to 8 with setrlimit to manufacture one EMFILE on purpose. The second is about the starting point: having only 0/1/2 is not a constant — inherited fds WSL hands a process drift with the launch chain, and the same segment's exp5 once started with two extra, 5 and 10 — so do not memorize it as a baseline.

The control group changes one word: the fd from open is handed to a `unique_fd` wrapper, everything else untouched. The output looks like this:

```text
$ ./leak_raii 1000
RLIMIT_NOFILE: soft=1048576 hard=1048576
fds before (3): 0 1 2
fds after  (3): 0 1 2
loop=1000 caught=1000 leaked=0
```

The same thousand exceptions, and `leaked=0`. These two outputs placed one before the other are this article's entire motivation: whether something leaks should not depend on how good your memory is — it should depend on the type. Next we build that type with our own hands.

## unique_fd: Writing close into the Destructor

C++ has a proper name for this solution: RAII (Resource Acquisition Is Initialization), coined by Stroustrup, and the [vol2 deep dive on RAII](../../../vol2-modern-features/ch01-smart-pointers/01-raii-deep-dive.md) already walked you through the language layer. Applied to the matter at hand, the idea is one sentence: wrap the resource and its lifetime in a stack object — acquire in the constructor, return in the destructor. The language guarantees for us that whether the scope is left by running to the end, by an early return, or by exception-driven stack unwinding, the stack object's destructor runs. From then on nobody needs to remember to write close; it becomes the sentence "the object is gone, so the resource is gone."

Here is `unique_fd` in full — the series' shared vocabulary, defined exactly once here, with the later Linux-side articles referencing it directly. The code comes from `common/raii.hpp` in the experiment archive, compiled with `g++ -std=c++20 -Wall -Wextra -O2`, zero warnings:

```cpp
class unique_fd
{
public:
    explicit unique_fd(int fd = -1) noexcept : fd_(fd) {}

    unique_fd(unique_fd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }

    unique_fd& operator=(unique_fd&& other) noexcept
    {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    ~unique_fd() { reset(); }

    int get() const noexcept { return fd_; }

    // 放弃所有权:fd 交还调用方,此后析构不再 close
    int release() noexcept { return std::exchange(fd_, -1); }

    explicit operator bool() const noexcept { return fd_ >= 0; }

    // 把手里现有的 close 掉,再接管新 fd;无参调用 = close + 变空
    void reset(int fd = -1) noexcept
    {
        if (fd_ >= 0) {
            ::close(fd_);   // 返回值直接丢弃,理由见下一节
        }
        fd_ = fd;
    }

    void swap(unique_fd& other) noexcept { std::swap(fd_, other.fd_); }
    friend void swap(unique_fd& a, unique_fd& b) noexcept { a.swap(b); }

    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;

private:
    int fd_;
};
```

Let us walk it line by line. The copy constructor and copy assignment are deleted outright with `= delete`: fd ownership must be single — if two unique_fds both believed they held fd 3, destruction would close the same fd twice. With copying deleted, the type has only the move road left, the move-only type we met in vol2's [move semantics](../../../vol2-modern-features/ch00-move-semantics/02-move-semantics.md): the move constructor takes the other's fd and hollows the other out to -1. The move assignment resets itself first, then takes over the newcomer. Destruction goes through reset uniformly. The mental model is fully isomorphic to `std::unique_ptr`, only the underlying asset swaps a heap pointer for an int.

The explicit on `explicit operator bool` is deliberate, though what it blocks is not what many assume. It does not block conditional contexts like `if (fd)`: contextual conversion leaves the door open for explicit, and `if (fd)` compiles just fine. What it truly intercepts is implicit conversion of the object to a value. We compiled a minimal class with only this member on the spot to verify: for `bool ok = obj;` GCC answers `cannot convert to 'bool' in initialization`; for `obj == 0` it reports no match for 'operator=='. Drop the explicit and both lines quietly compile, the object gets treated as a boolean for arithmetic and compared against integers, and the hidden danger is buried deep. `release`, `reset`, and `swap` are the exits and exchanges of ownership; we save them for a dedicated section later.

Two companion tools of no more than ten lines each, used by the example code later:

```cpp
// errno 装箱:errno 是线程局部的,读进 error_code 就定格了
inline std::error_code errno_code() noexcept
{
    return std::error_code{errno, std::generic_category()};
}

// 任何返回 -1 表失败的 syscall 都从这儿过:失败抛 system_error,what 带前缀
template <class F, class... Args>
auto sys_call(const char* what, F&& f, Args&&... args)
{
    auto result = std::forward<F>(f)(std::forward<Args>(args)...);
    if (result == -1) {
        throw std::system_error{errno, std::generic_category(), what};
    }
    return result;
}
```

Usage goes like this: `sys_call("open", ::open, path, flags, mode)` throws `std::system_error` on failure with an `open:` prefix in the message, so which step blew up is visible at a glance. How errno and GetLastError (the Windows side's error-reporting channel, stored per thread just like errno) get boxed, and how errors split between the toolkit layer and the application layer — that full story belongs to this series' next article, "The Error-Handling Paradigm: From errno to expected"; here we borrow only their minimal form.

## Under strace: close Happens in Pairs

That the destructor really closes should not be taken on faith. We write a four-act little program: create a file, leave one scope normally, open inside a function and return immediately (writing not a single close), and set up one mapping. The third act matters most; its code:

```cpp
// 提前 return 的典型现场:错误分支各写一个 close 的活,交给析构
static bool early_return_demo()
{
    unique_fd fd{
        sys_call("open", ::open, kPath, O_WRONLY | O_CREAT | O_TRUNC, 0666)};
    write_all(fd.get(), "written just before the early return\n");
    std::printf("[early-return] fd=%d still open, returning now\n", fd.get());
    return false;   // 一个 close 都没写,~unique_fd() 在栈上收尾
}
```

Then we bring in strace, tracing only openat and close. It records every syscall the process makes, one line each, and we count the pairings:

```text
$ strace -f -e trace=openat,close ./strace_demo
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
close(3)                                = 0
...（动态链接器开 .so 的另外 4 对 openat/close,全带 O_CLOEXEC,略）...
openat(AT_FDCWD, "/tmp/raii_lab/01-strace/note.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
close(3)                                = 0
openat(AT_FDCWD, "/tmp/raii_lab/01-strace/note.txt", O_RDONLY) = 3
close(3)                                = 0
openat(AT_FDCWD, "/tmp/raii_lab/01-strace/note.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
close(3)                                = 0
openat(AT_FDCWD, "/tmp/raii_lab/01-strace/note.txt", O_RDONLY) = 3
close(3)                                = 0
+++ exited with 0 +++
```

The four pairs at the trace's tail correspond exactly to the program's four acts: creating the file, reading, the write on the early-return path, and the read before the mapping. Point your lens at the third pair — that is early_return_demo's fd; we wrote not one close for it, yet close came anyway, right on that return's road. That is the destructor doing its work, on every leaving path. As for the loader pairs at the start: the dynamic linker opens .so files with `O_CLOEXEC` throughout, a phenomenon recorded in a sidebar of the [POSIX File I/O](../linux/file-io/01-posix-file-io.md) article, not part of the experiment itself.

## The close Return Value in a Destructor: The Right Handling of EINTR

With the skeleton standing, one question can no longer be dodged: can `close()` itself fail? It can, and man 2 close's ERRORS lists more than two. EBADF says the fd is invalid, which announces a different bug already present in the program — no point remedying. EIO is a real I/O error. ENOSPC and EDQUOT (space exhausted, quota exhausted) carry a special note on NFS-style filesystems: the overdraft often goes unreported at first write and only surfaces at a later write, fsync, or close. The genuinely troublesome one is EINTR (an errno value meaning interrupted by a signal).

Many people's intuition here is "interrupted, so close it again." The man page's NOTES section is titled Dealing with error returns from close(); here is its original wording:

> The author copies the original wording here, untouched: `Retrying the close() after a failure return is the wrong thing to do, since this may cause a reused file descriptor from another thread to be closed.`

Why is retrying wrong? Because the Linux kernel always releases the fd itself early in the close operation; from then on the number can be reused at any moment by another thread's open, while the step that can actually fail (flushing data to the filesystem or device) happens in the latter half. You receive a failed close, we close again, and the second attempt may land on a fresh fd someone else just acquired — good intentions doing harm.

EINTR also has a peculiar biography. POSIX.1-2008 says only that the fd's state is unspecified in this case. Linux and most implementations behave so that when EINTR is returned the fd is guaranteed already closed — the kernel merely reports in passing that a signal arrived. HP-UX is the documented counterexample: its fd is still open on EINTR, and there you must close again. With POSIX.1-2024, the standard settled on the HP-UX behavior, which made Linux a member of the "not conforming to the new standard" club, and the man page states plainly that no change is planned. No need to feel embarrassed on the kernel's behalf; just record Linux's actual behavior.

Applied to our destructor, the adopted ruling is one line: **the close return value is discarded outright**. No retry — the reasons are above. No throw either: destructors inherently run in a noexcept context, and a throw here ends in std::terminate, a far worse price than one leaked fd. What could the discarded return value have told us? Whether the flush in close's latter half actually hit an I/O error. If you genuinely care, the man page points us careful programmers to the proper road: to learn about I/O errors, call fsync(2) on the same fd before close and check fsync's own return value. It also warns that ignoring close's return value can cause silent data loss, with NFS and disk quotas the typical occasions. The full story of the page cache and fsync awaits you in the corresponding section of [POSIX File I/O](../linux/file-io/01-posix-file-io.md). For now we accept one rule: the questions worth asking get moved to fsync.

## mapped_region: A Different Asset, the Same Skeleton

Next we put the same skeleton on a second class of resource: memory mappings. mmap pastes a file into the address space and munmap handles the return; leaking it leaks a stretch of address space. The wrapper's skeleton is identical to unique_fd — the move-only approach, destruction finishing through reset — with only two spots deserving a second look, placed after the code:

```cpp
class mapped_region
{
public:
    // release() 的带走清单:想手动 munmap,要的正是这两样
    struct released {
        unsigned char* addr;
        std::size_t length;
    };

    mapped_region() noexcept = default;

    mapped_region(const unique_fd& fd, std::size_t length, int prot, int flags,
                  off_t offset = 0)
    {
        // mmap 失败返回 MAP_FAILED(即 (void*)-1)并设 errno,不是 nullptr
        void* p = ::mmap(nullptr, length, prot, flags, fd.get(), offset);
        if (p == MAP_FAILED) {
            throw std::system_error{errno_code(), "mmap"};
        }
        addr_ = static_cast<unsigned char*>(p);
        const std::size_t page =
            static_cast<std::size_t>(::sysconf(_SC_PAGE_SIZE));
        length_ = (length + page - 1) / page * page;   // 按页向上取整,记下真实长度
    }

    mapped_region(mapped_region&& other) noexcept
        : addr_(other.addr_), length_(other.length_)
    {
        other.addr_ = nullptr;
        other.length_ = 0;
    }

    mapped_region& operator=(mapped_region&& other) noexcept
    {
        if (this != &other) {
            reset();
            addr_ = std::exchange(other.addr_, nullptr);
            length_ = std::exchange(other.length_, 0);
        }
        return *this;
    }

    ~mapped_region() { reset(); }

    void reset() noexcept
    {
        if (addr_ != nullptr) {
            ::munmap(addr_, length_);   // 失败多半意味着地址或长度已经错了,吞掉返回值
            addr_ = nullptr;
            length_ = 0;
        }
    }

    released release() noexcept
    {
        return released{std::exchange(addr_, nullptr),
                        std::exchange(length_, 0)};
    }

    unsigned char* data() const noexcept { return addr_; }
    std::size_t size() const noexcept { return length_; }   // 取整后的映射长度
    explicit operator bool() const noexcept { return addr_ != nullptr; }

    // reset(addr, length):接管一段裸映射的重载,略
    // swap:成员与 friend 各一份,形态与 unique_fd 那对相同,略

    mapped_region(const mapped_region&) = delete;
    mapped_region& operator=(const mapped_region&) = delete;

private:
    unsigned char* addr_ = nullptr;
    std::size_t length_ = 0;
};
```

The first spot of trouble is the failure value. A failed mmap returns `MAP_FAILED` (that is, `(void*)-1`) and sets errno — not nullptr — so `sys_call`'s compare-the-result-with--1 check does not compile here; a void* cannot compare with an integer, and the constructor hand-writes the check. `if (p)` cannot detect the failure either, since it is always true for `(void*)-1`.

The second refinement is length padded to the page boundary. The kernel rounds length up to a page for us, and munmap wants exactly the rounded length, so the constructor computes and records it itself, and `size()` reports the rounded length too. The matter is visible at the syscall level — switch strace to tracing mmap and munmap:

```text
$ strace -f -e trace=mmap,munmap ./strace_demo
...（加载器把 libc 等映射进来的二十来行 mmap,略）...
mmap(NULL, 16, PROT_READ, MAP_PRIVATE, 3, 0) = 0x78f3972c3000
munmap(0x78f3972c3000, 4096)     = 0
+++ exited with 0 +++
```

We asked for 16 bytes, the kernel pasted in a whole page, and the destructor hands back 4096. The program's own print corroborates it: `asked 16 B, region claims 4096 B`. `release()` hands the caller a `released` struct carrying both address and length — exactly the full parameter set a manual munmap needs. As for the going rate of mmap's six parameters, which signals out-of-bounds access triggers, and how copy-on-write works, those are the main topics of the entire [mmap memory mapping](../linux/file-io/02-mmap-memory-mapping.md) article; we do not poach them here.

## The Windows Side: unique_handle and Two Failure Values

When Windows's turn comes, we refit the same skeleton with parts from that side: the fd becomes a HANDLE (an opaque pointer-sized value), CloseHandle takes over close's job, and the null value -1 becomes `INVALID_HANDLE_VALUE` (also -1). Not another word of the rest needs changing — this is where the two camps converge at the C++ layer. Before including `<windows.h>`, two macros must be defined: `WIN32_LEAN_AND_MEAN` keeps out a batch of headers we do not need this time, and `NOMINMAX` stops the min/max macros in windows.h, which would otherwise collide with `std::min`/`std::max`:

```cpp
class unique_handle
{
public:
    explicit unique_handle(HANDLE h = INVALID_HANDLE_VALUE) noexcept : h_(h) {}
    unique_handle(unique_handle&& o) noexcept
        : h_(std::exchange(o.h_, INVALID_HANDLE_VALUE)) {}
    unique_handle& operator=(unique_handle&& o) noexcept
    {
        if (this != &o) { reset(o.release()); }
        return *this;
    }
    ~unique_handle() { reset(); }

    HANDLE get() const noexcept { return h_; }
    HANDLE release() noexcept { return std::exchange(h_, INVALID_HANDLE_VALUE); }
    void reset(HANDLE h = INVALID_HANDLE_VALUE) noexcept
    {
        if (h_ != INVALID_HANDLE_VALUE) { ::CloseHandle(h_); }
        h_ = h;
    }
    explicit operator bool() const noexcept { return h_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE h_{INVALID_HANDLE_VALUE};
};
```

The skeleton is fine; the real fork is in the failure values. The POSIX side at least unified: a failed syscall always returns -1. Win32 keeps two failure sentinels standing at once. The term sentinel value now earns its keep: it means the specific return value an API's contract uses to mark failure — POSIX's -1 is one such, and Windows' two look nothing alike. Here is the measured matrix (the error codes are the GetLastError values read immediately after):

| API (how it fails) | Return value on failure | GetLastError |
| ----------------------------- | ---------------------------------- | ------------ |
| CreateFileW (OPEN_EXISTING, file missing) | INVALID_HANDLE_VALUE (hex ffffffffffffffff) | 2 (ERROR_FILE_NOT_FOUND) |
| CreateEventW (name collides with an existing mutex)       | NULL                               | 6 (ERROR_INVALID_HANDLE) |
| CreateFileMappingW (name collides with an existing mutex) | NULL                               | 6 (ERROR_INVALID_HANDLE) |
| OpenProcess (bogus pid)                  | NULL                               | 87 (ERROR_INVALID_PARAMETER) |

The CreateFileW family returns -1 on failure, while CreateEventW returns NULL on failure, CreateFileMappingW also NULL, and OpenProcess the same. Get the check backwards and a silent bug awaits — so before writing any failure checks, read the documentation's Return value section; memorization alone will not do. Beyond the matrix we keep one more control row, the one the author finds most interesting: `CreateFileMappingW(INVALID_HANDLE_VALUE, ...)` is **not a failure** — the -1 passed in means "a mapping backed by the page file": the page file is Windows' virtual-memory backing store, a mapping built on it is attached to no file, and the data lands in the page file. In the measurement it successfully returned a valid handle. Savor -1's double identity: in CreateFileW's return-value slot it means failure; moved to CreateFileMappingW's parameter slot it becomes a legitimate input. The same number in a different position changes identity entirely.

> The BOOL-returning APIs (ReadFile/WriteFile/CloseHandle and friends) fail with FALSE, details likewise read via GetLastError. For judging these two sentinel families we have a ready template: check_win32, which branches on the return type with if constexpr — fully covered in the [Win32 File I/O](../windows/file-io/01-win32-file-io.md) article.

## CloseHandle's Sentinel Reactions and a NULL Trap

unique_handle takes `INVALID_HANDLE_VALUE` as its null sentinel — so how does CloseHandle itself react to various strange inputs? A very small experiment: place a sentinel value into the last-error slot with `SetLastError(1234)`, then call CloseHandle once on each of five inputs, watching both the return value and the sentinel's movements:

```text
$ ./e1b_closehandle_sentinel
CloseHandle(NULL                   =                0) -> ret=0 err=6
CloseHandle(INVALID_HANDLE_VALUE   = ffffffffffffffff) -> ret=1 err=1234
CloseHandle(GetCurrentProcess()    = ffffffffffffffff) -> ret=1 err=1234
CloseHandle((HANDLE)-2             = fffffffffffffffe) -> ret=1 err=1234
CloseHandle((HANDLE)0x1234         =             1234) -> ret=0 err=6
```

Line by line. NULL and the wild value 0x1234 both get FALSE plus err=6 (ERROR_INVALID_HANDLE), the sentinel overwritten — an ordinary failure. The middle three lines are the interesting ones: they return TRUE with err still 1234, our planted sentinel untouched, the last-error slot never visited. -1 and -2 are **pseudo handles**: GetCurrentProcess() returns -1 and GetCurrentThread() returns -2; they occupy no slot in the handle table and are resolved on the spot to "current process / current thread" each time they are used. Calling CloseHandle on a pseudo handle finds no slot to close at all, so we observed nothing being closed, yet it returned TRUE.

Can this TRUE serve as a basis for judgment? No. On closing with a pseudo handle, the documentation states exactly one sentence — GetCurrentProcess's Remarks say that calling CloseHandle on a pseudo handle has no effect; closing it equals not closing it. The details of the TRUE return and the unmoved last-error slot are the author's measured findings on Win11 26200; the documentation does not go to that layer. The documentation's Return value section does carry one more item: when the program runs under a debugger, CloseHandle given an invalid handle **or a pseudo handle** raises an exception outright. Silently returning TRUE in normal runs, possibly blowing up on the spot under a debugger — that combination alone tells us not to write any code depending on this TRUE. The fate of a double close is also recorded: closing the same valid handle twice gets FALSE plus err=6 the second time, the same treatment as NULL.

What testifies to unique_handle's lifecycle, then? Windows provides a ready-made readout function: GetProcessHandleCount, which reads the total number of handles the process currently holds — what Task Manager's "Handles" column shows. We read the count at every step of the full move suite. The output block below, together with the later NULL-trap block, are excerpts from the same experiment program; it is divided into five segments, of which we carry only [3] lifecycle and [4] trap, with the numbering kept from the archive:

```text
[3] unique_handle 生命周期(数字 = 进程当前句柄数)
  baseline                       : 61
  构造 a(接管 CreateFileW)      : 62  bool(a)=1 get=00000000000000f8
  move 构造 b<-a                 : 62  bool(a)=0 bool(b)=1
  b.release() 所有权交还         : 62  bool(b)=0 raw=00000000000000f8
  手动 CloseHandle(raw)          : 61
  a.reset(新句柄)                : 62  bool(a)=1
  a.reset() 立即关闭             : 61  bool(a)=0
  move 赋值 c<-临时              : 62
  作用域结束(析构全部执行)     : 61  (回到 baseline=61)
```

Read the numbers against the operations: the constructor takes over and the count rises from 61 to 62. At move construction and move assignment the count does not move — the handle itself never changes place; only the wrapper on our side moves. After release the count likewise holds: ownership is handed out, the handle stays open, and only our manual CloseHandle brings it back to 61. reset(new handle) raises one, reset() immediately drops it back. When the scope ends, the destructors close everything that should close, returning precisely to baseline. The absolute value 61 floats with the process environment — yours will probably differ — and matching the rise-and-fall pattern is what counts as passing.

Finally we step on the NULL trap on purpose. CreateEventW returns NULL on a name collision, and we stuff that failure value straight into unique_handle:

```text
[4] 陷阱:把 CreateEventW 的失败值 NULL 直接塞给 unique_handle
  bool(bad)=1  —— NULL != INVALID_HANDLE_VALUE,-1 哨兵把它判成了"有效"
  析构里 CloseHandle(NULL) 留下 err=6(6=ERROR_INVALID_HANDLE,无害但语义错)
```

`bool(bad)=1`: a failure value judged valid by the RAII class, and the destructor even earnestly called CloseHandle on NULL. The program does not crash — it merely leaves err=6 behind — but the semantics are entirely wrong. The reminder: unique_handle recognizes only the -1 family of sentinels; for NULL-family APIs, the failure check must happen before the value enters the RAII wrapper, which is exactly why check_win32 exists.

## Hand-Back Semantics: release/reset/swap

The skeleton covers half the problem — forgetting to return — and the other half goes to release, reset, and swap, governing the active dispatching of ownership. We use /dev/null as the test subject (open always succeeds and no disk is consumed), with strace recording alongside the whole time:

```text
$ ./release_reset_swap
== 1) release(): give up ownership ==
[open     ] a -> fd 3
[release  ] a gave up fd 3, a now holds nothing
[manual   ] ::close(3) done — the only close it gets
== 2) reset(new_fd): close old, take new ==
[open     ] b -> fd 3
[open     ] c -> fd 4
[reset(new)] b closed its old fd and now holds 4
[reset()  ] b.reset() -> b now holds nothing
== 3) swap(): exchange roles ==
[open     ] x -> fd 3
[open     ] y -> fd 4
[swap     ] x holds 4, y holds 3
== done ==
```

What `release()` does is give ownership back to the caller: the object hands over the fd and empties itself, and the destructor never touches it again. It is not abandonment — the fd stays happily open; only its attribution moves from the object to you — and the real uses are all handovers: a dup2 redirection is done and we hand the original fd to someone else to finish with, or we pass the fd to a C interface that accepts only a bare int. Once handed over, the destructor naturally leaves it alone; strace can testify, with that fd receiving only our single manual close for the entire run:

```text
openat(AT_FDCWD, "/dev/null", O_RDONLY) = 3
close(3)                                = 0      <- release 之后调用方手动的唯一一次
```

`reset` works in both directions. The parameterized call closes what is held and takes over the new fd — in the strace, close(3) falls right on b's road to acquiring 4. The bare call is a plain close plus empty; after close(4), b holds nothing. `swap` exchanges ownership, not numbers: x and y each hold an fd, and after the swap it is x holds 4, y holds 3 — the fd number follows ownership, not the variable name. For the post-swap cleanup, straight to the two lines from the archived strace:

```text
close(3)                                = 0      <- y 声明得晚、析构得早,它手里的 3 被关掉
close(4)                                = 0      <- 轮到 x,给它手里的 4 收尾
```

At this moment 3 is in y's hand; y was declared later and its destruction arrives earlier — objects on the stack are always destroyed in the reverse order of declaration, and the arrangement of these two close lines is precisely that rule landed on strace. mapped_region's same three functions take identical shapes; its release hands back address plus length, the released struct we saw above.

## Into Containers: unique_fd in a vector

Where move-only types most often end up is containers. We stuff unique_fd into a `std::vector` and let the experiment count a few things for us: how many moves relocation performs, how many moves shuffle and sort each cause, and whether anything leaks when the scope ends. The counting relies on an observing subclass: we layer a move-construction counter on top of unique_fd, leaving the resource semantics in the base class:

```cpp
// 计数用的观测子类:叠加一个移动构造计数,资源语义全在基类
class counted_fd : public unique_fd
{
public:
    explicit counted_fd(int fd = -1) noexcept : unique_fd(fd) {}

    counted_fd(counted_fd&& other) RAII_MOVE_NOEXCEPT
        : unique_fd(std::move(other))
    {
        ++s_moves;
    }
    // ...移动赋值、swap、静态计数器略

private:
    static int s_moves;
};
```

`RAII_MOVE_NOEXCEPT` is the experiment's switch: built by default it is `noexcept`; compiled again with `-DRAII_MOVE_MAY_THROW`, it becomes a bare, potentially throwing move constructor. We ran both programs:

```text
$ ./vector_noexcept
counted_fd: copy_constructible=0 nothrow_move_constructible=1 (RAII_MOVE_MAY_THROW not defined)
== A) emplace_back x 8, no reserve ==
   size=8 capacity=8 moves=7
   fds (8): 3 4 5 6 7 8 9 10
== B) shuffle (mt19937, fixed seed) ==
   fds (8): 3 7 6 8 5 4 10 9
   moves during shuffle=0 (swap moves no bytes)
== C) sort by fd ==
   fds (8): 3 4 5 6 7 8 9 10
   moves during sort=7
   fd multiset unchanged: yes
== D) scope end: vector destructs, 8 fds close ==
fds: before=3 after=3 (back to baseline: yes)
== E) reserve(8) first, then emplace_back x 8 ==
   size=8 capacity=8 moves=0
   fds (8): 3 4 5 6 7 8 9 10
== done ==
```

In segment A we append 8 without reserve; capacity walks the three doublings 1, 2, 4, 8, each doubling relocating the old elements, and 1+2+4 makes exactly 7 move constructions. Only the wrapper moves: fd numbers 3 through 10 never change, the kernel sees not one close, and what moves is the bookkeeping layer of "which object holds it." Then segment E: after reserve(8) we append the same 8, relocation drops to zero, capacity arrives in one step, and even those 7 moves are saved.

The B-versus-C contrast is the author's favorite. After shuffle the fd order is genuinely scrambled (3 7 6 8 5 4 10 9), yet move constructions number zero: std::shuffle works through element-level swap, and our swap exchanges two ints without moving a single byte. When sort's turn comes, 7 move constructions happen in earnest while the fd multiset stays untouched — the multiset-unchanged check says yes. So algorithmic moving and growth relocation are two different things; the answer follows the algorithm's implementation, not intuition. And segment D: the scope ends, the vector's destruction takes its 8 elements down one by one, and the fd count returns to baseline with not one leaked.

Then there is the widely circulated claim: a move constructor without noexcept makes vector relocation fall back to copying. For copyable types the claim holds — the mechanism is `std::move_if_noexcept`: when moving might throw and copying is affordable, it copies, so a failed copy can be discarded wholesale while the original vector stays intact. But for our kind of type, with copying deleted, there is no fallback even if it wanted one: move_if_noexcept in the standard yields an lvalue reference only when the type is copyable; move-only types always receive an rvalue. The measurement agrees: apart from the first banner line, the throwing build's output matches the noexcept build field for field, and the 7/0/7/0 counts differ nowhere:

```text
$ ./vector_throwing
counted_fd: copy_constructible=0 nothrow_move_constructible=0 (RAII_MOVE_MAY_THROW defined)
（其余输出与 noexcept 版逐字段相同:moves 7 / 0 / 7 / 0）
```

So is the noexcept wasted? Not that either. What non-noexcept genuinely loses is vector's strong exception guarantee: if a move constructor throws midway through relocation, neither the old nor the new buffer is complete and the data structure cannot return to its original shape. Resources still do not leak — destruction during unwinding closes every element as usual; what rots is only the container layer's integrity. So we keep marking our move constructors noexcept — all the more so since, for these classes of ours, noexcept is simply the truth.

## Where the Three Tools Go

From this article on, the unique_fd, mapped_region, and unique_handle in our hands are the series' shared vocabulary; their single definition site lands here, and later articles reference them without redefining. The [POSIX File I/O](../linux/file-io/01-posix-file-io.md) article walks them through an fd's whole life, and the [mmap memory mapping](../linux/file-io/02-mmap-memory-mapping.md) article uses mapped_region from head to tail. The Windows side's [Win32 File I/O](../windows/file-io/01-win32-file-io.md) is more direct still — its file copier already puts unique_handle to work. To rerun this article's experiments, the READMEs in the two archive locations described at the top have every command written out.

The next article, "The Error-Handling Paradigm: From errno to expected," covers error handling in full: the thread-local nature of errno and GetLastError, the discipline of boxing failures immediately, and the two-exit convention — expected at the toolkit layer, system_error at the application's top. The full story of the Windows-side check_win32 stays in the [Win32 File I/O](../windows/file-io/01-win32-file-io.md) article. With this article written, forgetting to close resources has found its settlement. What to do about inaccurately reported errors — the next article takes care of that.

<ReferenceCard title="Reference Resources">
  <ReferenceItem
    :id="1"
    title="close(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/close.2.html"
  />
  <ReferenceItem
    :id="2"
    title="mmap(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/mmap.2.html"
  />
  <ReferenceItem
    :id="3"
    title="CloseHandle function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle"
  />
  <ReferenceItem
    :id="4"
    title="GetProcessHandleCount function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesshandlecount"
  />
  <ReferenceItem
    :id="5"
    title="GetCurrentProcess function (Remarks: pseudo handles and CloseHandle)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentprocess"
  />
  <ReferenceItem
    :id="6"
    title="std::move_if_noexcept"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/utility/move_if_noexcept"
  />
  <ReferenceItem
    :id="7"
    title="std::unique_ptr"
    publisher="cppreference.com"
    url="https://en.cppreference.com/w/cpp/memory/unique_ptr"
  />
</ReferenceCard>
