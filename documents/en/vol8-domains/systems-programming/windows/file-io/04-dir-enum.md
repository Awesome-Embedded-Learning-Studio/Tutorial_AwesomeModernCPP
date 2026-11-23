---
title: "Directory Enumeration and the NTFS Family"
description: "The fourth chapter, mirroring std::filesystem on the Windows side. The FindFirstFileW/FindNextFileW/FindClose trio brings back the name, attributes, three timestamps, and 64-bit size in one shot (a 4.5 GiB logical size proves nFileSizeHigh carries cargo). NTFS's enumeration order measured as case-folded lexicographic order while the documentation plainly says it does no sorting (the counterpoint to ext4's hash order). An empty directory with * happily yields . and .. (the error-code trichotomy: directory missing 3, pattern no match 2, normal end 18). *.htm hitches a ride on 8.3 short names to catch longname.html. Among the nine-output-postures matrix for wide-character names, the only one that gets everything right is manual WideCharToMultiByte(CP_UTF8) (the C locale's %ls silently drops characters while still reporting success, and the .UTF8 locale still drops surrogate-pair emoji). Recursion does not descend into junctions by default, and the follow mode's loop gets stopped by MAX_PATH after 22 laps (the same scenario on Linux is the kernel's 40-level ELOOP). The NTFS family matrix (hard links share one FileIndex, symbolic links cannot be created without privilege — 1314, dangling-junction queries never follow the target, a sparse 1 GiB file actually occupies 128 KiB, and exFAT as the negative control rejects everything). FindFirstFileExW's LARGE_FETCH runs steadily about 19% faster, but a hint bit is not a promise bit. LongPathsEnabled=1 only lets through applications with a longPathAware manifest, while the device path prefix lifts the ceiling to 32767."
chapter: 8
order: 4
platform: host
difficulty: intermediate
cpp_standard: [20]
reading_time_minutes: 21
prerequisites:
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
related:
  - "std::filesystem:目录与元数据"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - Win32
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
translation:
  source: documents/vol8-domains/systems-programming/windows/file-io/04-dir-enum.md
  source_hash: f8dfb859d1c46c4f89520ea54373b1285804489d6e50eebec03c7093022d7d60
  translated_at: '2026-11-22T15:46:37+00:00'
  engine: glm-5.3
  token_count: 25840
---
# Directory Enumeration and the NTFS Family

In the previous chapter we walked the exception dispatch chain in the company of `0xC0000005`. This chapter returns to far calmer calls — the subject is directories. Through the first three chapters what we held was a single file: opening it, reading it, writing it, building mappings for it were all its show. Once the files in hand multiply, the questions have to change: how do we take the names inside a directory out one by one? Do the names we get come with attributes? And when a name contains both Chinese and an emoji, how do we write it to the terminal intact? On the Linux side, [std::filesystem: Directories and Metadata](../../linux/file-io/04-filesystem.md) put ext4 through the same set of questions, and this chapter is its Windows mirror. Shared mechanisms — directory traversal, recursion, paths — we treat mirrored on the same themes. The forks — NTFS's link family, ext4's hash order — both sides cover in full.

The numbering and environment, stated up front as usual. This chapter's experiments are numbered e1 through e5, following the five archive subdirectories, all collected under `code/volumn_codes/vol8/systems-programming/windows/file-io/04-dir-enum/`. `01-findfirst/` houses three programs (e1_trio, e1_errors, e1_wide_out), each of the other four directories houses one, and the numbers are independent of W01, W02, and W03's e series — go by the file names and you will not mistake anyone. The machine is a Win11 26200 Chinese-locale system (ANSI code page 936, that is GBK), the compiler MSYS2 UCRT64 g++ 16.1.0, the compile command uniformly `-std=c++20 -Wall -Wextra`, with only e4's timing adding `-O2`. All data lives on real NTFS under `C:`'s `%TEMP%`. The cross-volume comparison used F: (also NTFS), and the exFAT negative control used D:. Two identity facts to keep in mind: not an administrator, and Developer Mode is off on this machine — both become the very real error code 1314 in e3. The outputs were captured on 2026-10-02.

The tools follow the series' division of labor as before. `unique_handle` and `last_error_code` are defined in [The RAII Paradigm for OS Resources](../../thinking/01-raii-paradigm.md) and [The Error-Handling Paradigm](../../thinking/02-error-paradigm.md), `check_win32` in [Win32 File I/O](01-win32-file-io.md). Here we only reference them and do not redefine. Two additions arrive, both collected in the archive's `common/win_dir.hpp`:

```cpp
// win_dir.hpp(节选)——本篇新增的两件工具
class unique_find {
public:
    explicit unique_find(HANDLE h = INVALID_HANDLE_VALUE) noexcept : h_(h) {}
    unique_find(unique_find&& o) noexcept : h_(std::exchange(o.h_, INVALID_HANDLE_VALUE)) {}
    unique_find& operator=(unique_find&& o) noexcept
    {
        if (this != &o) { reset(o.release()); }
        return *this;
    }
    ~unique_find() { reset(); }

    HANDLE get() const noexcept { return h_; }
    explicit operator bool() const noexcept { return h_ != INVALID_HANDLE_VALUE; }
    void reset(HANDLE h = INVALID_HANDLE_VALUE) noexcept
    {
        if (h_ != INVALID_HANDLE_VALUE) { ::FindClose(h_); }   // 查找句柄的关法是 FindClose
        h_ = h;
    }

private:
    HANDLE h_{INVALID_HANDLE_VALUE};
};

// 宽字符名 -> UTF-8:WIN32_FIND_DATAW 的名字是 wchar_t,存档的 .out 要的是 UTF-8 字节
inline std::string to_utf8(const wchar_t* ws)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n - 1 : 0), '\0');
    if (n > 0) { WideCharToMultiByte(CP_UTF8, 0, ws, -1, s.data(), n, nullptr, nullptr); }
    return s;
}
```

`unique_find` takes care of the find handle that FindFirstFileW hands back: the failure value is still INVALID_HANDLE_VALUE, but the function that closes it is FindClose, not CloseHandle — a point the documentation makes a point of noting too. Why `to_utf8` looks the way it does, and why we do not simply printf, is exactly what the e1_wide_out experiment answers, and we meet the verdict in the wide-character section.

## The Trio: Names with Attributes, in One Trip

The protagonists of single-level traversal are three functions: `FindFirstFileW` takes a pattern string with wildcards, hands back a find handle, and writes the first entry into `WIN32_FIND_DATAW` as a bonus. `FindNextFileW` fetches the entries that follow. `FindClose` handles the closing. The core loop in our e1_trio is just these few lines:

```cpp
// e1_trio.cpp(节选):dump_dir 的主干(detail 分支)
WIN32_FIND_DATAW fd{};
HANDLE h = FindFirstFileW(pattern, &fd);
if (h == INVALID_HANDLE_VALUE) {
    printf("    FindFirstFileW(\"%s\") -> 失败 err=%lu\n", to_utf8(pattern).c_str(),
           GetLastError());
    return -1;
}
unique_find guard{h};
int n = 0;
do {
    ++n;
    char bits[256];
    attr_bits(fd.dwFileAttributes, bits);   // 属性位解码成可读名
    unsigned long long size =
        ((unsigned long long)fd.nFileSizeHigh << 32) | (unsigned long long)fd.nFileSizeLow;
    printf("    [%d] %-22s attr=%s\n", n, to_utf8(fd.cFileName).c_str(), bits);
    printf("        大小 %llu(0x%llx)= High 0x%lx << 32 | Low 0x%lx\n", size, size,
           (unsigned long)fd.nFileSizeHigh, (unsigned long)fd.nFileSizeLow);
} while (FindNextFileW(guard.get(), &fd));
```

The fields of `WIN32_FIND_DATAW` deserve to be introduced one by one, because they are far richer than Linux's readdir. `cFileName` holds the long name, `cAlternateFileName` the 8.3 short name (its story comes later), `dwFileAttributes` gives the attribute bits, the creation, access, and modification times each occupy a FILETIME, and the size is split into two 32-bit halves, `nFileSizeHigh` and `nFileSizeLow`, that assemble into the 64-bit value. Linux's readdir spits out only a name plus a d_type, and L04 tallied that price for us: type-checking is free courtesy of the d_type cache, but asking the size means one added stat per entry — a ten-thousand-file directory rising from 3.42 ms to 16.22 ms. Windows' enumeration brings attributes back together with the name, so e4's timing has no per-entry stat leg at all. For the high-low assembly, we used SetEndOfFile to create a 4.5 GiB logical size (without writing a single byte) and had FindFirstFileW look it back up by single-name query: High gave 0x1, Low gave 0x20000000, assembled as 4831838208 bytes — the high 32 bits genuinely carry cargo.

Enumeration order is this chapter's first cross-check against L04, and we deliberately scrambled the names when building the tree:

```text
$ ./e1_trio.exe
== [1] 测试树按乱序创建,FindFirstFileW 枚举序 ==
  创建顺序:zeta.txt, alpha.txt, Mike.TXT, 中文文件.txt, Beta.dat, noext, longname.html, 子目录/
  枚举结果(pattern=\*,只报名字与短名):
    [1] .
    [2] ..
    [3] alpha.txt
    [4] Beta.dat
    [5] longname.html             短名=LONGNA~1.HTM
    [6] Mike.TXT
    [7] noext
    [8] zeta.txt
    [9] 中文文件.txt
    [10] 子目录
  共 10 项(含 . 与 ..)——NTFS 按大小写折叠后的字典序返回,与创建顺序无关
```

Compare creation with enumeration: at creation zeta took the head position, yet in the enumeration it ranked third from the end. Beta.dat came in ahead of longname.html, and Mike.TXT wedged itself between longname and noext. The case-folded order is alpha, beta, longname, mike, noext, zeta — all matched, with the Chinese name ranking after the Latin letters, and subdirectories entering the same lexicographic order as files, settled at the tail of this tree. The documentation's position on the matter has two layers. FindFirstFileW's Remarks puts it bluntly: `"This is because FindFirstFile does no sorting of the search results"` — the API does no sorting of its own. FindNextFileW's Remarks adds the file systems' habits: `"With the NTFS file system and CDFS file systems, the names are usually returned in alphabetical order"`, while FAT usually follows the order written to disk, and a closing sentence adds that none of these behaviors are within the guarantee. The mechanical explanation sits inside NTFS: a directory's contents live in a B+ tree ordered by name (the NTFS documentation calls it the $I30 index), the sort key folds names into a unified uppercase form using a $UpCase case-mapping table on the volume, and when enumeration walks the tree, what it emits is naturally the folded lexicographic order. NTFS naming is case-insensitive by default (the documentation's own words: `"Do not assume case sensitivity"`), which is self-consistent with exactly this ordering. The mirror's other face is L04's E1: ext4's large directories go through HTree with name-hash positioning, f00408 stole f00000's head spot, and the order bears neither the shadow of creation order nor the look of a lexicographic one. Both sides teach the same sentence: we cannot count on the API for order — code that depends on order does its own sorting.

Give the output's first two lines an extra glance as well: the first two entries a `*` pattern spits out are always `.` and `..`, both with attributes DIRECTORY and timestamps from the directory's own metadata. The dot entries have a few temperaments: non-`*` patterns (like `*.txt`) do not produce dot entries, which is easy enough to accept. The root directory is stranger still: e1_trio enumerated `C:\*` and the very first entry seen was `$Recycle.Bin`, with no trace of `.` or `..` anywhere. That also explains why querying the root directory's attributes goes through GetFileAttributesW — the documentation dedicates a move to the root directory precisely for this.

The world of filter patterns is wider than glob intuition, and we lay out three groups of edge cases:

```text
== [3] 过滤模式(节选:3d 单名查询一行略,[3a]-[3c] 的逐条行合并成了一行) ==
  [3a] *.txt(点项不该出现):
    [1] alpha.txt   [2] Mike.TXT   [3] zeta.txt   [4] 中文文件.txt
  [3b] *.(DOS 尾点模式,看看能匹配到谁):
    [1] .    [2] ..   [3] noext   [4] 子目录
  [3c] *.htm(短名匹配:longname.html 会不会被捎上?):
    [1] longname.html             短名=LONGNA~1.HTM
```

`*.` matches names with no extension, counting `.` and `..` and subdirectories among them — a legacy of DOS wildcards. The genuine surprise is [3c]: we searched for `*.htm` and longname.html came along for the ride, matched by its 8.3 short name. The **8.3 short name** refers to the old DOS-era naming scheme, a primary name of at most 8 characters plus a 3-character extension. For every long-named file, NTFS maintains exactly such a shadow name — it is what lives in `cAlternateFileName`, and the LONGNA~1.HTM in the output is the evidence. The documentation nods at this point too: `"The search includes the long and short file names"` — both long and short names enter the match. The urgent reminder sits on another page: the Short vs. Long Names section of Naming Files reads `"This 8.3 aliasing can be disabled for performance reasons either system-wide or for a specified volume"`. The aliasing can be switched off for a whole volume, so extension-filtering code changes behavior on a machine with 8.3 disabled, and the Note on the same page warns us not to assume short names exist on disk. e1_trio's ending also compared the failure value: FindFirstFileW returns INVALID_HANDLE_VALUE on failure, yet calling FindClose with it yields FALSE plus err=3 — the CloseHandle-on--1 silently returning TRUE that the Thinking Foundation's [RAII Paradigm](../../thinking/01-raii-paradigm.md) measured does not hold for it.

## Error Paths: 3, 2, and the Empty Directory That Reports No Error

The same INVALID_HANDLE_VALUE hides three different worlds of error codes beneath it, and we let e1_errors line them up in one pass:

```text
$ ./e1_errors.exe
== [1] 错误路径区分 ==
  不存在目录 \*           -> INVALID_HANDLE_VALUE, err=3
  不存在目录 \*.txt       -> INVALID_HANDLE_VALUE, err=3
  存在目录,模式无匹配 -> INVALID_HANDLE_VALUE, err=2
  空目录 \*                 -> 成功,吐了 2 项(最后一项之后 FindNextFileW err=18)
  空目录 \*.txt             -> INVALID_HANDLE_VALUE, err=2
  文件路径挂 \*           -> INVALID_HANDLE_VALUE, err=267
  尾部反斜杠(无模式)   -> INVALID_HANDLE_VALUE, err=2
  尾部双反斜杠+*         -> 成功,吐了 2 项(最后一项之后 FindNextFileW err=18)

== [2] 空目录的完整枚举过程(三件套逐项) ==
  FindFirstFileW 成功,第一项 = "."
  FindNextFileW  -> ".."
  FindNextFileW 止步,err=18(18=ERROR_NO_MORE_FILES,正常收尾,不是错误)
  句柄用尽后再 FindNextFileW -> 0, err=18(还是 18,不会翻页也不会崩)
```

Take the trichotomy one at a time. When the directory does not exist, whatever pattern we hang gets err=3 (`ERROR_PATH_NOT_FOUND`) — dead at the path layer. When the directory exists but the pattern matches no name, we get err=2 (`ERROR_FILE_NOT_FOUND`) — dead at the name layer. The empty-directory end is the most counterintuitive: hanging `\*` succeeds, spitting out the two entries `.` and `..`. The directory is plainly empty, yet the enumeration's result looks anything but. So emptiness cannot be judged by counting entries — we must skip the dot entries before counting, and counting non-dot entries with `\*` is the sturdiest way. The `\*.txt` route does not go through: dot entries do not match non-`*` patterns, so an empty directory gives 2 and a full directory gives 2 — the two cases cannot be told apart. 267's name is `ERROR_DIRECTORY`, and the name says the directory name is illegal: hang `\*` on a file's path, and this is Windows' answer. The trailing-backslash group has its provenance too, in the documentation's own words: `"An attempt to open a search with a trailing backslash always fails"` — measured, the shape is that a wildcard-free trailing backslash gets treated as a pattern to match, yielding 2, while a double backslash with a wildcard gets tolerated.

18 (`ERROR_NO_MORE_FILES`) must be shelved separately in our taxonomy: it belongs only to FindNextFileW's normal end, the loop's exit condition, and throwing it as an exception does it an injustice. Call FindNextFileW again after the handle is exhausted and 18 still comes back — no page turn, no crash. Compare L04's E2 (upper half), where the same ENOENT received four different treatments at the hands of exists, is_directory, status, and file_size: the identity of answer versus error travels with the question. Windows' story here is cut from the same cloth: 2 and 3 are real errors, 18 is the normal terminus, and the empty directory's success only counts once we skip the dot entries ourselves.

## Wide-Character Names: Nine Postures, Only One Fully Correct

`cFileName`'s type is `wchar_t`, its content stored as UTF-16, while our .out files and the terminal want UTF-8 bytes — one transcoding in between is unavoidable. The transcoding itself sounds simple. All the trouble lives in the CRT's output functions. e1_wide_out deliberately created two files, `中文文件.txt` and `emoji😀.txt`, the latter exercising a **surrogate pair**: in UTF-16, code points beyond the Basic Multilingual Plane (this emoji, U+1F600, for instance) must be encoded as two consecutive `wchar_t`, and that pair of code units is what the name surrogate pair refers to. Note the experiment's discipline: each posture occupies an independent process, so that wide and narrow output in one process cannot contaminate each other's byte streams. stdout was redirected into files, od preserved the hex, and return values were reported to stderr. The nine postures' results sit below. The whole table is bound to this machine's UCRT plus MinGW g++ 16.1.0 — the locale behaviors of rows c, d, and e follow the CRT anyway, so on a different runtime, re-run e1_wide_out:

| Mode | Posture | 中文文件.txt | emoji😀.txt | Reading |
| --- | --- | --- | --- | --- |
| base | environment facts only | n/a | n/a | GetACP=936, locale defaults to C |
| a | C locale + printf("%ls") | characters dropped, return reports success | characters dropped | the most dangerous one |
| b | C locale + wprintf(L"%ls") | replaced with ???? | replaced with ?? | a different face of failure than a |
| c | setlocale(LC_ALL,"") (GBK) | correct (GBK bytes) | dropped | GBK cannot hold U+1F600 |
| d | setlocale(LC_ALL,".UTF8") + printf | correct (UTF-8 bytes) | dropped | the surrogate pair does not get through |
| e | .UTF8 + wprintf | correct | dropped | same as above |
| f | manual WideCharToMultiByte(CP_UTF8) + %s | correct | correct (f0 9f 98 80) | the only fully correct one |
| orient | wprintf then printf, same process | both steps succeed | — | the classic legend does not reproduce, emoji untested |
| orient2 | printf then wprintf, same process | both steps succeed | — | the reverse direction does not reproduce either, emoji untested |

Pick three rows for a close look — the bytes are all preserved in od's output. a is the most dangerous route: under the C locale, printf's wide-to-narrow conversion for `%ls` simply drops the characters it cannot convert. The Chinese line came out entirely empty (`cn=[]`), the emoji line kept only its ASCII prefix, and the return value reported the full line's character count — on failure it does not even say hello. d is the most deceptive route: with the locale set to `.UTF8`, the Chinese line landed correctly as UTF-8 bytes, but swap in a surrogate pair and it drops just the same. UCRT-plus-MinGW's `%ls` conversion cannot get past this hurdle. So switching the locale to UTF-8 is not the same as fully correct — it cures the Chinese ailment but not the surrogate-pair one. f is the only fully correct route: `WideCharToMultiByte(CP_UTF8, ...)` explicitly converts to UTF-8 bytes, and `%s` then prints them as an ordinary narrow string. The emoji landed as the four bytes f0 9f 98 80, all present and accounted for. This chapter's to_utf8 walks exactly f's road — not because I play favorites, but because among the nine postures it really is the only one that gets everything right.

The two orient rows close the book on a widely circulated tale: in the old MSVCRT era, mixing wprintf and printf could mute each other's streams. Measured on UCRT with MinGW g++ 16.1.0, both directions — wide first and narrow first — printed successfully, the legendary muting never appeared, and we file the negative result as is. The engineering posture fits in one sentence: enumeration hands us `wchar_t` names, output walks f's route, and we cash in the cross-check point L04 left behind while we are at it. Over there `path::value_type` is char and `c_str()` feeds the C API directly. Here on Windows the value_type is wchar_t, and feeding narrow-character APIs requires one explicit conversion — f is that conversion.


## Recursion: Junctions Do Not Descend by Default, and What Stops the Loop

No library wraps recursion for us. We wrap one function around the trio, and the traversal we get is preorder DFS (Depth-First Search), isomorphic to L04's `recursive_directory_iterator`. e2_recursive built a five-level tree plus two junctions (a junction being a privilege-free species of directory link on NTFS — the family identity gets a proper introduction in the next section). Its trunk looks like this:

```cpp
// e2_recursive.cpp(节选):walk 的判型与下钻
do {
    if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) { ++st.dots; continue; }
    std::wstring p = dir + L"\\" + fd.cFileName;
    bool subdir    = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    bool reparse   = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    const char* tag = reparse ? (subdir ? " [junction]" : " [symlink]") : (subdir ? "/" : "");
    printf("%*s%s%s\n", depth * 2, "", to_utf8(fd.cFileName).c_str(), tag);
    if (subdir && !reparse) {
        ++st.dirs;
        if (!follow || depth < 40) { walk(p, depth + 1, follow, st, laps); }
    } else if (subdir && follow && depth < 40) {
        ++st.links;
        walk(p, depth + 1, follow, st, laps);          // follow:junction 也下钻
    } else {
        if (subdir) { ++st.links; } else { ++st.files; }
    }
} while (FindNextFileW(h.get(), &fd));
```

Type-judging reads the attribute bits the enumeration brings along, sparing us even one extra open. In the default mode, directories carrying `FILE_ATTRIBUTE_REPARSE_POINT` get one line reported and no descent — the tally came to 7 files, 6 real directories, and 2 link leaves, with the loop turning 0 laps. Directory links get their name called and pass on. The loop never even had a road in. This is the same posture as L04's default with `follow_directory_symlink` switched off.

Now we switch follow on. At the tree's root sits a jn_cycle whose junction target is the tree root itself, and every lap we follow it, the path grows one segment longer. Measured, it turned 22 laps, the tally coming to 187 files, 127 real directories, and 44 link leaves — and what stopped it was not our self-provided 40-level depth gate:

```text
== follow 模式(节选,中段 22 圈的重复树形删节) ==
                                                [打不开:路径 259 字符 err=3]
                                                [打不开:路径 262 字符 err=3]
                                            [打不开:路径 259 字符 err=3]
                                            [打不开:路径 259 字符 err=3]
                                            [打不开:路径 266 字符 err=3]
                                            [打不开:路径 264 字符 err=3]

统计:文件 187,真目录 127,链接叶子 44,点项 332
junction 环共转了 22 圈
```

The batch that could not be opened all carried err=3, and the path lengths stopped at 259 to 266 characters: every junction followed lengthens the path by a segment, and once it grows to the vicinity of MAX_PATH (the traditional 260-character ceiling), FindFirstFileW can no longer open the door, and the recursion comes to its natural end. The brake that truly stopped the recursion differs on each side, and so does the pedal stroke: in this Windows run, it was the path length hitting its ceiling that braked for us — our self-provided 40-level depth gate never got its turn. In the Linux run (L04's follow_directory_symlink), it was the kernel's hard 40-level limit on symbolic links during path resolution returning ELOOP, with keep.txt accessed 41 times. The Linux story has a sequel too: libstdc++ swallowed that ELOOP — ec stayed clean the whole way, and the iterator stopped without a word. Neither brake was one we designed on purpose, and the gate we provided ourselves never got used. Across implementations and cache policies, their reliability can change its mind. When you truly must follow directory links, the self-protective routes do agree: on Linux, canonical paired with `disable_recursion_pending()`. On Windows, resolve the true path with GetFinalPathNameByHandleW and check it against a table. Do not count on the strata underfoot to catch you.

The recipe for making a junction deserves a note here too: Win32 has no official CreateJunction API. The proper route is to write the reparse data yourself with `FSCTL_SET_REPARSE_POINT`. In e2 we took the shortcut — `cmd /c mklink /J` — and every junction in the experiment was made that way, with the commands and return codes recorded in the .out.

## The NTFS Family: Hard Links, Symbolic Links, Junctions, and Sparse Files

Before the family introduction, one underlying mechanism must be acknowledged. NTFS officially recognizes three kinds of file links: hard links, junctions, and symbolic links — the latter two both built on **reparse points**. A reparse point is a class of special record in NTFS: a small block of extra data hanging on a directory entry, containing a four-byte tag that the file system does not interpret itself. On encounter, it hands processing rights to the component named by the tag. The junction's tag is 0xA0000003 (named MOUNT_POINT), the symbolic link's 0xA000000C. Sparse files are not links at all, but they enjoy the same privilege-free treatment, so we lay them into the family matrix as well:

| Member | How created | Privilege | Points to | POSIX relative |
| --- | --- | --- | --- | --- |
| hard link | CreateHardLinkW | none needed | files on the same volume | ln's hard links |
| symbolic link | CreateSymbolicLinkW | requires privilege or Developer Mode | file or directory, may be relative | symlink |
| junction | mklink /J (no official API) | none needed | directories on local volumes | directory symbolic link |
| sparse file | FSCTL_SET_SPARSE | none needed | not a link, an occupancy strategy | ext4's born-sparse |

Start with the most honest one, the hard link — e3_family's first group walked it end to end:

```text
== [1] 硬链接:两个名字,一条 MFT 记录 ==
    original.txt     FileIndex=0x00150000000ab77f  nNumberOfLinks=2  VolSer=0xda3672fa
    hard.txt         FileIndex=0x00150000000ab77f  nNumberOfLinks=2  VolSer=0xda3672fa
    从 hard.txt 续写 "world",再从 original.txt 读:hello world
    DeleteFileW(original.txt) 后,hard.txt 读到 "hello world"
    hard.txt         FileIndex=0x00150000000ab77f  nNumberOfLinks=1  VolSer=0xda3672fa
```

The **MFT** (Master File Table) we need to recognize is NTFS's central registry: every file and directory on the volume occupies one record in it. What CreateHardLinkW does is hang one more name onto the same MFT record, so the two names read out an identical FileIndex, `nNumberOfLinks` rose to 2, what we appended through one side was readable from the other, and after removing one name the other carried on living happily, the count falling back to 1. This behavior is fully isomorphic to POSIX hard links, with nNumberOfLinks playing st_nlink's role. The restriction clause is written plainly in the documentation: `"Hard links can't reference directories, only files, and they can't reference files on different volumes"` — it recognizes files only, and crosses same volumes only. Name removal uses DeleteFileW, and Windows lays an entire additional layer of sharing-mode restrictions on deletion: with a handle open and no D in the share flags, deletion gets refused with 32, while POSIX's unlink always succeeds. That five-row matrix and the DeletePending measurements were covered in full in W01's dwShareMode section, with the data under the archive `01-win32-file-io-supplement/03-sharemode/`. Here we reference and do not redo.

From the symbolic link we could not obtain a successful sample — my machine cannot manufacture one, and the evidence in hand is two refusals:

```text
== [2] 符号链接:这台机器不给我造 ==
    无 flag            -> ret=0 err=1314(1314=ERROR_PRIVILEGE_NOT_HELD)
    ALLOW_UNPRIVILEGED -> ret=0 err=1314(此 flag 需开发者模式背书,本机未开)

(mklink 的四条命令与答复,GBK 转写自 e3_mklink_capture.txt,原文命令与答复各占一行,排版合并)
C:\> mklink sl_file.txt real.txt        你没有足够的权限执行此操作。
C:\> mklink /D sl_dir real_dir          你没有足够的权限执行此操作。
C:\> mklink /J jn_dir real_dir          为 jn_dir <<===>> real_dir 创建的联接
C:\> mklink /H hard.txt real.txt        为 hard.txt <<===>> real.txt 创建了硬链接
```

The refusal we ate for calling CreateSymbolicLinkW without flags was expected. Being refused again even with `SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE` is where the confusion easily creeps in: the flag's documentation entry states plainly, `"Developer Mode must first be enabled on the machine before this option will function"`. What it wants is not ordinary user authorization but the backing of Developer Mode — which is off on this machine. cmd's mklink /D returned the same sentence, so requiring privilege is real on both fronts, and the POSIX contrast makes it starker still: symlink is something an ordinary user creates on a whim. This fork is where the two sides' engineering experiences diverge the widest. Tag 0xA000000C and the symbolic link's attribute bits — with no link creatable on this machine, that is exactly the part missing from the measurements. Some day when Developer Mode is on, re-running e3 fills it in, and the archive's README says as much.

The junction is the family's commoner: mklink /J needs no privilege, the attributes read 0x410 (DIRECTORY plus REPARSE), and the documentation says it links directories on local volumes — a same-machine cross-volume case is also within its service range. Measured, we pointed C: at F:'s root, and enumerating through it showed F:'s contents. What most deserves a close look on it is the different fates of two kinds of query. The single-name query FindFirstFileW(jn_dir) and GetFileAttributesW both read the link itself — attr gives 0x410 as usual — and the documentation's basis sits right there: `"If the path points to a symbolic link, the WIN32_FIND_DATA buffer contains information about the symbolic link, not the target"`. But hang `jn_dir\*` and enumerate, and the very act of opening the jn directory crosses the link, spitting out the target's t1.txt and t2.txt. The dangling-junction measurement also corrected my starting expectation: the target directory was never created, yet GetFileAttributesW returned 0x410 successfully all the same, and the sentinel error code we had planted did not move by a hair — placing the fact that it never followed the target right in front of us. The expectation of following was thus denied by measurement, and the criterion is precisely that unmoved sentinel. The posture for reading the tag goes into the record as well: open the link body itself with `FILE_FLAG_OPEN_REPARSE_POINT`, and `FSCTL_GET_REPARSE_POINT` reads out the reparse data, tag and target path both in view — the target written in the `\??\C:\...` form, where `\??\` is the entry in the NT object namespace that points at drive letters. The official header for the REPARSE_DATA_BUFFER structure lives in the WDK's ntifs.h, absent from the Win32 SDK. WDK spells out to Windows Driver Kit, Microsoft's driver development kit, and e3 used a hand-copied one — happily its ABI is stable. Deleting a junction uses RemoveDirectoryW, and measured, it deletes only the link — the files inside the target are untouched.

The sparse file's switch is `FSCTL_SET_SPARSE`, likewise privilege-free. W01's e1b already turned it on in the hole-occupancy comparison, and here we add the family-matrix perspective. Two files, each with one byte written at offset 0 and at 1 GiB:

```text
== [4] 稀疏文件:1 GiB 的账面,两簇的身价 ==
    normal.bin     逻辑大小   1073741825 字节   实占   1073741825 字节   attr=ARCHIVE
    sparse.bin     逻辑大小   1073741825 字节   实占       131072 字节   attr=ARCHIVE|SPARSE
    normal.bin 的已分配区间(中间不是洞,是实打实的零):
    已分配区间 1 个: [0..1073741825)
    sparse.bin 的已分配区间(1 GiB 的洞):
    已分配区间 2 个: [0..65536) [1073741824..1073741825)
```

Take the two files' readings one at a time. The ordinary file filled the entire middle stretch with honest zeros, so its real occupancy equals its logical size. The sparse file occupies only two clusters, `FSCTL_QUERY_ALLOCATED_RANGES` listed the two written extents, and the 1 GiB in between is the hole. In the enumeration's attribute bits, SPARSE (0x200) is directly visible — FindFirstFileW recognizes its identity at a glance. ext4 walks the other extreme, born sparse, holes never occupying disk. NTFS requires an explicit switch — exactly what W01's e1b verified with a three-column table. Laid into the family matrix and looked at again, it still holds.

Finally we invite exFAT on stage as the negative control, moving the same family kit onto the D: drive: CreateHardLinkW and FSCTL_SET_SPARSE refused, both giving GetLastError's err=1 (`ERROR_INCORRECT_FUNCTION`). mklink /J hit the wall too — rc=1 is cmd's exit code, not a GetLastError reading, and the link body simply does not exist. The link family belongs to NTFS, not to "the file system" at large. The USB stick and SD card in your hand are most likely exFAT — do not take it for granted when porting code.

> One small find while tidying the experiment scene: for junctions the program's ending cleanup could not clear, the WSL side's way to delete is `rm` — `rmdir` instead reports Not a directory. That is because WSL's cross-system view of Windows drives goes through a 9P-protocol file system, which sees a junction as a symbolic link, and the link's body is a file, not a directory. Not one byte on the target side gets touched.

## LARGE_FETCH: The Price of a Hint Bit

`FindFirstFileExW` is the trio's sibling entrance, offering a few extra flags, and the documentation's wording for `FIND_FIRST_EX_LARGE_FETCH` is remarkably restrained: `"Uses a larger buffer for directory queries, which can increase performance of the find operation"` — a larger buffer for directory queries, with performance that "can" increase. The "can increase" phrasing leaves room: its identity is a hint bit, not a promise bit. We had e4_bench's setup build a directory of 10000 files and timed three legs, warm-cache discipline, one warm-up round, 3 rounds per leg with the median taken:

```text
$ ./e4_bench.exe
== 10000 文件目录遍历计时(暖缓存,预热后每腿 3 轮取中位)==
  目录:C:\Users\CharlieChen114514\AppData\Local\Temp\sysprog-direnum\e4lots
  A  FindFirstFileW                          条目 10002(含 . ..)  3 轮 1.73 / 1.67 / 1.79 ms  中位 1.67 ms
  B  FindFirstFileExW flags=0                条目 10002(含 . ..)  3 轮 1.66 / 1.81 / 1.94 ms  中位 1.81 ms
  C  FindFirstFileExW LARGE_FETCH            条目 10002(含 . ..)  3 轮 1.38 / 1.39 / 1.56 ms  中位 1.39 ms
```

Read the three lines one by one. B and A are the same order of magnitude, and the documentation backs this: when FindFirstFileExW gets `FindExInfoStandard`, `FindExSearchNameMatch`, and flags 0, the documentation states outright that it equals one call to FindFirstFile — the 1.67-versus-1.81 gap sits inside the noise. C is genuinely faster: across four independent re-runs, C ranked first every time, with a range of 1.35 to 1.68 ms against A and B's 1.66 to 1.96 ms — this setting runs steadily about 19% faster. A bigger buffer means the kernel hauls more directory blocks per trip and fewer round trips follow. The mechanism is plain. The magnitude is this machine's number — cold caches and other machines, please recompute. A hint bit's duty is to point out a direction worth trying. How much faster it actually goes must be measured back on your own machine.

Set the table beside L04's E4 (same scale, ext4): there, hand-rolled readdir ran 2.16 ms and directory_iterator 3.42 ms, with the file_size leg down at 16.22 ms. Here, A is 1.67 ms, with attributes and sizes already riding along with the enumeration — there is no per-entry stat leg, and the 16 ms class has no corresponding phenomenon on this side at all. One more number I am willing to record on its own: setup took about 5 seconds to create the 10000 files, while traversal took only 1.4 to 1.9 ms. Writing and reading differ by three orders of magnitude — NTFS metadata updates are genuinely expensive on the write path.

## Paths: The 260 Ceiling, the Manifest, and the Device Prefix

On the path layer, e5_paths swept all four groups of edge cases, and the first group is long paths. This machine's registry has `LongPathsEnabled` at 1, yet bare paths still get clipped at 260. The documentation's account of the policy is clear, and neither condition can be spared: `"A registry value must be set, and the application manifest must include the longPathAware element"` — the registry value must be set, the application manifest must carry the longPathAware element, and the documentation adds that the registry affects only opted-in applications. Our bare exe compiled by MSYS2 g++ has no manifest, and no policy this friendly reaches it:

```text
== [1] 长路径:260 的墙与 \\?\ 的梯子 ==
  本机注册表 HKLM\...\FileSystem\LongPathsEnabled=1,但那是给带清单的应用的;
  本 exe 无 longPathAware 清单,裸路径仍按 MAX_PATH=260 裁。8 层 × 40 字符下钻:
    第 5 层倒下:路径 274 字符,err=3(3=ERROR_PATH_NOT_FOUND)
    \\?\ 前缀把 8 层建完:总长 404 字符(含前缀 4 字符,去掉前缀 400 字符)
    最深层的第 8 层目录(404 字符):
    裸路径 GetFileAttributesW       -> attr=0xffffffff err=3
    \\?\ 前缀 GetFileAttributesW     -> attr=0x10 err=1234
    裸路径 FindFirstFileW \*        -> 失败 err=3
    \\?\ 前缀 FindFirstFileW \*      -> 成功(枚举到 "." 与 "..",证明通配符路径也能带前缀)
```

The err=1234 residue in the success line — do not go looking it up as an error code: 1234 is a probe value we planted with SetLastError before the call. A successful call does not touch it, and its sitting there untouched is exactly what proves the call is clean. This sentinel technique comes from the Thinking Foundation's RAII chapter. For the experiment we drilled down eight levels of 40 characters each: the bare path passed at level 4's 233 characters and fell over at level 5's 274. For the same deepest directory, among the three tasks of query, enumerate, and delete, the bare-path attempts all failed and the prefixed ones all passed — even the wildcard pattern enjoyed the ride. The `\\?\` mechanism, in the documentation's words, is to `"disable all string parsing and to send the string that follows it straight to the file system"` — the string that follows is shipped to the file system unparsed, the Win32 layer's normalization (including the 260 clip) is skipped wholesale, and the ceiling lifts to 32767 wide characters (the documentation notes it as approximate — the prefix can be expanded at run time). The price is stricter rules: forward slashes find no tolerance in device paths. The automatic `"/"`-to-`"\"` conversion belongs to ordinary paths alone, and a forward slash slipping in earns err=123, measured in e5. The treatment of `.` and `..` is a place where the two official pages disagree: the Maximum Path page says device paths cannot use them, while Naming Files says the opposite, listing `..` and `.` among the allowed. We did not measure to add weight — we only lay the disagreement out here as it stands. Relative paths remain forever under MAX_PATH's jurisdiction. The documentation adds a pointed reminder that `\\?\` cannot reach the root directory — querying the root is still GetFileAttributesW's job.

The forward-and-back slash group condenses into one sentence: `/` and `\` are equivalent at the Win32 layer in ordinary paths, the conversion rule written in the documentation (`"File I/O functions in the Windows API convert \"/\" to \"\\\" as part of converting the name to an NT-style name, except when using the \"\\\\?\\\" prefix"`), and e5 verified it with an all-forward-slash CreateFileW and a mixed FindFirstFileW. The trailing backslash's treatment, by contrast, splits by API, and we lay e5's five lines below:

```text
== [3] 尾部反斜杠 ==
    CreateDirectoryW "newdir\\"          -> 成功 err=1234(尾杠收下)
    GetFileAttributesW "dir\\"            -> attr=0x10 err=1234
    CreateFileW(开目录) "dir\\"          -> 成功 err=0
    FindFirstFileW "dir\\"(无通配符)     -> 失败 err=2(2=当模式匹配,啥也匹配不上)
    FindFirstFileW "dir\\\\*"(双杠+通配)   -> 成功 err=1234(冗余分隔符被容忍)
```

Three parties accept the trailing backslash, and FindFirstFileW is the only one that refuses, for the reason we already met in the error-path section: it takes the whole string as a pattern, a wildcard-free trailing backslash matches nothing, and what it gives is 2.

The fs::path cross-check is the mirror point L04's E5 left ready, and e5's [4] segment ran the same algebra on the Windows side: `value_type` is the 2-byte `wchar_t` (1-byte char on the Linux side), appending with a root name on the right shoves the entire left side off, a trailing separator gets absorbed while a doubled one survives, concatenating an empty string adds a separator, and `lexically_normal("./a/../b")` yields `b` — all of them the same as on the Linux side. Iterating `"a/b/"` likewise yields the trailing empty segment `[a][b][]`, so remember to filter out empties when processing segments — the code over there needs not one character changed. Only one addition is Windows-specific: `path("C:/x") == path("C:\\x")` holds, because forward slashes get normalized before comparison.

> While writing the experiment code I also stepped on a one-line comment: a `//` comment ending with `F:\` — the backslash spliced the next line into the comment, and the function declaration on that line simply vanished. The `-Wcomment` warning tells the truth. When a Windows path enters a C++ comment, mind the trailing backslash.

## How the Other Side Sees It

We close by setting the two sides on one table, and the mirror chapter's ledger is complete:

| Comparison point | Windows (this chapter) | Linux (L04) |
| --- | --- | --- |
| traversal entrance | FindFirstFileW trio, handle-style | opendir/readdir, fd-style |
| enumeration order | NTFS folded lexicographic ($I30's B+ tree) | ext4 hash order (HTree) |
| promise of order | documentation says no sorting, no guarantee | standard unspecified, libstdc++ does not sort |
| attributes per entry | attributes, three timestamps, 64-bit size all included | only d_type, the rest via added stat |
| dot entries | `*` yields . and .., root has none | readdir yields . and .., filter yourself |
| name charset | wchar_t (UTF-16), output needs transcoding | char, bytes as-is |
| link family | hard link, junction, symbolic link — three kinds | hard link plus symbolic link, unified |
| threshold to create links | symbolic link needs privilege or Developer Mode | ordinary user creates at will |
| loops when following directory links | stopped by MAX_PATH, no kernel backstop | kernel's 40-level ELOOP backstop |
| sparse files | explicit FSCTL_SET_SPARSE | born sparse |
| long paths | manifest plus registry, or device prefix to 32767 | PATH_MAX 4096, enough for daily use |

The family row deserves another two sentences. POSIX unifies directory links and file links under the symbolic-link concept, while NTFS splits them into two identities, junction and symbolic link: the former is privilege-free but recognizes only directories on local volumes, the latter is omnipotent but demands privilege. Hard links are isomorphic on both sides, the difference being only that Windows adds an entire layer of sharing-mode restrictions to deletion. The loop when following links brakes differently on the two sides, yet the library layers' attitudes agree: neither detects for you — your own visited set is the proper road. For the other side's complete story, please proceed to [std::filesystem: Directories and Metadata](../../linux/file-io/04-filesystem.md), where E1's hash order, E2's silent truncation, and E4's timing dissection mirror this chapter's sections one to one. The two sides also each have a chapter on the ground of file locks — the Windows side's [File Locks: LockFileEx](05-lockfileex.md), where we take up the story when we reach the territory of locks.

<ReferenceCard title="References">
  <ReferenceItem
    :id="1"
    title="FindFirstFileW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfilew"
  />
  <ReferenceItem
    :id="2"
    title="FindNextFileW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextfilew"
  />
  <ReferenceItem
    :id="3"
    title="FindFirstFileExW function (FIND_FIRST_EX_LARGE_FETCH)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw"
  />
  <ReferenceItem
    :id="4"
    title="FindClose function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findclose"
  />
  <ReferenceItem
    :id="5"
    title="Hard Links and Junctions"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/fileio/hard-links-and-junctions"
  />
  <ReferenceItem
    :id="6"
    title="CreateHardLinkW function"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createhardlinkw"
  />
  <ReferenceItem
    :id="7"
    title="CreateSymbolicLinkW function (SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createsymboliclinkw"
  />
  <ReferenceItem
    :id="8"
    title="Naming Files, Paths, and Namespaces"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file"
  />
  <ReferenceItem
    :id="9"
    title="Maximum Path Length Limitation (LongPathsEnabled and the longPathAware manifest)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation"
  />
  <ReferenceItem
    :id="10"
    title="Sparse Files (FSCTL_SET_SPARSE / FSCTL_QUERY_ALLOCATED_RANGES)"
    publisher="Microsoft Learn"
    url="https://learn.microsoft.com/en-us/windows/win32/fileio/sparse-files"
  />
</ReferenceCard>
