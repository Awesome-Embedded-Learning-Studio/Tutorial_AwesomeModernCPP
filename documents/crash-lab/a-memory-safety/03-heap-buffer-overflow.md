---
title: "堆缓冲区溢出:多写一个没事,多写两个必死"
description: "new int[5] 的数组里写进 1000 个值,Linux 下 exit 134(128+SIGABRT),glibc 留下 malloc(): corrupted top size 这句遗言,死在事后第一个 new 里,错误在 crash.cpp 的 24 行,崩在 31 行。GDB 扒出字节边界:0x21 的块头、24 字节可用、4 字节垫层,arr[6] 起盖住 top chunk 的 size 字段,只写 arr[5] 静默 exit 0,只写 arr[6] 必死 134。ASAN 在 i=5 第一次越界就按住,治本把边界检查交给 vector 的 at()。"
chapter: 15
order: 3
difficulty: intermediate
platform: host
reading_time_minutes: 12
tags:
  - host
  - cpp-modern
  - intermediate
  - 内存管理
  - 容器
  - vector
prerequisites:
  - "卷一·ch04: 指针基础"
related:
  - "悬垂指针:释放之后,指针还活着"
cpp_standard: [11]
---

# 数组:我说了只装五个!——堆缓冲区溢出

上一案咱们追的是悬垂指针:内存都释放了,指针还攥在您手里,读也读了,写也写了,埋下去的炸弹还可能哑火。这一案是它的近亲,祸根同样是几笔不该发生的写,只是方向掉了个头——上一案写的是里面,写进已经不属于您的内存,这一案写的是外面,越出自家数组的边界。往 `new int[5]` 的数组里写 1000 个值,干的就是这么一件事。

这类案子的崩溃报告,笔者是见过好几回的,而且每见一回都想替堆栈里的当事人喊冤:死的位置停在 `operator new` 里,review 的人盯着那一行看半天,它老老实实地来要 64 字节,毛病是半点没有的。真凶其实在几十行之外,藏在一个循环的边界里,写的人自己都没察觉。所以这一案,也是咱们栏目那句招牌话最标准的一次演出:“错误在这里,崩在那里”。空指针那案倒是省心,您在哪一行犯的错,哪一行就把进程带走了。悬垂指针那案就滑头了,其实压根可能不崩,连尸体都不给您留。这一案的错是在 crash.cpp 的 24 行犯下的,人死在了 31 行,中间隔着的是 7 行代码和两个 printf。

## 咱们把它造出来

咱们三行就能把这一案的案发代码造出来,短得是有点过分了:

```cpp
int* arr = new int[5];        // 5 个 int,20 字节
for (int i = 0; i < 1000; i++)
    arr[i] = 0xDEADBEEF;     // 从 arr[5] 开始,全是越界写
```

`new int[5]` 给您 5 个 int、20 字节,多的一分也没有。循环却偏偏要从 0 写到 999,越界从第 6 个元素起就开始了,每一笔都写到了界外。完整的复现代码在配套代码 `code/volumn_codes/crash-lab/03-heap-buffer-overflow/crash.cpp` 里,分配之后咱们还把五个元素初始化成了 0、10、20、30、40,越界写之后又跟了 100 次 `new`/`delete` 和一次 `delete[]`。机器是笔者自己的 WSL2,GCC 16.2.1 配 glibc 2.44 的环境,编译时挂的是 `-g -O0`,咱们也没挂任何消毒器,跑出来的输出是这样的:

```text
Array allocated at 0x60a73d065020, size = 5 ints
Writing far out of bounds (corrupting heap metadata)...
Wrote 1000 values to a 5-element array
Allocating more memory (triggers heap corruption crash)...
malloc(): corrupted top size
```

咱们一行行过,有三处值得您停下来看:

- 1000 个值全部写完了,`Wrote 1000 values` 也打出来了,越界写的过程中,程序毫发无损。
- 最后一行 `malloc(): corrupted top size`,这句不是咱们的 printf,是 glibc 的 malloc 自己喊出来的,喊完进程就没了。
- 循环开场就该打的 `new alloc #0` 连影都没有,程序死在了第一轮 `new int[16]` 的内部。

咱们这次拿到的退出码是 134,Linux 的约定就是 128 加上信号编号,而 6 号信号的名字就是 SIGABRT。空指针那案的 139(段错误)是硬件拦的,咱们这里的 134,则是 glibc 自己动的手:它在 malloc 的入口检查堆,发现管理数据已经对不上了,所以它当场 abort,自己了结了自己。

> glibc 的主动 abort,咱们可以有两个读法。坏处是崩溃点离案发点隔着开头说过的那 7 行,排查的人被指到了无辜的 `new` 头上。而往好里说,它的死干脆利落,总比静默地错下去体面。您要是宁可不崩,后面治本的思路也就有了着落:让越界的那一笔,当场就变成一次能 catch 的 `std::out_of_range`。

## 死的是 new,凶手在 24 行

接下来咱们请 GDB 抓尸体,一把 bt 打下去就有数了(无关的系统帧已截去):

```text
Program received signal SIGABRT, Aborted.
#2  abort () from /usr/lib/libc.so.6
#7  operator new(unsigned long) () from /usr/lib/libstdc++.so.6
#8  main () at crash.cpp:31
```

堆栈最底下停着的那一帧,说的是 `main () at crash.cpp:31`。31 行是哪句您猜?`int* tmp = new int[16];`。这是一次老老实实的分配,来意就是要 64 字节的内存,再没别的了。真正的凶手在 24 行的 `arr[i] = 0xDEADBEEF;`,那 1000 笔的写入,笔笔都是从这一句出去的。

您要是不看源码,这个堆栈能把人带沟里:谁会去怀疑一个刚进门要 64 字节的 `new`?不过反过来说,这堆栈虽然指认了死亡现场,却不肯提半个字的案发现场。您想找到 24 行,手里能用的就只有源码了,再外加一条笔者的经验:要是崩溃死在了分配器内部,凶手多半躲在别处的越界写里。

## 扒开字节看边界

您要看懂 glibc 在检查什么,咱们得回到案发前,把 `arr` 周围的内存摆出来。咱们开 GDB,断点下到了 23 行,越界写其实还没发生,咱们用 `p arr` 拿到地址,再看的就是 `arr-8` 和 `arr+16` 前后的值:

```text
arr = 0x55555556b020 (chunk size field at arr-8, usable = 24 bytes)
0x55555556b018:  0x0000000000000021
--- boundary: your 24 usable bytes end at 0x55555556b038 ---
0x55555556b030:  0x0000000000000028  0x0000000000020fd1
```

> 笔者加在中间的那行 boundary 分隔线,是自己做的标注,其余的都是 GDB 的原样输出。另外您看这个 `0x55555556b020`,跟悬垂指针那案 GDB 里看到的悬垂指针一模一样,其实不是巧合:GDB 默认把地址随机化关掉了,堆的起步点每次都一模一样。您直接跑 `./crash`,拿到的就是您在开头看到的 `0x60a73d065020` 那副样子,而且每次都不一样。

字节咱们一个个认。glibc 的堆不是一大块任您取用的内存,它把堆切成了一个个 chunk(块),每块都带一个自己的 size 字段,记的是自己这块有多大。您拿到 `arr` 指针的时候,头部已经跳过去了,所以 `arr-8` 恰好压着自家这块的 size 字段。

第一行的 `0x21` 分开看是 32 + 1:32 是这块的总大小,最低位的 1 是个标志位(PREV_INUSE),说的就是上一块还在用。您申请的是 20 字节,glibc 对齐之后给了 32 字节的块,再扣掉 8 字节的开销,您名下能用的就有 24 字节,比申请的还多出 4 字节,多出来的 4 字节就是垫层。

第二行是从 `arr+16` 开始 dump 的,两个 8 字节都是有来头的。头一个 8 字节您猜怎么拼出来的?它就是 arr[4] 的值 40,也就是十六进制的 0x28,和垫层拼在一起的样子。后一个 8 字节就更有意思了,位置落在了 `arr+24`——您的 24 字节到这为止,紧跟着的下一块,它的身份可不一般:它就是 glibc 喊出的那句 `corrupted top size` 里的 top,而 top chunk 指的就是它,堆里还没分出去的那一大片连续内存。size 字段上写着的是 0x20fd1,扣掉标志位后的块大小是 0x20fd0,折合 135120 字节的规模。往后您每调一次 `new`,新内存都是从它身上切下来的。

所以边界清清楚楚:arr[5] 落进的是自家垫层,还在您名下的 24 字节之内。而 arr[6] 就不同了,咱们一笔写下去,盖住的就是 top chunk 的 size 字段。

```mermaid
graph LR
  A["arr-8<br/>自家 size = 0x21"] --> B["arr[0]..arr[4]<br/>20 字节"] --> C["垫层 4 字节<br/>arr[5] 落在这里"] --> D["arr+24 起<br/>top 的 size = 0x20fd1"] --> E["top chunk 主体<br/>还没分出去的空闲内存"]
  style C fill:#efe,stroke:#3a3
  style D fill:#fee,stroke:#c33,color:#900
```

咱们把断点挪到 29 行,1000 笔写入都完成了,再 dump 出来的同一片内存,从 `arr+8` 开始已经全成了 `0xdeadbeefdeadbeef`——咱们自己的元素、垫层、top chunk 的 size 字段,全部被 0xDEADBEEF 涂掉了。

咱们再补一笔算术,您就明白 Linux 为什么连写过程中的段错误都不给:咱们写的 1000 个 int 合 4000 字节,而 top chunk 名下有 135120 字节,越界写全都落在了还映射着的页里,页权限又是可写的,CPU 对每一笔都是放行的。空指针那案里把您拦下来的 MMU,在这一案压根没它的戏份,越界的地址是合法映射的内存,所以硬件根本不管。拦不拦这事咱们说了不算,得看 glibc 的检查什么时候来。

```mermaid
graph LR
  A["24 行<br/>越界写盖掉 top size"] -. "没人查<br/>程序继续跑" .-> B["31 行第一个 new"]
  B --> C["malloc 检查 top size<br/>垃圾值过不了这一关"] --> D["abort<br/>exit 134"]
  style A fill:#fed,stroke:#c80
  style D fill:#fee,stroke:#c33,color:#900
```

## 多写一个没事,多写两个必死

垫层和 top size 的边界摆在这了,咱们接着拿 crash.cpp 复制出两份副本做变体实验,其余的代码都原样,一份的越界写只有 arr[5],另一份的越界写只有 arr[6],各跑各的。

只写 arr[5] 的那一遍,后面 100 次 `new`/`delete` 和最后的 `delete[]` 全部都跑完了,程序安安静静地退出了,咱们拿到的是 exit code 0。那一笔落进了 4 字节的垫层,等于白写了都没人发现。

只写 arr[6] 的那一遍就没这么客气了,您看:

```text
arr[6] written
malloc(): corrupted top size
```

咱们拿到的是 exit code 134,死法跟原版的一模一样。arr[6] 盖掉的是 top size 的低 4 字节,size 从 0x20fd1 变成了垃圾值,到了下一个 `new` 进门,glibc 的检查当场把它拦下。

| 变体 | 结果 | 原因 |
|------|------|------|
| 只越界写 arr[5] | 100 次 new/delete 全过,exit 0 | 落在自家 4 字节垫层里,谁也没伤到 |
| 只越界写 arr[6] | 下一个 new 必死,exit 134 | 盖掉 top chunk 的 size 字段,glibc abort |

您喊一声好家伙都不过分:一边是静默的 exit 0,一边是必死的 134,分界线就画在您的 24 字节到哪为止。常听到的说法是越界写越多越危险,方向倒是没错,但实测给出的边界比它锋利得多:要紧的不在于写了几笔,在于哪一笔跨过了 `arr+24`。

C++ 标准可从没承诺过 arr[5] 的安全,今天不崩靠的全是巧合:手头的代码、这个 glibc、这次对齐,缺了哪一样都得变天。您换一个编译器,再或者把分配的尺寸一换,垫层一旦没了,同一行代码立刻就换了一副面孔。咱们再把视角放到真实世界,堆溢出在那边可是安全漏洞的常客:CVE-2021-22555 就是现成的例子,Linux 内核 Netfilter 模块的堆越界写,在真实系统上真的能把权限提上去,靠的就是越界写破坏旁边的堆块、程序还不崩的本事,攻击者爱的就是这样的安静。

## 同一个错,Windows 另一种死法

同一份 crash.cpp 咱们搬到 MSVC 19.51 / Windows x64 上跑,输出是这样的:

```text
Array allocated at 00000208860E8940, size = 5 ints
Writing far out of bounds (corrupting heap metadata)...
crash exit code: -1073741819 (0xC0000005 = STATUS_ACCESS_VIOLATION)
```

您对照着看,味道就出来了。Linux 这边 glibc 挺过了整场 1000 笔的写入,连收尾的 printf 都打完了,死在了事后的第一个 `new` 上,临走还留了句遗言。Windows 这边就没了耐性,连 `Wrote 1000 values` 都没打出来就倒了,它死在了写的过程里,而不是事后的分配里,这一点是输出能证明的。死因再往深里挖的话,咱们手里没有 Windows 堆的字节证据,就不瞎猜了。

同一个错误交到两个堆管理器的手里,演出的就是两种死法。死法是跟着平台变的,说到底跟的就是各家堆管理器的做法,咱们手里的两份输出就是现成的对照。不变的只有一条:越界写妥妥的是未定义行为,标准是不承诺任何结局的。

## ASAN:第一次越界就被按住

老朋友 AddressSanitizer 该出场了,您猜怎么着,ASAN 照样一句话就拿下了这一案。咱们编译时挂上 `-fsanitize=address` 再跑一遍,拿到的输出是这副样子,无关的帧已截去:

```text
==3944==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x77aec5fe0054
WRITE of size 4 at 0x77aec5fe0054 thread T0
    #0 0x5850e07bd31f in main crash.cpp:24

0x77aec5fe0054 is located 0 bytes after 20-byte region [0x77aec5fe0040,0x77aec5fe0054)
allocated by thread T0 here:
    #1 0x5850e07bd22c in main crash.cpp:13
SUMMARY: AddressSanitizer: heap-buffer-overflow crash.cpp:24 in main
```

咱们把报告读下来,跟案卷一样清楚:13 行分配了 20 字节,24 行的是往里写,i=5 的第一笔越界,落点恰好是区域的右端点——0x77aec5fe0040 + 20 = 0x77aec5fe0054,算下来是一分不差的。ASAN 在这笔上就按住了进程,留下的退出码是 1,之后的 994 笔越界压根没了发生的机会。GDB 抓的是尸体,死因还得咱们自己拼。ASAN 抓的是现行犯,哪行分配的、哪行下的手,ASAN 都替您记着。红区是怎么埋的,悬垂指针那案咱们讲过一轮。至于工具家族什么时候用哪个的取舍,[卷六·ASAN 家族](/vol6-performance/ch00-performance-mindset/03-asan-family-and-memory-safety)那篇里有系统的讲解,这里咱们就不再重复了。

## 治本:把边界检查交给容器

咱们查得到,还得治得了。这一案的病根一句话:拿手写索引去管堆数组的做法,边界只活在您的脑子里。治本的思路也就一句话:就是把边界交给容器,让越界从随机时刻的 134,变成当场一次能捕获的异常。

配套的 `fixed.cpp`(与 crash.cpp 同目录)给了三种修法,主线嘛,只有一条:咱们别再 `new[]` 出来自己数下标了。

修法一是咱们上 `vector` 加 `at()`:

```cpp
std::vector<int> arr = {0, 10, 20, 30, 40};
arr.at(5) = 0xDEAD;   // 越界:抛 std::out_of_range,不是 UB
```

`at()` 是每次都做边界检查的,越界了就抛 `std::out_of_range`。咱们真跑 fixed.cpp,catch 到的消息是:

```text
vector::_M_range_check: __n (which is 5) >= this->size() (which is 5)
```

越界的下标是哪个、size 又是多大,一句话都报给您。咱们 catch 住打印出来,程序接着往下走了,exit 0——崩溃变成了错误处理,接下来怎么办,您可以自己定,而不是等堆管理器替您一死了之。

修法二是咱们拿 `size()` 在下标之前把关,`if (i < arr.size())` 成立了才动手。修法三就更干脆了,写的就是范围 for,`for (auto& v : arr)`,连索引都不存在了,越界也就无从谈起了。

至于 `at()` 和 `operator[]` 的选择,笔者的立场是默认 `at()`。它慢的那一点,也就是每次一次的整数比较,绝大多数程序是感知不到的。换回来的是越界必抛异常、当场暴露的错误。`operator[]` 是不检查的,越界了直接就是 UB,只有性能敏感的热循环,而且您已经证明过索引在界内,咱们才值得换它。边界来自用户输入、网络、文件的场合,您别犹豫,闭着眼睛 `at()` 都不算错的。

附带的好处对咱们也是实打实的:`vector` 是自己管内存的,`new[]`/`delete[]` 配错的那类事故,也跟着一并消失了。

配套的代码就放在 `code/volumn_codes/crash-lab/03-heap-buffer-overflow/`,咱们备了一份 crash.cpp、一份 fixed.cpp,您克隆下来,一把 `cmake -B build && cmake --build build` 跑起来就完事了,再把两边的输出对照着看一遍,这案就算您亲手办过了。至于同一块内存 free 两次的 double free,案卷已经在路上了,死法又是跟这三案都不同的。

## 参考

- [cppreference: std::vector::at](https://en.cppreference.com/w/cpp/container/vector/at)
- [CVE-2021-22555: Turning \x00\x00 into 10000$](https://google.github.io/security-research/pocs/linux/cve-2021-22555/writeup.html)
