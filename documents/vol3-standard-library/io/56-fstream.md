---
chapter: 7
cpp_standard:
- 11
- 17
- 20
description: 讲透 fstream 的三类文件流与 open 模式、RAII 自动 close 的生命周期坑、错误状态机、文本与二进制模式之差、结构体直接 write 的跨平台坑，以及大文件读写为什么该换 mmap / stdio
difficulty: intermediate
order: 56
platform: host
prerequisites:
- string 深入：SSO、COW 与 resize_and_overwrite
- charconv：零开销的数字与字符串互转
- 迭代器适配器：反向、插入与流，把现成迭代器改出新行为
reading_time_minutes: 16
related:
- charconv：零开销的数字与字符串互转
- string 深入：SSO、COW 与 resize_and_overwrite
tags:
- host
- cpp-modern
- intermediate
- 基础
- RAII
title: fstream：文件流读写、RAII 与它的可移植性坑
---

# fstream：文件流读写、RAII 与它的可移植性坑

把一份数据落盘、再把一份配置读回来，是几乎每个 C++ 程序都绕不开的活。标准库给的答案就是 `<fstream>`：`ifstream` 读、`ofstream` 写、`fstream` 读写都能干，背后是 RAII 帮你管文件描述符的生命周期。

看上去是个「打开、读写、关掉」的简单东西，可真写起来你会发现它有一堆反直觉的角落：为什么同样的代码在 Linux 上好好的，到了 Windows 上文本里就多出一堆 `\r`？为什么我 `write` 出去的结构体在另一台机器上 `read` 回来字段全错位？为什么打开失败程序却一声不吭地继续跑？为什么 `close()` 之后再写，数据直接没了？这些坑的根，要么在 C 遗留下来的「文本/二进制」两种模式，要么在结构体内存布局的不确定性，要么在流的那套「状态位 + 自动重置」机制上。

这一篇我们就把 `<fstream>` 从里到外拆一遍。重点不在把每个 API 列一遍，而在讲清楚三件事：**模式标志到底改了什么、RAII 到底帮你管了什么没管什么、以及什么时候你不该用它**。跑的例子全部在本机 GCC 16.1.1 上实测过，输出原样贴出来。

## 三类流与 open 模式：先搞清楚每个标志干了什么

`<fstream>` 给了三个类，本质就是「方向」不同的同一套东西：

- `std::ifstream` —— 默认只读（隐含 `std::ios::in`）；
- `std::ofstream` —— 默认只写，而且**打开即清空**（隐含 `std::ios::out | std::ios::trunc`）；
- `std::fstream` —— 读写都行，但**不会替你隐含任何方向**，要自己写 `in | out`。

真正决定行为的是那串 open mode 标志。把它们当成「打开时对文件做的动作」来记就顺了：

| 标志 | 干什么 | 一句话记忆 |
|------|--------|-----------|
| `in` | 读 | 必须，`ifstream` 自动加 |
| `out` | 写 | 必须，`ofstream` 自动加 |
| `trunc` | 打开时清空文件 | `ofstream` 默认带，最容易翻车 |
| `app` | 每次写前先跳到文件末尾 | 追加日志 |
| `ate` | 打开后定位到末尾（仅一次） | 想知道文件多长 |
| `binary` | 关掉平台相关的字符翻译 | 二进制数据必加 |
| `noreplace`（C++23） | 文件已存在就拒绝打开 | 安全地「只创建新文件」 |

这里最容易踩的是 `trunc`。下面这段代码，很多新手会以为「打开一个已存在的文件、往里写」，结果一打开老内容就没了：

```cpp
// Standard: C++17
#include <fstream>
#include <iostream>
#include <string>

std::string read_all(const char* path) {
    std::ifstream in(path);
    std::string s, line;
    while (std::getline(in, line)) s += line + "|";
    return s;
}

int main() {
    { std::ofstream out("/tmp/m.txt"); out << "OLD"; }
    { std::ofstream out("/tmp/m.txt"); out << "new"; }  // 默认 trunc
    std::cout << "trunc (默认 ofstream): " << read_all("/tmp/m.txt") << "\n";
    return 0;
}
```

```text
trunc (默认 ofstream): new|
```

`OLD` 没了。想保留老内容、只在末尾接着写，得显式加 `app`：

```cpp
// Standard: C++17
{ std::ofstream out("/tmp/m.txt", std::ios::app); out << "APPENDED"; }
```

```text
after app: newAPPENDED|
```

`app` 的语义比「定位到末尾」更狠：它在**每次写操作之前**都强制把写指针挪到文件末尾，无论你之前 `seekp` 到哪。这正好是追加日志想要的行为——多线程各写各的，不会互相覆盖对方的数据区。

::: warning ate 不会救你，trunc 照样清空
一个特别常见的误解：以为 `std::ios::ate`（打开后定位到末尾）能保住老内容。**不能**。`ate` 只是「打开之后把指针挪到末尾」，它**不取消** `ofstream` 默认隐含的 `trunc`。我们在本机实测过：

```cpp
{ std::ofstream out("/tmp/a.txt"); out << "0123456789"; }   // 先写 10 字节
{ std::ofstream out("/tmp/a.txt", std::ios::ate);           // 想保留? 不行
  std::cout << "ate tellp=" << out.tellp() << "\n";
  out << "XY"; }
```

```text
ate tellp=0
```

`tellp` 是 0 而不是 10，说明文件已经被 `trunc` 清空了，`ate` 挪到的是一个空文件的末尾（即位置 0）。最终磁盘上只有 `XY`，`0123456789` 没了。

要「保留老内容、打开时定位到末尾、还能随便 `seekp`」，组合得是 `in | out | ate`：

```cpp
// Standard: C++17
{ std::fstream f("/tmp/a.txt", std::ios::in | std::ios::out | std::ios::ate);
  std::cout << "in|out|ate tellp=" << f.tellp() << "\n";   // 10，内容保住了
  f << "XY"; }
```

```text
in|out|ate tellp=10
content: 0123456789XY
```

记不住没关系，记一条：**只要用 `ofstream` 打开已存在的文件，默认就是清空**，想保留就得 `app` 或 `in | out`。
:::

至于 C++23 新增的 `noreplace`，名字就说明了一切——文件已存在就拒绝打开，专门用来安全地「只创建新文件，不覆盖」。我们本机 GCC 16.1.1 已经支持：

```cpp
// Standard: C++23
#include <fstream>
#include <iostream>

int main() {
    { std::ofstream out("/tmp/np.txt"); out << "original"; }
    // 文件已存在 -> noreplace 拒绝打开
    std::ofstream a("/tmp/np.txt", std::ios::out | std::ios::noreplace);
    std::cout << "已存在: is_open=" << a.is_open() << " fail=" << a.fail() << "\n";
    // 文件不存在 -> 正常创建
    std::ofstream b("/tmp/np_new.txt", std::ios::out | std::ios::noreplace);
    std::cout << "新文件: is_open=" << b.is_open() << " fail=" << b.fail() << "\n";
    return 0;
}
```

```text
已存在: is_open=0 fail=1
新文件: is_open=1 fail=0
```

以前要实现这个语义得 `std::filesystem::exists()` 先查一遍，但「先查再开」有 TOCTOU（time-of-check to time-of-use）竞态——查完到打开之间别人可能就建了文件。`noreplace` 把「不存在才创建」做成了 `open(2)` 的原子操作（底层就是 `O_EXCL | O_CREAT`），竞态从根上消除。写配置文件、PID 文件、锁文件这种「绝不覆盖」的场景，C++23 起用它最稳。

## RAII：析构即 close，但别和手动 close 打架

`<fstream>` 是 RAII 的教科书级应用。文件流对象析构时，底层文件会被自动关闭——你几乎永远不需要手动 `close()`。这意味着一段经典 C 代码：

```cpp
// Standard: C++11
std::ofstream out("config.txt");   // 构造时打开
out << "key=value\n";
// 作用域结束自动 close，哪怕中间抛异常也关
```

构造函数直接接文件名打开，省掉了 C 时代「先 `fopen`、判空、再操作、最后 `fclose`」那一长串。而且异常安全：只要对象构造成功，无论作用域里发生什么，析构都会兜底关闭。

::: warning 手动 close 之后再写，数据直接没了
RAII 自动 close 是好事，但如果你**自己**调了 `close()`，又继续往这个对象里写，事情就微妙了。`close()` 之后流进入「未打开」状态，后续写操作会被静默丢弃：

```cpp
// Standard: C++11
std::ofstream out("/tmp/close_use.txt");
out << "first";
out.close();
out << "second";   // 写给一个已关闭的流
std::cout << "fail=" << out.fail() << " bad=" << out.bad() << "\n";
```

```text
fail=1 bad=1
```

去磁盘上看 `/tmp/close_use.txt`，里面**只有 `first`**，`second` 不翼而飞。`failbit` 和 `badbit` 都被置上了，但程序没报错、没异常，就那么默默吞了。

所以原则是：**要么把生命周期交给 RAII（对象管 close），要么你手动 close 之后就不要再碰这个对象**。非要复用同一个变量，得显式 `out.open("...")` 重新打开，必要时先 `out.clear()` 清掉错误位。两套机制混着用——既手动 close 又指望 RAII——就是数据丢失的温床。
:::

需要手动 `close()` 的场景其实不多，主要一个：你在一个长生命周期对象里持有流，想在析构之前**主动**确认落盘成功。因为析构函数里没法抛异常（会 `std::terminate`），如果 close 时 flush 失败（比如磁盘满），析构只能吞下错误；而你手动 `close()` 后可以检查 `fail()`，主动报告。所以「打开-写-手动 close-检查」是一个合理的「我必须知道这次写成功没」的写法，但记住 close 之后别再用它。

## 错误检查：打开失败一定要报告，别默默继续

流对象有一套状态位机制：`goodbit` / `failbit` / `badbit` / `eofbit`。日常只需要记住两条查询路径：

- 整体健康用 `if (!stream)` 或 `if (stream)`——等价于 `!fail()`，`failbit` 或 `badbit` 任一置位都为真。
- 读到末尾用 `eof()`——只在「尝试读取但读不到更多」时才置位，**不要**拿它当循环的唯一终止条件。

最该养成习惯的是：**打开文件之后立刻检查**。打开失败是最常见的运行时错误（路径错、权限不够、文件不存在），可它默认不抛异常、不报错，你不查就默默继续，后面所有读写都失败，程序输出一堆垃圾还查不出为什么。下面这段就是反面教材：

```cpp
// Standard: C++11
std::ifstream bad("/tmp/does_not_exist_xyz.txt");
int x = 42;
bad >> x;   // 打开失败，读也失败，x 原封不动
std::cout << "没检查打开: x=" << x << " fail=" << bad.fail() << "\n";
```

```text
没检查打开: x=42 fail=1
```

`x` 还是 42，看着像「读到了 42」，其实是压根没读到东西、保留了初值。这种 bug 在生产里能把人逼疯。正确做法是构造完立刻查 `is_open()` 或 `!stream`：

```cpp
// Standard: C++11
std::ifstream in("data.bin", std::ios::binary);
if (!in.is_open()) {                       // 或 if (!in)
    std::cerr << "无法打开 data.bin\n";
    return 1;                              // 早退，别硬撑
}
```

`is_open()` 比 `!fail()` 更精确——它只问「文件是不是真的开着」，不掺和其他错误状态。打开阶段用它最合适。

关于 `eof()` 也有个经典坑：拿 `while (!in.eof())` 当读循环的终止条件，几乎一定会多读一次。因为 `eofbit` 是在「**读操作尝试越过末尾**」之后才置位的，不是在读到最后一个有效字节时。于是循环末尾你会读到一个失败的结果，还以为是有效数据。正确写法是把读操作本身放进循环条件：

```cpp
// Standard: C++11
while (in >> x) {        // 读成功才进循环体，读到 EOF/失败自动退出
    use(x);
}
```

`in >> x` 返回的是流自身的引用，在布尔语境里走 `operator bool`（等价 `!fail()`），读到 EOF 或出错就退出，干净利落。这条规律对 `std::getline` 同理：`while (std::getline(in, line))`。

## 文本模式 vs 二进制模式：一个 `\r` 引发的血案

open mode 里那个 `binary` 标志，是 C 时代留下来的设计，也是 fstream 跨平台坑的源头。

关键在于：**文本模式下，平台会对换行做翻译**。在 Windows 上，你程序里的 `\n`，写出去会变成 `\r\n`，读回来再变回 `\n`；在 Linux/macOS 上 `\n` 就是 `\n`，不翻译。这套翻译对纯文本文件是好事（符合各平台的换行习惯），但对**任何不是纯文本的数据，就是灾难**。

我们在本机（Linux）实测，同一段带换行的字符串，文本模式和二进制模式写出来字节长度完全一样：

```cpp
// Standard: C++17
const std::string data = "line1\nline2\nline3\n";
{ std::ofstream out("/tmp/text.txt");            out << data; }   // 文本模式
{ std::ofstream out("/tmp/bin.txt", std::ios::binary); out << data; }   // 二进制
std::ifstream t("/tmp/text.txt", std::ios::binary | std::ios::ate);
std::ifstream b("/tmp/bin.txt",  std::ios::binary | std::ios::ate);
std::cout << "text:   " << t.tellg() << " bytes\n";
std::cout << "binary: " << b.tellg() << " bytes\n";
```

```text
text:   18 bytes
binary: 18 bytes
```

Linux 上两者一模一样（18 字节），因为 Linux 根本不翻译。但同样的代码搬到 Windows，`text.txt` 会变成 21 字节（三个 `\n` 各被翻译成 `\r\n`，多了 3 字节），`bin.txt` 还是 18 字节。这段「跨平台行为不一致」就是文本模式的本质风险。

更隐蔽的坑在二进制读写：文本模式翻译会**破坏你精心算好的字节偏移**。你 `seekg(100)` 跳到一个位置，文本模式下这个偏移和实际字节可能对不上（因为翻译改变了字节数），`tellg()` 返回的值也不再是文件真实字节位置。所以凡是 `read` / `write` / `seek` 配合的场景，一律上 `binary`。经验法则一句话：**只要数据不是给人读的纯文本，就加 `binary`**。

## 二进制读写：`read` / `write` 与 char 缓冲

`ifstream::read` 和 `ofstream::write` 操作的是**字节**，签名只认 `char*`。想写一个 `int`、一个 `double`、一段自定义数据，都得先取地址、转成 `char*`、配上字节数：

```cpp
// Standard: C++11
std::int32_t n = 42;
double d = 3.14;
std::ofstream out("nums.bin", std::ios::binary);
out.write(reinterpret_cast<const char*>(&n), sizeof(n));
out.write(reinterpret_cast<const char*>(&d), sizeof(d));
```

这个 `reinterpret_cast<const char*>` 几乎是 C++ 二进制 IO 的固定戏法——它不改变字节，只是骗过类型系统让它按字节处理。读回来反过来：

```cpp
// Standard: C++11
std::int32_t n;
double d;
std::ifstream in("nums.bin", std::ios::binary);
in.read(reinterpret_cast<char*>(&n), sizeof(n));
in.read(reinterpret_cast<char*>(&d), sizeof(d));
```

类型安全靠你自己保证：写进去的是 `int32_t`，读出来的也得是 `int32_t`，不能 `int` 在一台机器上是 4 字节、换台机器变 8 字节还指望能对上。所以二进制格式里**永远用定宽整数**（`<cstdint>` 里的 `int32_t` / `uint64_t` 这种），别用裸 `int` / `long`。

## 结构体直接 `write`：那个最诱人、最坑的写法

写到 `int` / `double` 还好，诱人之处在于：一个结构体，能不能也 `reinterpret_cast` 成 `char*` 一把写出去？毕竟它就是一段连续内存嘛。

```cpp
// Standard: C++17  —— 危险写法，别在生产里用
struct Record {
    char name[8];
    std::int32_t id;
    double score;
};
out.write(reinterpret_cast<const char*>(&rec), sizeof(Record));
```

能编译能跑，**同一个编译器、同一台机器**上读写也自洽。但这段代码有三个独立层面的可移植性炸弹，我们一个一个看。

第一个是**字节序（endianness）**。`int32_t` 的 42，在小端机器（x86、ARM 默认小端）上内存是 `2A 00 00 00`，在大端机器上是 `00 00 00 2A`。直接把内存字节写进文件，等于把「机器内部表示」原样落盘。小端机器写的文件，大端机器读回来 `id` 就不是 42 了，是 `0x2A000000` = 704643072。x86 之间、ARM 之间互相读没问题，但只要混入大端设备（某些网络设备、老 PowerPC）就崩。

第二个是**填充（padding）**。C++ 为了内存对齐，会在结构体成员之间和末尾插入填充字节，而填充是**实现定义的**——不同编译器、不同平台插法可能不一样。我们拿本机的实测看：

```cpp
// Standard: C++17
struct Record {
    char name[8];       // 8 字节，偏移 0
    std::int32_t id;    // 4 字节
    double score;       // 8 字节，要 8 字节对齐
};
std::cout << "sizeof(Record) = " << sizeof(Record) << "\n";
std::cout << "offsetof id    = " << offsetof(Record, id) << "\n";
std::cout << "offsetof score = " << offsetof(Record, score) << "\n";
std::cout << "alignof(Record)= " << alignof(Record) << "\n";
```

```text
sizeof(Record) = 24
offsetof id    = 8
offsetof score = 16
alignof(Record)= 8
```

成员加起来明明是 `8 + 4 + 8 = 20` 字节，`sizeof` 却是 24。多出来的 4 字节去哪了？`score` 是 `double`，要 8 字节对齐，可它前面是 `name(8) + id(4) = 12` 字节，不是 8 的倍数，于是编译器在 `id` 后面**塞了 4 字节填充**，把 `score` 推到偏移 16（8 的倍数）；结构体整体对齐又是 8（取最大成员 `score` 的对齐），所以总长要凑成 8 的倍数，`20 + 4(padding 内) = 24` 正好。

问题在于：**这 4 字节填充里装的是什么？没有定义**。很多实现里它是未初始化的内存垃圾。于是你把同一条 `Record` 写出去两次，文件里那 4 字节填充可能完全不一样——同一份数据，写出两份不同的字节序列。读到别处，填充位被忽略还好；如果哪天换编译器、换编译选项（比如 `-fpack-struct`），填充方式一变，字段全错位。

第三个是**类型本身的可拷贝性**。`reinterpret_cast` 成 `char*` 然后 `write` 这套，对**平凡可拷贝（trivially copyable）**类型才安全。一旦结构体里有 `std::string`、`std::vector`、虚函数、指针，这套就彻底崩了——你写出去的是指针值和一堆内部状态，换个进程读回来，指针指向的内存早没了，直接解引用就是段错误。编译器对这种用法有时会告警，但不是所有情况都告。

::: warning 别在生产里这么写
结构体直接 `write` 这套「内存镜像落盘」，只在一个狭窄的条件下成立：同编译器、同平台、同字节序、同对齐选项、类型平凡可拷贝，且你不在乎填充字节里是什么。这些条件一旦任何一个被打破，文件就读不回来了。

正经做二进制文件格式，三个方向选一个：

1. **序列化**：每个字段单独定宽 `write`，字节序自己定（网络格式用大端），填充自己管（字段一个个写就没有填充问题）。代价是啰嗦，但完全可控、可移植。
2. **用现成的序列化库**：protobuf、FlatBuffers、JSON/YAML（文本，跨语言友好）。把「字节序、填充、版本演进」这些脏活交给库。
3. **文本格式**：如果数据量不大、人也要读，直接写文本（CSV / JSON / key=value），用前面 `charconv` 那篇讲的 `from_chars` / `to_chars` 做数字转换。最稳，最可移植。

只有一种场景内存镜像勉强可接受：**纯内部、临时、单机、同进程**的缓存文件（比如某个计算的中间结果，自己写自己读，不出本机）。即便如此，加个 magic number + 版本号头，也比裸写强。
:::

## 定位：seekg / seekp / tellg / tellp

读用 `seekg`（get，读指针）、`tellg`；写用 `seekp`（put，写指针）、`tellp`。`fstream` 同时读写时这两个指针可能是分开的，但大多数实现里共享一个位置。用法直白：

```cpp
// Standard: C++17
{ std::ofstream out("/tmp/seek.txt", std::ios::binary); out << "ABCDE"; }
std::fstream f("/tmp/seek.txt", std::ios::in | std::ios::out | std::ios::binary);
std::cout << "tellg(开头): " << f.tellg() << "\n";      // 0
char c;
f.get(c);
std::cout << "读到: " << c << " tellg: " << f.tellg() << "\n";   // A, 1
f.seekg(2);
f.get(c);
std::cout << "seekg(2) 后: " << c << "\n";               // C
f.seekp(0);
f.put('X');                                             // 覆盖偏移 0
f.seekg(0);
std::string s; std::getline(f, s);
std::cout << "put X 后: " << s << "\n";                  // XBCDE
```

```text
tellg(开头): 0
读到: A tellg: 1
seekg(2) 后: C
put X 后: XBCDE
```

`seekg` 还有个带方向的重载：`seekg(offset, dir)`，`dir` 是 `beg`（开头）/ `cur`（当前）/ `end`（末尾）。想拿文件大小，最经典的招是先 `seekg(0, end)` 再 `tellg()`：

```cpp
// Standard: C++17
std::ifstream in("file.bin", std::ios::binary | std::ios::ate);
auto size = in.tellg();     // 已经 ate 了，直接读
in.seekg(0);                // 别忘了读之前挪回开头
```

构造时直接带 `ate` 是个更简洁的写法——打开即定位到末尾，紧接着 `tellg()` 就是文件大小。注意上面那条 `ate` 的警告针对的是**写**（`ofstream` 隐含 `trunc`）；`ifstream` 用 `ate` 没这个坑，`in` 不会清空文件。

还有个真实的坑：**不能对一些「非随机访问」的流 seek**。管道、终端、套接字这些「流式」设备没有「位置」的概念，对它们 `seekg` / `tellg` 会失败并置 `failbit`。只有普通文件、字符串流这种支持随机访问的才能 seek。

## 性能：fstream 不慢，但用错了就慢

`fstream` 性能名声不太好，坊间常听说「fstream 比 C 的 `fread`/`fwrite` 慢」。这个说法对也不对，我们实测拆一下。

先看**大块缓冲区读写**——一次性 `read` / `write` 一大坨字节。我们写一个 64 MB 的 buffer，分别用 fstream 写、stdio `fread` 读、fstream 读、`mmap` 读：

```cpp
// Standard: C++17  （benchmark 摘要，完整代码见文末说明）
static constexpr std::size_t kBytes = 64 * 1024 * 1024;  // 64 MB
// fstream 写
{ std::ofstream out(path, std::ios::binary); out.write(buf.data(), kBytes); }
// stdio 读
{ std::FILE* f = std::fopen(path, "rb"); std::fread(r, 1, kBytes, f); std::fclose(f); }
// fstream 读
{ std::ifstream in(path, std::ios::binary); in.read(r, kBytes); }
// mmap 读
{ int fd = ::open(path, O_RDONLY);
  char* m = static_cast<char*>(::mmap(nullptr, kBytes, PROT_READ, MAP_PRIVATE, fd, 0));
  std::memcpy(r, m, kBytes); ::munmap(m, kBytes); ::close(fd); }
```

本机（GCC 16.1.1，libstdc++）跑出来的数量级（绝对值会随机器/缓存波动，看数量级）：

```text
file size: 64 MB
fstream 写        :  ~43 ms
stdio fread 读    :  ~28 ms
fstream 读        :  ~26 ms
mmap 读           :  ~22 ms
```

你会发现：**大块读写时，fstream 一点都不慢**，和 stdio 差不多在一个量级。因为 libstdc++ 的 fstream 内部有自己的缓冲区，大块 `read`/`write` 基本就是把用户 buffer 直接倒给底层 `read(2)`/`write(2)`，开销可以忽略。`mmap` 略快是因为它连「拷贝到用户 buffer」都省了（直接把文件页映射进地址空间），但省的也就那一两次拷贝。

那「fstream 慢」的名声从哪来的？从**格式化逐项读写**来。当你写 `out << x << ' '` 一个一个 `int` 往外塞，或者 `in >> x` 一个一个读，每个操作都要经过一次 locale 感知的格式化/解析，这才是真正的瓶颈。我们实测：读 400 万个 `int`（文本格式），三种方式对比：

```text
fstream >> 逐 int  :  ~210 ms
stdio fscanf 逐 int:  ~225 ms
缓冲整块读 + 手写解析: ~59 ms
```

结论很直白：**`fstream >>` 和 `fscanf` 一样慢**（两者都走昂贵的格式化路径，locale、错误检查一个不少），谁也别说谁。真正快的是第三种——**一次性把整个文件 `read` 进内存，再手动解析**。这就回到了 `charconv` 那篇讲的：`from_chars` 不带 locale、不带异常、不带分配，是数字解析最快的路径。

所以性能建议就两条：

1. **二进制大块 IO，放心用 fstream**，不用特意换 `fread`/`fwrite`，数量级一样。
2. **批量读数字 / 文本解析，先整块 `read` 进 `std::string`，再用 `from_chars` 或手写解析逐项处理**，比 `>>` / `fscanf` 快好几倍。

至于 `mmap`，它的优势不在绝对速度，而在**语义**：把整个文件映射进内存，像访问数组一样随机访问，操作系统按需换页。处理超大文件、想零拷贝、或者多个进程要共享同一份只读数据时，`mmap` 是利器。但它把「读失败」从「函数返回错误码」变成「访问内存触发 SIGSEGV」，调试更难，用之前心里要有数。`mmap` 是 POSIX，Windows 上对应的是 `CreateFileMapping`，跨平台要抽象一层——这也是为什么很多人图省事还是用 fstream。

## 和 `std::filesystem::path` 配合（C++17 起）

C++17 给文件流的构造函数加了一个 `std::filesystem::path` 重载。这意味着你可以直接把一个 `path` 对象喂给 `ifstream` / `ofstream`，不用先 `.string()` 转成字符串：

```cpp
// Standard: C++17
#include <fstream>
#include <filesystem>

std::filesystem::path p =
    std::filesystem::temp_directory_path() / "fstream_path_demo.txt";
{
    std::ofstream out(p);   // 直接接 path
    out << "hello from filesystem::path overload\n";
}
std::ifstream in(p);
std::string line;
std::getline(in, line);
std::cout << "read back: " << line << "\n";
std::cout << "path: " << p << "\n";
```

```text
read back: hello from filesystem::path overload
path: "/tmp/fstream_path_demo.txt"
```

这个重载的价值在跨平台，尤其是 Windows：`std::filesystem::path` 内部用原生编码（Windows 上是宽字符 `wchar_t` 路径），直接喂给 fstream 能正确打开那些文件名里有非 ASCII 字符的文件。如果你先 `.string()` 转成窄字符串，在 Windows 上遇到中文/日文文件名就可能打不开。所以路径相关的操作统一交给 `<filesystem>`，把 `path` 对象直接传给 fstream，是 C++17 起最干净、最跨平台的写法。路径本身的拼接、遍历、规范化这些操作归 `filesystem` 管，我们留到下一篇专门讲。

## 小结

`<fstream>` 的核心不是一堆 API，而是几个设计决定和它们带来的坑。几条关键结论收一下：

- **三类流 + open mode**：`ifstream` 读、`ofstream` 写（默认 `trunc` 清空！）、`fstream` 要自己写 `in|out`；`app` 追加、`ate` 打开后定位末尾、`binary` 关掉换行翻译、C++23 的 `noreplace` 原子地只创建新文件。
- **RAII 管生命周期**：析构自动 close，异常安全；但**手动 `close()` 之后别再写**（数据会被静默吞掉），要复用变量就重新 `open()`。
- **打开失败必查**：`is_open()` / `!stream` 立刻判，否则后续读写全失败还默默继续；读循环用 `while (in >> x)` 这种把读操作放条件里的写法，别用 `while (!in.eof())`。
- **二进制必加 `binary`**：文本模式会翻译换行（Windows `\n`→`\r\n`）、破坏字节偏移、让 `seek`/`tell` 不可靠；`read`/`write` 只认 `char*`，定宽数字用 `<cstdint>`。
- **结构体直接 `write` 是坑**：字节序、填充、平凡可拷贝三道坎，任何一道不满足就读不回来；生产里用逐字段序列化、现成序列化库或文本格式。
- **性能**：大块二进制 IO fstream 不慢（和 stdio 同量级）；慢的是格式化逐项 `>>`/`fscanf`，批量场景先整块读再 `from_chars` 解析快好几倍；超大/零拷贝/多进程共享考虑 `mmap`。
- **C++17 起 fstream 直接接 `std::filesystem::path`**，跨平台（尤其 Windows 非 ASCII 文件名）最稳，路径操作细节归 `filesystem`。

下一篇我们把镜头给 `<filesystem>`：路径拼接、目录遍历、文件属性查询这些「文件系统」层面的操作，看 `std::filesystem::path` 怎么和这一篇的文件流无缝配合。

## 参考资源

- [cppreference: `<fstream>`](https://en.cppreference.com/w/cpp/header/fstream) —— 三类文件流与 open mode 总览
- [cppreference: std::filebuf::open](https://en.cppreference.com/w/cpp/io/basic_filebuf/open) —— open mode（含 C++23 `noreplace`）的权威说明
- [cppreference: std::basic_ifstream](https://en.cppreference.com/w/cpp/io/basic_ifstream) —— 构造函数，含 `std::filesystem::path` 重载（C++17）
- [cppreference: `std::ios_base::openmode`](https://en.cppreference.com/w/cpp/io/ios_base/openmode) —— 各 open mode 标志的语义
- [cppreference: `std::fstream` C++23 `noreplace`](https://en.cppreference.com/w/cpp/io/ios_base/openmode) —— P2467 引入的 `noreplace` 标志
