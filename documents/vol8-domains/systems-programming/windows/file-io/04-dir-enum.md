---
title: "目录枚举与 NTFS 家族"
description: "Windows 侧镜像 std::filesystem 的第四篇。FindFirstFileW/FindNextFileW/FindClose 三件套把名字、属性、三时间与 64 位大小一次带回(4.5 GiB 的逻辑大小实证 nFileSizeHigh 有货),NTFS 的枚举序实测是大小写折叠后的字典序而文档明说 does no sorting(ext4 散列序的对照面)、空目录挂 * 成功吐出 . 与 ..(错误码三分法:目录不存在 3、模式无匹配 2、正常收尾 18)、*.htm 靠 8.3 短名捎带 longname.html、宽字符名的九种输出姿势矩阵里唯一全对的是手动 WideCharToMultiByte(CP_UTF8)(C locale 的 %ls 静默丢字还报成功、.UTF8 locale 对代理对 emoji 仍丢)、junction 默认不下钻而 follow 模式的环转 22 圈后由 MAX_PATH 拦停(Linux 同场景是内核 40 层 ELOOP)、NTFS 家族矩阵(硬链接同 FileIndex、符号链接无特权造不出 1314、悬空 junction 的查询压根不跟目标、稀疏 1 GiB 实占 128 KiB、exFAT 负对照全拒)、FindFirstFileExW 的 LARGE_FETCH 稳定快约 19% 但提示位不是承诺位、LongPathsEnabled=1 也只放行带 longPathAware 清单的应用而设备路径前缀把上限抬到 32767"
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
---

# 目录枚举与 NTFS 家族

上一篇咱们陪着 `0xC0000005` 走完了异常的分发链,这一篇回到平静得多的调用上,聊的是目录。前三篇咱们手里握着的都是单个文件,开它、读它、写它、给它建映射都是它的戏。等手里的文件多了起来,问法就得换:一个目录里的名字,怎么挨个拿出来?拿出来的名字带不带属性?名字里既有中文又有 emoji 的时候,又怎么把它完整地写到终端上?Linux 侧的 [std::filesystem:目录与元数据](../../linux/file-io/04-filesystem.md) 用同一组问题把 ext4 考了一遍,本篇是它的 Windows 镜像篇。目录遍历、递归、路径这一类共用的机制,咱们按同一主题镜像着讲。NTFS 的链接家族与 ext4 的散列序这一类分岔,两侧都全量地讲。

编号与环境的口径,咱们照例交代在开头。本篇的实验按 e1 到 e5 编号,跟着存档的五个子目录走,全部收在了 `code/volumn_codes/vol8/systems-programming/windows/file-io/04-dir-enum/` 下面,其中 `01-findfirst/` 里住的是三份程序(e1_trio、e1_errors、e1_wide_out),其余的四个目录里各住一份,您认文件名就不会认错人,与 W01、W02、W03 的 e 系互不相干。机器是 Win11 26200 的中文系统(ANSI 代码页 936 即 GBK),编译器用的是 MSYS2 UCRT64 的 g++ 16.1.0,编译命令统一给的是 `-std=c++20 -Wall -Wextra`,只有 e4 的计时加了 `-O2`。数据全落在 C: 盘 `%TEMP%` 的真实 NTFS 上,跨卷的对照用了 F:(同样是 NTFS),exFAT 的负对照用 D:。有两条身份请您记好:非管理员,开发者模式在本机是关着的,它们到了 e3 会变成实打实的错误码 1314。输出的捕获日期是 2026-10-02。

工具还是沿用系列里的分工。`unique_handle` 与 `last_error_code` 的定义在 [OS 资源的 RAII 范式](../../thinking/01-raii-paradigm.md)和[错误处理范式](../../thinking/02-error-paradigm.md),`check_win32` 的定义在 [Win32 文件 I/O](01-win32-file-io.md),咱们在本篇只引用、不做重定义。新添的有两件,都收在了存档的 `common/win_dir.hpp` 里:

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

`unique_find` 管的是 FindFirstFileW 发回的查找句柄:失败值走的还是 INVALID_HANDLE_VALUE,但关它的函数却是 FindClose,而不是 CloseHandle,这一点文档里也特意写了。`to_utf8` 为什么长这样、为什么不直接 printf,这正是 e1_wide_out 整场实验要回答的问题,咱们到了宽字符的小节再见分晓。

## 三件套:名字带属性,一次回来

单层遍历的主角是三个函数:`FindFirstFileW` 拿的是带通配符的模式串,发回的是一个查找句柄,顺手把头一个条目写进了 `WIN32_FIND_DATAW`。`FindNextFileW` 拿的是后续的条目。`FindClose` 管的是收尾。咱们在 e1_trio 里的核心循环就这么几行:

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

`WIN32_FIND_DATAW` 的字段值得咱们挨个认一遍,因为它比 Linux 的 readdir 阔气得多。`cFileName` 存的是长名,`cAlternateFileName` 存的是 8.3 短名(后面细讲它的来历),`dwFileAttributes` 给的是属性位,创建、访问、修改三个时间各占了一个 FILETIME,大小则拆成了 `nFileSizeHigh` 与 `nFileSizeLow` 两个 32 位,拼起来的才是 64 位。Linux 那边的 readdir 吐的只是名字加一个 d_type,L04 为咱们数过这笔价:判型靠的是 d_type 缓存白捡,问大小的时候就得每条补一次 stat,万文件的目录也就从 3.42 ms 涨到了 16.22 ms。Windows 的枚举把属性连同名字一次带了回来,e4 的计时里也就没有补 stat 的腿。高低位的拼接,咱们拿 SetEndOfFile 造一个 4.5 GiB 的逻辑大小(一个字节都不写),再让 FindFirstFileW 单名查询回看了一遍:High 给的是 0x1,Low 给的是 0x20000000,拼出来的是 4831838208 字节,高 32 位里真的有货。

枚举的顺序,是本篇与 L04 的第一处对拍,咱们建树的时候故意把名字打乱:

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

咱们看创建与枚举的对照:创建的时候 zeta 打的头名,枚举回来它却排到了倒数第三,Beta.dat 排在了 longname.html 前面,Mike.TXT 则夹在了 longname 与 noext 中间。大小写折叠之后的序是 alpha、beta、longname、mike、noext、zeta,全都对上了,中文名排在了拉丁字母后面,子目录与文件进的是同一套字典序,排在这一棵树的末位。文档对这件事的口径分两层。FindFirstFileW 的 Remarks 写得干脆:`"This is because FindFirstFile does no sorting of the search results"`,API 自己是不排序的。FindNextFileW 的 Remarks 补了文件系统的习惯:`"With the NTFS file system and CDFS file systems, the names are usually returned in alphabetical order"`,FAT 则通常按写入磁盘的顺序,末尾又补了一句,说这些行为都不在保证的范围里。机制上的解释在 NTFS 的内部:目录的内容住在一棵按名排好的 B+ 树里(NTFS 文档叫它 $I30 索引),排序的键拿卷上一张 $UpCase 大写映射表把名字折叠成统一的大写形式,枚举顺着树走的时候,吐出来的自然就是折叠字典序。NTFS 的命名默认对大小写不敏感(文档的原话是 `"Do not assume case sensitivity"`),这与它的排序正好自洽。镜像的另一面是 L04 的 E1:ext4 的大目录走 HTree 按名字散列定位,f00408 抢了 f00000 的头名,序里既没有创建序的影子,也没有字典序的样子。两边给出的教训是同一句,顺序咱们不能指望 API,依赖顺序的代码自己排。

输出的头两行也请您多看一眼:`*` 模式吐的头两项永远是 `.` 与 `..`,attributes 给的都是 DIRECTORY,时间是目录自己的元数据。点项的脾气有几个:非 `*` 的模式(比如 `*.txt`)不吐点项,这一点是好理解的。根目录的脾气更怪:e1_trio 拿 `C:\*` 枚举了一遍,看到的头一项就是 `$Recycle.Bin`,`.` 与 `..` 的影子都见不着。这也解释了为什么查根目录的属性得走 GetFileAttributesW,文档也专门为根目录支了这一招。

过滤模式的世界比 glob 的直觉宽,咱们把三组边角摆开:

```text
== [3] 过滤模式(节选:3d 单名查询一行略,[3a]-[3c] 的逐条行合并成了一行) ==
  [3a] *.txt(点项不该出现):
    [1] alpha.txt   [2] Mike.TXT   [3] zeta.txt   [4] 中文文件.txt
  [3b] *.(DOS 尾点模式,看看能匹配到谁):
    [1] .    [2] ..   [3] noext   [4] 子目录
  [3c] *.htm(短名匹配:longname.html 会不会被捎上?):
    [1] longname.html             短名=LONGNA~1.HTM
```

`*.` 匹配的是无扩展名的名字,连 `.`、`..`、子目录都算在了里头,这是 DOS 通配符的遗产。真正意外的是 [3c]:咱们找 `*.htm`,longname.html 被捎带了,替它匹配的是 8.3 短名。**8.3 短名**说的是 DOS 时代的老命名法,主名最多 8 字符加 3 字符的扩展名,NTFS 给长名文件配的就是一个这样的影子名,存在 `cAlternateFileName` 里的就是它,输出里的 LONGNA~1.HTM 就是证据。这一点文档也点了头:`"The search includes the long and short file names"`,长名短名都进了匹配的范围。要紧的提醒在另一页:Naming Files 的 Short vs. Long Names 一节写着 `"This 8.3 aliasing can be disabled for performance reasons either system-wide or for a specified volume"`,别名是可以整卷关掉的,所以按扩展名过滤的代码,换一台关了 8.3 的机器,行为就变了,同页的 Note 也提醒了,叫咱们别假设盘上一定有短名。e1_trio 的收尾还对照了失败值:FindFirstFileW 失败返回的又是 INVALID_HANDLE_VALUE,而拿它去调 FindClose 的时候,给的是 FALSE 加 err=3,思维基石 [RAII 范式](../../thinking/01-raii-paradigm.md) 里实测过的 CloseHandle 对 -1 静默回 TRUE,在它身上是不成立的。

## 错误路径:3、2 与不报错的空目录

同样的 INVALID_HANDLE_VALUE,底下的错误码却分了三个世界,咱们让 e1_errors 把它们一次排开:

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

三分法咱们一个一个看。目录不存在的时候,挂什么模式都是 err=3(`ERROR_PATH_NOT_FOUND`),死在了路径层。目录在、模式没匹配到任何名字的场合,给的是 err=2(`ERROR_FILE_NOT_FOUND`),死在了名字层。空目录的这一头最反直觉:挂 `\*` 是成功的,吐出的是 `.` 与 `..` 两项,目录明明是空的,枚举的结果看起来却不空。所以判空不能数条目,咱们得跳过点项再数,用 `\*` 数非点项是最稳的。挂 `\*.txt` 的路是走不通的:点项不匹配非 `*` 的模式,空目录给的是 2,满目录给的也是 2,两种情况是分不开的。267 的名字是 `ERROR_DIRECTORY`,名字说的是目录名非法:拿一个文件的路径挂上 `\*`,Windows 给的回答就是它。尾部反斜杠的那一组也有它的出处,文档的原话是 `"An attempt to open a search with a trailing backslash always fails"`,实测的形态是无通配符的尾杠被当成了模式去匹配,给了 2,双杠带通配符的则被容忍了。

18(`ERROR_NO_MORE_FILES`)在咱们的分类里得单独放:它只属于 FindNextFileW 的正常收尾,是循环的退出条件,把它当异常抛出去就冤枉它了。句柄用尽了之后再调 FindNextFileW,回来的还是 18、不翻页、也不崩。对照 L04 的 E2(上),那边同一个 ENOENT 在 exists、is_directory、status、file_size 四个函数的手里,得到了四种对待,答案与错误的身份是跟着问题走的。Windows 这边的故事是同款的:2 与 3 是真的错误,18 是正常的终点,而空目录的成功,得咱们自己跳过点项才算数。

## 宽字符的名字,九种姿势只有一种全对

`cFileName` 的类型是 `wchar_t`,内容是按 UTF-16 存的,而咱们的 .out 与终端要的是 UTF-8 字节,中间的一次转码是免不了的。转码的事听起来简单,麻烦全在 CRT 的输出函数里。e1_wide_out 专门建了 `中文文件.txt` 与 `emoji😀.txt` 两个文件,后者用到了**代理对**:UTF-16 里超出基本多文种平面的码位(比如这个 emoji,U+1F600)得用两个连续的 `wchar_t` 编码,这一对码元的名字就叫代理对。实验的口径请您留意:每种姿势占一个独立的进程,避免同进程的宽窄输出互相污染字节流,stdout 重定向进了文件,od 把十六进制留了下来,返回值则报给了 stderr。九种模式的结果摆在下面,整张表绑定的是本机的 UCRT 加 MinGW g++ 16.1.0,c 与 d、e 的 locale 行为本来就随 CRT 走,换了运行库的话,请您拿 e1_wide_out 复跑一遍:

| 模式 | 姿势 | 中文文件.txt | emoji😀.txt | 判读 |
| --- | --- | --- | --- | --- |
| base | 只报环境事实 | 不适用 | 不适用 | GetACP=936,locale 默认是 C |
| a | C locale + printf("%ls") | 丢字,返回值报成功 | 丢字 | 最危险的一种 |
| b | C locale + wprintf(L"%ls") | 替换成 ???? | 替换成 ?? | 与 a 不同的失败长相 |
| c | setlocale(LC_ALL,"")(GBK) | 正确(GBK 字节) | 丢 | GBK 装不下 U+1F600 |
| d | setlocale(LC_ALL,".UTF8") + printf | 正确(UTF-8 字节) | 丢 | 代理对过不去 |
| e | .UTF8 + wprintf | 正确 | 丢 | 同上 |
| f | 手动 WideCharToMultiByte(CP_UTF8) + %s | 正确 | 正确(f0 9f 98 80) | 唯一全对 |
| orient | 同进程 wprintf 后 printf | 两步都成功 | — | 经典传说不复现,emoji 未测 |
| orient2 | 同进程 printf 后 wprintf | 两步都成功 | — | 反方向也不复现,emoji 未测 |

咱们挑三行细看,字节都在 od 的输出里留着。a 是最危险的一路:C locale 下 printf 对 `%ls` 做宽转窄,转不动的字符直接丢了,中文行整段都是空的(`cn=[]`),emoji 行留下了 ASCII 的前缀,而返回值报的是整行的字符数,失败的时候连个招呼都不打。d 是最有欺骗性的一路:locale 设成了 `.UTF8`,中文这行倒是正确地落成了 UTF-8 字节,换成代理对就照样丢了,UCRT 加 MinGW 的 `%ls` 转换就是过不了这一关。所以 locale 改成 UTF-8 不等于全对,它治了中文,治不了代理对的毛病。f 是唯一全对的一路:`WideCharToMultiByte(CP_UTF8, ...)` 显式地转成 UTF-8 字节,再拿 `%s` 当普通的窄串打出去,emoji 落成了 f0 9f 98 80 四个字节,全都到齐了。本篇的 to_utf8 走的就是 f 的路子,倒不是笔者偏心谁,九种姿势里它确实是唯一全对的。

orient 两行是给一个流传很广的说法收尾:老 MSVCRT 时代,wprintf 与 printf 混用会互相弄哑对方的流。UCRT 加 MinGW g++ 16.1.0 的实测里,宽在前与窄在前的两个方向都打印成功了,传说中的哑局没有出现,负结果咱们如实入册。工程上的姿势一句话就够:枚举拿到 `wchar_t` 的名字,输出走的就是 f 路线,顺手把 L04 留的对拍点也兑现了。那边的 `path::value_type` 是 char,`c_str()` 喂 C API 是直通的,Windows 这边的 value_type 是 wchar_t,喂窄字符的 API 必须显式转一次,f 就是转的法子。

## 递归:junction 默认不下钻,环靠什么停

递归没有库替咱们包,咱们在三件套外头套一层函数,得到的遍历就是 DFS(Depth-First Search)前序,与 L04 的 `recursive_directory_iterator` 同构。e2_recursive 建了五层的树外加两个 junction(junction 是 NTFS 上免特权的一类目录链接,家族身份咱们到下一节细认),它的主干是这么一段:

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

判型看的是枚举自带的属性位,咱们连一次额外的打开都省了。默认的模式下,带 `FILE_ATTRIBUTE_REPARSE_POINT` 的目录只报一行、不下钻,统计给的是文件 7、真目录 6、链接叶子 2,环转了 0 圈。目录链接报个名就过去了,环连进来的路都没有,这与 L04 关着 `follow_directory_symlink` 的默认是同一副姿态。

咱们把 follow 打开。树根上摆了个 jn_cycle,junction 的目标写着树根自己,咱们跟着它每转一圈,路径就长出了一截。实测它转了 22 圈,统计给的是文件 187、真目录 127、链接叶子 44,拦住它的却不是咱们自备的 40 层深度闸:

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

打不开的那批,err 给的全是 3,路径长度停在了 259 到 266 字符:每跟一圈 junction,路径就长了一截,长到 MAX_PATH(260 字符的传统上限)附近,FindFirstFileW 就开不了门了,递归也就自然到了头。真正把递归拦下来的刹车,两边各有各的一脚,踩法还不同:Windows 的这一场,是路径长度到了上限替咱们踩的,咱们自备的 40 层深度闸压根没轮上。Linux 那一场(L04 的 follow_directory_symlink),是内核解析路径时符号链接最多 40 层的硬上限回了 ELOOP,keep.txt 被访问了 41 次。Linux 的故事还有一段下文:libstdc++ 把那声 ELOOP 压住了,ec 全程都是干净的,迭代器一声不吭地停了。两脚刹车没有一脚是咱们主动设计的,自备的那道闸又没轮上,跨实现、跨缓存策略的时候,它们的可靠性都是可能变卦的。真要跟随目录链接的时候,自保的路子倒是一致的:Linux 那边的做法是 canonical 配 `disable_recursion_pending()`,Windows 这边的做法是拿 GetFinalPathNameByHandleW 解出真身再查表,别指望脚下的地层替您兜底。

junction 的造法也在这里交代一下:Win32 没有官方的 CreateJunction API,正经的路子是拿 `FSCTL_SET_REPARSE_POINT` 自己写 reparse 数据,e2 里咱们图省事,走的是 `cmd /c mklink /J`,实验里每一条 junction 都是这么造的,命令与返回码都记进了 .out。

## NTFS 家族:硬链接、符号链接、junction 与稀疏

咱们讲家族之前,得把一个底层的机制认下来。NTFS 官方承认的文件链接有三种:硬链接、junction、符号链接,后两种都架在了**重解析点(reparse point)**上。重解析点说的是 NTFS 里的一类特殊记录:目录项上挂着的一小块附加数据,里头有一个四字节的 tag,文件系统是不亲自解释的,碰到就把处理权按 tag 转给对应的组件,junction 的 tag 是 0xA0000003(名字叫 MOUNT_POINT),符号链接的 tag 是 0xA000000C。稀疏文件是算不上链接的,特权待遇却是同款的,咱们一并摆进家族矩阵:

| 成员 | 造法 | 特权 | 指向 | POSIX 的亲戚 |
| --- | --- | --- | --- | --- |
| 硬链接 | CreateHardLinkW | 免 | 同一个卷的文件 | ln 的硬链接 |
| 符号链接 | CreateSymbolicLinkW | 要特权或开发者模式 | 文件或目录,可相对 | symlink |
| junction | mklink /J(没有官方 API) | 免 | 本地卷的目录 | 目录符号链接 |
| 稀疏文件 | FSCTL_SET_SPARSE | 免 | 不是链接,是占盘策略 | ext4 的天生稀疏 |

咱们从最老实的硬链接开始,e3_family 的第一组把它从头到尾走了一遍:

```text
== [1] 硬链接:两个名字,一条 MFT 记录 ==
    original.txt     FileIndex=0x00150000000ab77f  nNumberOfLinks=2  VolSer=0xda3672fa
    hard.txt         FileIndex=0x00150000000ab77f  nNumberOfLinks=2  VolSer=0xda3672fa
    从 hard.txt 续写 "world",再从 original.txt 读:hello world
    DeleteFileW(original.txt) 后,hard.txt 读到 "hello world"
    hard.txt         FileIndex=0x00150000000ab77f  nNumberOfLinks=1  VolSer=0xda3672fa
```

咱们要认的 **MFT**(Master File Table,主文件表)是 NTFS 的中央登记表,卷上的每个文件、目录各占一条记录。CreateHardLinkW 干的事情,是往同一条 MFT 记录上再挂了一个名字,所以两个名字读出的 FileIndex 一模一样,`nNumberOfLinks` 涨到了 2,咱们从一边续写,另一边就读得到了,摘掉了一个名字之后,另一个还活得好好的,计数回落到了 1。这套行为与 POSIX 的硬链接完全同构,st_nlink 的角色由 nNumberOfLinks 扮演。限制的条款在文档里写得直白:`"Hard links can't reference directories, only files, and they can't reference files on different volumes"`,它能认的只有文件,能跨的只有同一个卷。摘名用的是 DeleteFileW,而 Windows 对删除还设有一整层分享模式的限制:句柄开着、share 里没有 D 的时候,删除是会被 32 拒掉的,POSIX 的 unlink 则永远成功。那套五行矩阵与 DeletePending 的实测,W01 的 dwShareMode 一节讲全了,数据都在存档 `01-win32-file-io-supplement/03-sharemode/` 的下面,本篇的做法是引用、不重做。

咱们在符号链接身上拿不到成功的样本,笔者的机器上造不出来,拿到手的证据是两声拒绝:

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

咱们不带 flag 直接调 CreateSymbolicLinkW,吃到的拒绝在意料之中。带上 `SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE` 也被拒了,才是容易迷糊的地方:文档在 flag 的条目里写明,`"Developer Mode must first be enabled on the machine before this option will function"`,它要的不是普通的用户授权,背后要的是开发者模式的背书,而本机的开发者模式关着。cmd 侧的 mklink /D 给出了同一句拒绝,要特权这件事在两边都是实打实的,拿 POSIX 一对照就更明显了:symlink 是普通用户随手就能造的东西,这个分岔是两侧工程体验差得最远的地方。tag 0xA000000C 与符号链接的属性位,本机是造不出链接的,实测里缺的就是这部分。等哪天开发者模式开了,拿 e3 重跑一遍也就补上了,存档的 README 也是这么交代的。

junction 是家族里的平民:mklink /J 是免特权的,属性给的是 0x410(DIRECTORY 加 REPARSE),文档说它链接的是本地卷上的目录,同机跨卷的情形也在它的服务范围里,实测里咱们拿 C: 指到 F:\ 的根,枚举穿过去看到的就是 F: 盘的内容。它身上最值得咱们细看的,是两种查询的不同下场。单名查询 FindFirstFileW(jn_dir) 与 GetFileAttributesW 读的都是链接自身,attr 照常给的是 0x410,文档的依据也摆在那里:`"If the path points to a symbolic link, the WIN32_FIND_DATA buffer contains information about the symbolic link, not the target"`。而挂 `jn_dir\*` 枚举,打开 jn 这个目录的动作本身就穿过了链接,吐出来的是目标里的 t1.txt 与 t2.txt。悬空 junction 的实测还纠正了笔者起手的预期:目标的目录根本没建过,GetFileAttributesW 照样成功地返回了 0x410,咱们设下的哨兵错误码纹丝未动,把它压根没跟目标的事实摆在了咱们面前。跟随的预期就这样被实测否掉了,判据就是那个没动的哨兵。读 tag 的姿势也顺手入册:拿 `FILE_FLAG_OPEN_REPARSE_POINT` 打开链接本体,`FSCTL_GET_REPARSE_POINT` 读出的就是重解析数据,里面的 tag 与目标路径全都能看到,目标写的是 `\??\C:\...` 的形式,`\??\` 是 NT 对象命名空间里指向盘符的入口。结构体 REPARSE_DATA_BUFFER 的官方头,住在 WDK 的 ntifs.h 里,Win32 SDK 里是没有的。WDK 的全称是 Windows Driver Kit,也就是微软的驱动开发包,e3 用的是手抄的一份,好在它的 ABI 是稳定的。删 junction 用的是 RemoveDirectoryW,实测删的只是链接,目标里的文件毫发无损。

稀疏文件的开关是 `FSCTL_SET_SPARSE`,同样是免特权的,W01 的 e1b 在洞的占盘差异里已经开过头,咱们在这里补上家族矩阵的视角。同样在偏移 0 与 1 GiB 处各写一个字节的两份文件:

```text
== [4] 稀疏文件:1 GiB 的账面,两簇的身价 ==
    normal.bin     逻辑大小   1073741825 字节   实占   1073741825 字节   attr=ARCHIVE
    sparse.bin     逻辑大小   1073741825 字节   实占       131072 字节   attr=ARCHIVE|SPARSE
    normal.bin 的已分配区间(中间不是洞,是实打实的零):
    已分配区间 1 个: [0..1073741825)
    sparse.bin 的已分配区间(1 GiB 的洞):
    已分配区间 2 个: [0..65536) [1073741824..1073741825)
```

两份文件的读数,咱们挨个看。普通文件把中间整段都填上了实打实的零,它的实占等于逻辑大小,稀疏文件占的只有两簇,`FSCTL_QUERY_ALLOCATED_RANGES` 把两个写入区段列了出来,中间的 1 GiB 就是洞。枚举的属性位里 SPARSE(0x200)直接可见,FindFirstFileW 一眼就认得出它的身份。ext4 走的另一个极端是天生稀疏,洞是从来不占盘的,NTFS 要显式地开开关,这正是 W01 的 e1b 用三列表验过的事情,咱们把它放进家族矩阵里再看一遍,它依然是成立的。

最后咱们请 exFAT 出场当负对照,把同一套家族原样搬到 D: 盘上:CreateHardLinkW 与 FSCTL_SET_SPARSE 拒了,给的都是 GetLastError 的 err=1(`ERROR_INCORRECT_FUNCTION`)。碰壁的还有 mklink /J,rc=1 是 cmd 的退出码,不是 GetLastError 的读数,链接的本体压根不存在。链接家族是 NTFS 的,不是文件系统的,您手里的 U 盘与 SD 卡多半是 exFAT,咱们搬代码的时候别想当然。

> 咱们收拾实验现场的时候有个小发现:程序末尾清理不净的 junction,WSL 那侧的删法是 `rm`,`rmdir` 报的反而是 Not a directory,因为 WSL 跨系统看 Windows 盘走的是 9P 协议的文件系统,它把 junction 看成了符号链接,链接的本体是文件、不是目录。目标侧的内容是一个字节都不会被碰到的。

## LARGE_FETCH:一档提示位的价格

`FindFirstFileExW` 是三件套的同级入口,多给了几个 flags,其中 `FIND_FIRST_EX_LARGE_FETCH` 的文档措辞相当克制:`"Uses a larger buffer for directory queries, which can increase performance of the find operation"`,说的是用更大的缓冲做目录查询,性能是可以提升的。can increase 的说法留了余地,它的身份是提示位,而不是承诺位。咱们拿 e4_bench 的 setup 建好 10000 个文件的目录、给三条腿计时,计时的口径是暖缓存,预热了一轮,每腿跑 3 轮取的中位:

```text
$ ./e4_bench.exe
== 10000 文件目录遍历计时(暖缓存,预热后每腿 3 轮取中位)==
  目录:C:\Users\CharlieChen114514\AppData\Local\Temp\sysprog-direnum\e4lots
  A  FindFirstFileW                          条目 10002(含 . ..)  3 轮 1.73 / 1.67 / 1.79 ms  中位 1.67 ms
  B  FindFirstFileExW flags=0                条目 10002(含 . ..)  3 轮 1.66 / 1.81 / 1.94 ms  中位 1.81 ms
  C  FindFirstFileExW LARGE_FETCH            条目 10002(含 . ..)  3 轮 1.38 / 1.39 / 1.56 ms  中位 1.39 ms
```

三行咱们挨个判读。B 与 A 是同量级的,这里有文档的背书:FindFirstFileExW 给 `FindExInfoStandard`、`FindExSearchNameMatch`、flags 给 0 的时候,文档里明说了它等价于 FindFirstFile 的一次调用,1.67 对 1.81 的差在噪声里。C 是真的快:独立复跑过四轮,C 全部排在了最前,成绩的区间是 1.35 到 1.68 ms,A 与 B 落在了 1.66 到 1.96 ms,本档稳定地快出了约 19%。缓冲大了,内核一趟搬的目录块就多,往返就少了,机制是朴素的。幅度属于本机的数据,冷缓存与别的机器请您另算,提示位的本分是给咱们一个值得试的方向,能快多少的幅度,得回到各自的机器上再量。

横向对表 L04 的 E4(同规模的 ext4),那边手搓 readdir 跑了 2.16 ms,directory_iterator 跑了 3.42 ms,再往下的 file_size 腿是 16.22 ms。Windows 这边的 A 是 1.67 ms,而且属性与大小全都随枚举自带了,没有每条补一次 stat 的腿,16 ms 那一档在这边压根没有对应的现象。还有个数字笔者愿意单独记一笔:setup 建 10000 个文件花了约 5 秒,遍历却只花了 1.4 到 1.9 ms,写与读差了三个数量级,NTFS 元数据的更新,在写入的路上是真贵的。

## 路径:260 的上限、清单与设备前缀

路径的这一层,咱们让 e5_paths 把四组边角全扫了一遍,头一组讲的就是长路径。本机的注册表里 `LongPathsEnabled` 明明是 1,裸路径却照样被 260 裁掉了。文档对这个政策的讲述很清楚,两个条件是一个都不能少的:`"A registry value must be set, and the application manifest must include the longPathAware element"`,注册表的值要设上,应用的清单里还得有 longPathAware 元素,文档又补了一句,注册表只影响改造过的应用。咱们 MSYS2 g++ 编出来的裸 exe 没有清单,再友好的政策也轮不到它:

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

成功行里残留的 err=1234,请您别拿去当错误码查:1234 是咱们在调用前用 SetLastError 设下的探针值,成功的调用不动它,它留在原地正说明调用是干净的,这套哨兵手法来自思维基石的 RAII 篇。咱们拿八层、每层 40 字符的下钻做实验:裸路径在第 4 层的 233 字符还过得去,第 5 层 274 字符就倒了。同一个最深层的目录,查询、枚举、删除的三件事里,裸路径的尝试全部失败,带前缀的全通,连通配符的模式都享受到了。`\\?\` 的机制,文档的原话是 `"disable all string parsing and to send the string that follows it straight to the file system"`,把后面的字符串不经解析直送文件系统,Win32 层的归一化(包括 260 的裁剪)整层跳过,上限抬到了 32767 个宽字符(文档注明的是约数,前缀在运行期是可能被展开的)。代价是规则变严了:正斜杠在设备路径里是容不下的,`"/"` 到 `"\"` 的自动转换只归普通路径享有,正斜杠混进去的下场就是 err=123,e5 里实测过了。`.` 与 `..` 的待遇,两份官方页的说法是打架的:Maximum Path 那页的说法是设备路径用不了它们,Naming Files 那页的说法却相反,允许的名单里有 `..` 与 `.`,咱们没实测点分量,只把分歧如实地摆在这儿。相对路径则永远在 MAX_PATH 的管辖里。文档还特意提醒了一句,`\\?\` 是到不了根目录的,查根目录还是 GetFileAttributesW 的活。

正反斜杠的这一组,收拢成一句:普通路径里的 `/` 与 `\` 在 Win32 层是等效的,文档里写过转化的规则(`"File I/O functions in the Windows API convert \"/\" to \"\\\" as part of converting the name to an NT-style name, except when using the \"\\\\?\\\" prefix"`),e5 拿全正斜杠的 CreateFileW 与混用的 FindFirstFileW,都验过了。尾部反斜杠的待遇则要按 API 分家,咱们把 e5 的五行输出摆在下面:

```text
== [3] 尾部反斜杠 ==
    CreateDirectoryW "newdir\\"          -> 成功 err=1234(尾杠收下)
    GetFileAttributesW "dir\\"            -> attr=0x10 err=1234
    CreateFileW(开目录) "dir\\"          -> 成功 err=0
    FindFirstFileW "dir\\"(无通配符)     -> 失败 err=2(2=当模式匹配,啥也匹配不上)
    FindFirstFileW "dir\\\\*"(双杠+通配)   -> 成功 err=1234(冗余分隔符被容忍)
```

收下尾杠的有三家,FindFirstFileW 是唯一不收的一家,理由咱们在错误路径一节已经见过:整个串被它当成了模式,无通配符的尾杠啥也匹配不上,给的就是 2。

fs::path 的对拍是 L04 的 E5 留好的镜像点,咱们让 e5 的 [4] 段把同一组代数在 Windows 侧跑了一遍:`value_type` 是 2 字节的 `wchar_t`(Linux 侧 1 字节 char),右侧带着根名的拼接会把整个左侧顶掉,尾杠是会被吸收的,双杠则残留了下来,拼空串的时候会添上分隔符,`lexically_normal("./a/../b")` 得到的是 `b`,全部与 Linux 侧是同款的。`"a/b/"` 迭代同样会产出尾部的空段 `[a][b][]`,处理段的时候记得滤掉空的,这边的代码一个字都不用改。多出来的只有一条是 Windows 特有的:`path("C:/x") == path("C:\\x")` 是成立的,比较之前正斜杠被归一化了。

> 写实验代码的时候笔者还踩了一行注释:行尾写着 `F:\` 的 `//` 注释,反斜杠把下一行接进了注释,那一行上的函数声明就这么没了,`-Wcomment` 的警告说的是真事。Windows 的路径进 C++ 注释,末尾的反斜杠得留意。

## 另一侧怎么看

咱们把两侧摆到一张表上收尾,镜像篇的家底就齐了:

| 对照点 | Windows(本篇) | Linux(L04) |
| --- | --- | --- |
| 遍历入口 | FindFirstFileW 三件套,句柄式 | opendir/readdir,fd 式 |
| 枚举序 | NTFS 折叠字典序($I30 的 B+ 树) | ext4 散列序(HTree) |
| 顺序的承诺 | 文档明说不排序、不保证 | 标准未指定,libstdc++ 不排 |
| 条目自带的属性 | 属性、三时间、64 位大小全带 | 只带 d_type,其余补 stat |
| 点项 | `*` 吐 . 与 ..,根目录没有 | readdir 吐 . 与 ..,自己滤 |
| 名字的字符集 | wchar_t(UTF-16),输出得转码 | char,字节原样 |
| 链接家族 | 硬链接、junction、符号链接三种 | 硬链接加符号链接,一统 |
| 造链接的门槛 | 符号链接要特权或开发者模式 | 普通用户随手造 |
| 跟随目录链接的环 | MAX_PATH 拦停,内核不兜底 | 内核 40 层 ELOOP 兜底 |
| 稀疏文件 | 显式 FSCTL_SET_SPARSE | 天生稀疏 |
| 长路径 | 清单加注册表,或设备前缀到 32767 | PATH_MAX 4096,日常够用 |

家族那一栏值得咱们再说两句。POSIX 把目录链接与文件链接统一在符号链接的概念下,NTFS 把它拆成了 junction 与符号链接两种身份,前者是免特权的,但只认本地卷的目录,后者是全能的,但要的是特权。硬链接的两边是同构的,差别只在 Windows 给删除加了一整层分享模式的限制。跟随链接时的环,两边连刹车的方式都不同,而两边库层的态度倒是一致的:都不替您检测,自备的 visited 集合才是正路。另一侧的完整故事,请您移步 [std::filesystem:目录与元数据](../../linux/file-io/04-filesystem.md),那边 E1 的散列序、E2 的静默截断与 E4 的计时解剖,和本篇的对应小节一一互为镜像。两侧在文件锁的阵地上也各有一篇,Windows 侧的 [文件锁:LockFileEx](05-lockfileex.md),咱们到了锁的地界再开讲。

<ReferenceCard title="参考资源">
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
    title="Maximum Path Length Limitation (LongPathsEnabled 与 longPathAware 清单)"
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
