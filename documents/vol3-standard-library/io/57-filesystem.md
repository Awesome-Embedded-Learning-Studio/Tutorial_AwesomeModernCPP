---
chapter: 7
cpp_standard:
- 17
- 20
description: 讲透 C++17 filesystem 的 path 拼接与归一化、查询、两级迭代器、增删改拷贝操作、异常与 error_code 双错误路径、符号链接的 status 与 symlink_status 之别，并用真实遍历 /usr/include 的 benchmark 揭示"每次进系统调用、缓存为王"的性能真相
difficulty: intermediate
order: 57
platform: host
prerequisites:
- string 深入：SSO、COW 与 resize_and_overwrite
- 迭代器基础与 category
reading_time_minutes: 16
related:
- 容器选择指南：按操作、内存与失效规则挑对容器
tags:
- host
- cpp-modern
- intermediate
- 基础
title: 'filesystem：C++17 跨平台文件系统操作'
---

# filesystem：C++17 跨平台文件系统操作

前面几篇我们都在内存里折腾——容器、迭代器、算法、字符串，数据全在进程内。这一篇把视线挪到进程外：硬盘上的文件系统。

这件事在 C++17 之前一直是块难言之隐。标准库只有 `<fstream>` 能读写文件内容，但"建个目录、查文件大小、递归遍历一个文件夹"这种最基本的需求，标准库一个字都没说。想干就得自己 `#ifdef`：POSIX 走 `opendir`/`stat`/`mkdir`，Windows 走 `FindFirstFile`/`GetFileAttributes`/`CreateDirectory`，两套 API、两种路径分隔符、两种错误码。跨平台项目人手一份 `fs_posix.cpp` + `fs_win.cpp`，维护到后面谁都嫌烦。

C++17 把 Boost.FileSystem 收编进标准，正式落地 `<filesystem>`。一套 API、一个 `path` 类型、一套迭代器，把"跨平台文件系统操作"这件事从手写 `#ifdef` 里彻底解放出来。这一篇我们就把这套库拆开跑一遍——路径怎么表示和拼、怎么查属性、怎么遍历、怎么增删改拷贝、错误怎么处理，最后拿真实遍历 `/usr/include` 的 benchmark 揭示它的性能脾气。文件**内容**的读写（`ifstream`/`ofstream`）归第 56 篇，这里只管文件系统本身的元数据和结构。

## path：可移植的路径表示

整个 `<filesystem>` 的地基是 `std::filesystem::path`。它把一个路径抽象成"一串路径组件的序列"，而不是一个原始字符串——这件事听起来平淡，但它决定了后面所有的拼接、分解、归一化操作都能跨平台正确工作。

`path` 内部用一个实现定义的"原生格式"存储（POSIX 是 `/` 分隔，Windows 同时认 `\` 和 `/`），对外提供一套平台无关的接口。我们先看最常用的分解四件套：

```cpp
// Standard: C++20
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main()
{
    fs::path p = fs::path{"/home"} / "charlie" / "proj" / "main.cpp";
    std::cout << "full path       : " << p << '\n';
    std::cout << "parent_path     : " << p.parent_path() << '\n';
    std::cout << "filename        : " << p.filename() << '\n';
    std::cout << "stem            : " << p.stem() << '\n';
    std::cout << "extension       : " << p.extension() << '\n';
}
```

用 `g++ -std=c++20 -O2`（本机 GCC 16.1.1）跑出来：

```text
full path       : "/home/charlie/proj/main.cpp"
parent_path     : "/home/charlie/proj"
filename        : "main.cpp"
stem            : "main"
extension       : ".cpp"
```

`operator/` 是 `path` 拼接的主力，语义是"在末尾追加一个路径组件"。而 `stem` / `extension` 这一对是处理文件名的黄金搭档：`stem` 是"去掉扩展名的文件名"，`extension` 是"最后一个点开始的扩展名"（含那个点）。注意 `extension` 返回的是 `.cpp` 不是 `cpp`——这跟很多人手写字符串处理的直觉不一样，拼回去的时候别忘了带点。

### operator/ 的反直觉坑：右边带根会吃掉左边

`operator/` 的拼接规则有个很容易踩的坑。它的语义不是无脑字符串拼接，而是"把右操作数**追加**到左操作数后"——**但当右操作数是一个绝对路径（带根名/根目录）时，左操作数会被整个丢掉**，直接返回右边。这其实符合路径语义（绝对路径已经自解释了，前面再拼什么都没意义），但写起来容易懵：

```cpp
fs::path base = "/opt/app";
fs::path joined  = base / "/etc/config";   // 右边是绝对路径
fs::path joined2 = base / "etc/config";    // 右边是相对路径
std::cout << "base / \"/etc/config\" => " << joined << '\n';
std::cout << "base / \"etc/config\"  => " << joined2 << '\n';
```

```text
base / "/etc/config" => "/etc/config"
base / "etc/config"  => "/opt/app/etc/config"
```

第一个例子右边以 `/` 开头，是绝对路径，`base` 整个没了，结果就是 `/etc/config`。这跟 Python 的 `os.path.join` 行为完全一致，但跟纯字符串拼接的直觉相反。所以用 `operator/` 拼路径时，**右边那个片段千万别带开头的 `/`**，否则前面的前缀全白拼。

### lexically_normal 与 lexically_relative：纯词法归一化

`path` 还提供两个纯词法的（lexical，不碰磁盘）变换函数，处理"路径里有点点点"的场景。

`lexically_normal` 把 `.` 和 `..` 在**不访问文件系统**的前提下化简：

```cpp
fs::path messy = "a/b/../../c/./d";
std::cout << "lexically_normal  : " << messy.lexically_normal() << '\n';
// a/b/.. => a, a/.. => (空), c/./d => c/d, 最终 "c/d"
```

```text
lexically_normal  : "c/d"
```

它纯粹按路径组件的规则折叠——`b/..` 抵消掉 `b`，`a/..` 抵消掉 `a`，`.` 是空操作，剩 `c/d`。注意它**不会**把符号链接解开，也不检查这些目录存不存在，纯粹是字符串层面的归一化。想跟着符号链接走，得用 `weakly_canonical` / `canonical`（那俩会真去 `stat`）。

`lexically_relative` 算的是"从路径 A 出发，怎么相对地走到路径 B"：

```cpp
fs::path rel_from = "/a/b/c", rel_to = "/a/b/x/y";
std::cout << "lexically_relative: " << rel_to.lexically_relative(rel_from) << '\n';
```

```text
lexically_relative: "../x/y"
```

从 `/a/b/c` 到 `/a/b/x/y`，要先退一层到 `/a/b`，再进 `x/y`，所以是 `../x/y`。这个函数在生成"相对于某个基目录的相对路径"时特别有用，比如把一堆绝对路径转成相对路径写进配置。

## 查询：exists / file_size / is_directory / last_write_time

有了路径，接下来是查属性。`<filesystem>` 的查询函数分两类：返回值型（`exists`、`file_size`、`last_write_time`）和判断型（`is_directory`、`is_regular_file`、`is_symlink` 一大家子）。用法都很直白：

```cpp
// Standard: C++20
std::cout << "exists(/usr/include)         : " << fs::exists("/usr/include") << '\n';
std::cout << "is_directory(/usr/include)   : " << fs::is_directory("/usr/include") << '\n';
std::cout << "is_regular_file(/usr/include): " << fs::is_regular_file("/usr/include") << '\n';

auto write_tp = fs::last_write_time("/usr/include");
// file_time_type 到 C++20 才有 clock 互转，这里只取 epoch 秒数佐证它能拿到时间
auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                write_tp.time_since_epoch()).count();
std::cout << "last_write_time epoch (sec)  : " << secs << '\n';
```

```text
exists(/usr/include)         : 1
is_directory(/usr/include)   : 1
is_regular_file(/usr/include): 0
last_write_time epoch (sec)  : -4655527457
```

几个要点。`exists` 判存在，`is_regular_file` 和 `is_directory` 判类型，`is_symlink` 判符号链接——注意 `is_regular_file` 内部会**跟随符号链接**（看的是链接指向的真实文件），想判断"它本身是不是链接"用 `is_symlink`。`last_write_time` 返回 `file_time_type`，这是 C++20 起明确了和系统时钟互转的类型（之前那一段 epoch 秒数是负的，是因为这个 `clock` 的 epoch 跟 `system_clock` 不一样，别被吓到，真实工程里直接拿它做时间戳比较就行）。

## 遍历：两级迭代器

`<filesystem>` 给了两个迭代器，正好对应 STL 那套"迭代器遍历序列"的心智模型。这也是为什么本卷前面花大力气讲迭代器——到了文件系统这里，迭代器抽象的威力直接兑现：你可以像遍历一个 `vector` 一样遍历一个目录。

`directory_iterator` 只遍历当前这一层，不往下钻：

```cpp
// Standard: C++20
std::size_t top = 0;
for (const auto& e : fs::directory_iterator("/usr/include")) {
    (void)e;
    ++top;
}
std::cout << "directory_iterator 顶层条目数: " << top << '\n';
```

`recursive_directory_iterator` 会递归往下走，自动钻进子目录：

```cpp
std::size_t all = 0;
for (const auto& e : fs::recursive_directory_iterator("/usr/include")) {
    (void)e;
    ++all;
}
std::cout << "recursive 总条目数          : " << all << '\n';
```

跑出来：

```text
directory_iterator 顶层条目数: 791
recursive 总条目数          : 21173
```

`/usr/include` 顶层有 791 个条目，递归下去总共 21173 个。这就是它俩的差别：一个看表面，一个挖到底。循环里每次解引用拿到的是一个 `directory_entry`，它把"路径 + 这次遍历已经查过的属性"打包在一起——这个"已查过的属性"很关键，性能那一节会专门讲。

::: warning 迭代器遍历期间目录被改了怎么办
`directory_iterator` 是对目录的一次快照式扫描，它**不锁**目录。如果你在遍历的同时别的进程（或你自己）在删/建文件，标准允许迭代器看到也可能看不到这些变化，行为是实现定义的。所以别指望遍历是"一致的快照"——要一致快照，先把目录读进一个 `vector<path>` 再处理。
:::

`recursive_directory_iterator` 还有两个实用开关。一个是构造时的 `directory_options`，比如 `skip_permission_denied`（遇到没权限的目录跳过而不是抛异常）——遍历整个 `/` 的时候几乎必加，不然一个 root-only 目录就让你整个遍历挂掉。另一个是迭代器自己的 `depth()`（当前在第几层）、`recursion_pending()`（要不要继续钻进下一个目录，可以设 false 跳过）。

## 操作：create / remove / rename / copy

增删改拷贝是文件系统操作的另一大块。`<filesystem>` 的命名很统一，看名字基本就知道干啥：

```cpp
// Standard: C++20
fs::path root = "/tmp/fs_demo_dir";
fs::remove_all(root);

// create_directories: 一次建多级目录(中间层不存在也建出来)
fs::create_directories(root / "sub1" / "sub2");
std::ofstream(root / "sub1" / "sub2" / "a.txt") << "hello";
std::cout << "create_directories + 写文件 ok\n";

// copy 单个文件
fs::copy(root / "sub1" / "sub2" / "a.txt", root / "sub1" / "a_copy.txt");
std::cout << "copy a.txt -> a_copy.txt ok, exists=" << fs::exists(root / "sub1" / "a_copy.txt") << '\n';

// copy 目录: 必须加 copy_options::recursive,否则只复制目录本身(空壳)
fs::copy(root / "sub1", root / "sub1_copy", fs::copy_options::recursive);
std::cout << "copy 目录(recursive) ok, sub1_copy/sub2/a.txt exists="
          << fs::exists(root / "sub1_copy" / "sub2" / "a.txt") << '\n';

// rename: 改名/移动,同文件系统内是原子的
fs::rename(root / "sub1" / "a_copy.txt", root / "sub1" / "a_renamed.txt");
std::cout << "rename ok\n";

// remove_all: 递归删整个目录树,返回删除的条目数
auto removed = fs::remove_all(root / "sub1_copy");
std::cout << "remove_all(sub1_copy) 删除条目数: " << removed << '\n';
fs::remove_all(root);
```

```text
create_directories + 写文件 ok
copy a.txt -> a_copy.txt ok, exists=1
copy 目录(recursive) ok, sub1_copy/sub2/a.txt exists=1
rename ok
remove_all(sub1_copy) 删除条目数: 4
```

几个容易踩的点值得展开。

`create_directory` 和 `create_directories` 差一个 `s`，语义差一档：前者只建最末一级，**父目录不存在直接失败**；后者像 `mkdir -p`，中间层全给你补上。新手写 `create_directory("a/b/c")` 但 `a` 和 `b` 都不存在，会收获一个异常或错误码。绝大多数场景你要的是 `create_directories`。

`copy` 是个多面手，靠 `copy_options` 控制行为，常用的几个位：

- `copy_options::recursive` —— 拷目录时递归（不加这个，拷目录只会得到一个空目录壳）；
- `copy_options::overwrite_existing` —— 目标已存在就覆盖（默认不覆盖，源目标都存在时直接跳过，不报错）；
- `copy_options::copy_symlinks` —— 拷符号链接本身（默认是跟随链接，拷的是链接指向的内容）；
- `copy_options::directories_only` —— 只拷目录结构，不拷文件。

`copy_options` 是位掩码（bitmask），多个用 `|` 组合，比如 `copy_options::recursive | copy_options::overwrite_existing`。

`remove` 删单个文件/空目录（返回 `bool`，删没删掉都告诉你），`remove_all` 递归删整棵树（返回删除的条目数，上面那个 `sub1_copy` 删了 4 个：目录本身、sub2、a.txt 各算一个）。`remove_all` 删不存在的路径会安静返回 0，不抛异常——这点比 `remove` 更宽容。

## 错误处理：异常与 error_code 双路径

`<filesystem>` 的错误处理设计是这套库最值得讲透的地方之一。几乎每个会失败的函数都有**两个重载**：一个抛 `filesystem_error` 异常，另一个末尾多一个 `std::error_code&` 出参、不抛。我们拿 `file_size` 查一个不存在的文件，把两条路都走一遍：

```cpp
// Standard: C++20
fs::path bad = "/tmp/this_definitely_does_not_exist_xyz";

// 路径一:不传 error_code,失败抛 filesystem_error
try {
    [[maybe_unused]] auto sz = fs::file_size(bad);   // [[maybe_unused]]: 避免编译器警告返回值没用
} catch (const fs::filesystem_error& ex) {
    std::cout << "抛异常: " << ex.what() << '\n';
}

// 路径二:传 error_code&,失败不抛,错误写进 ec
std::error_code ec;
auto sz = fs::file_size(bad, ec);
std::cout << "error_code 重载: size=" << sz
          << ", ec.value=" << ec.value()
          << ", ec.message=" << ec.message() << '\n';
```

```text
抛异常: filesystem error: cannot get file size: No such file or directory [/tmp/this_definitely_does_not_exist_xyz]
error_code 重载: size=18446744073709551615, ec.value=2, ec.message=No such file or directory
```

两条路的行为差异一目了然：

- **异常版**把失败信息塞进 `filesystem_error::what()`，里头带操作名（"cannot get file size"）、系统错误描述（"No such file or directory"）、以及涉及的路径。信息全、读着舒服，适合"这事必须成，不成程序没法继续"的场景。
- **error_code 版**不抛，失败时 `ec` 被填上错误码（`ec.value()` 是 2，对应 POSIX 的 `ENOENT`；`ec.message()` 是人话描述），函数返回值是个"无效值"——`file_size` 失败返回 `static_cast<uintmax_t>(-1)`，也就是上面那个 `18446744073709551615`（uintmax 的最大值）。

那么什么时候用哪个？这是个实打实的工程判断，不是口味问题。

- **异常版**适合"这个操作的成功是程序正确性的前提"——比如读一个必须存在的配置文件，读不到就该崩，让异常往上冒到能处理的地方（或干脆让它终止）。代码主干干净，不用每行都判 `ec`。
- **error_code 版**适合两种情况：一是**遍历**——你在递归扫一个目录树，某一个子目录没权限或已经被人删了，你不想因为这一个失败让整趟扫描挂掉，想跳过它继续，那就用 error_code 版安静地拿到错误、记个日志、往下走。二是**性能敏感或禁用异常**的场景（嵌入式、某些游戏引擎关了 `-fno-exceptions`），异常机制本身有开销，error_code 是零开销的。

`error_code` 这个类型本身（怎么判、怎么分类、怎么和 `system_category` / `errc` 配合）是个独立的大话题，我们留到第 66 篇专门讲。这里你只需要记住：文件系统操作几乎都提供"传 `error_code&` 不抛"的重载，遍历和容错场景优先用它。

## 权限与符号链接：status vs symlink_status

`<filesystem>` 有两个查状态的函数：`status` 和 `symlink_status`。它俩的差别就一句话——`status` **跟随**符号链接（看的是链接指向的真实对象的属性），`symlink_status` **不跟随**（看的是链接本身）。这个差别在处理符号链接时是决定性的，搞混了会得到完全错误的判断。

我们建一个指向 `/usr/include/stdio.h`（一个普通文件）的符号链接，分别用两个函数看它：

```cpp
// Standard: C++20
fs::path sym_target = "/usr/include/stdio.h";
fs::path sym_link = "/tmp/fs_demo_symlink";
fs::remove(sym_link);
fs::create_symlink(sym_target, sym_link);

auto st = fs::status(sym_link);            // 跟随:看到的是 stdio.h 的属性
auto sl_st = fs::symlink_status(sym_link); // 不跟随:看到的是链接本身
```

把两个 `file_type` 翻译成人话打印出来：

```text
status(link) 类型        : regular       (它跟随了链接,看到 stdio.h 是普通文件)
symlink_status(link) 类型: symlink       (它没跟随,看到这个条目本身是个链接)
```

差别就在这。`status(link)` 看穿链接，报告它是 `regular`（指向的 stdio.h 是普通文件）；`symlink_status(link)` 报告它是 `symlink`（这个条目本身是个链接）。于是用 `is_symlink` 配两个 status 会得到不同结果：

```cpp
std::cout << "is_symlink(status)        : " << fs::is_symlink(st) << '\n';
std::cout << "is_symlink(symlink_status): " << fs::is_symlink(sl_st) << '\n';
```

```text
is_symlink(status)        : 0
is_symlink(symlink_status): 1
```

`is_symlink` 内部调的就是 `symlink_status`，所以它判的是"这个条目本身是不是链接"。而 `is_regular_file`、`is_directory` 这些用的是 `status`（跟随），所以它们看的是链接指向的对象。记住这条规则就不会错：**想看链接本身用 `symlink_status` / `is_symlink`，想看链接指向的东西用 `status` / `is_regular_file` / `is_directory`**。

顺带说一句，前面查询那一节的 `exists` / `is_directory` / `is_regular_file` 都是跟随链接的（用 `status`），所以一个指向目录的链接会被 `is_directory` 报成 `true`——这在大多数场景下是你想要的，但你要做"备份工具、不能跟随链接"之类的活，就得显式用 `symlink_status` 自己判断。

权限用 `perms` 枚举表示，是个位掩码（`owner_read` / `owner_write` / `group_exec` 一大家子），通过 `status(p).permissions()` 拿到，位运算判断：

```cpp
auto pm = fs::status(sym_target).permissions();
std::cout << "owner_write 位: "
          << ((pm & fs::perms::owner_write) != fs::perms::none) << '\n';
// stdio.h 所有人可写? 这里输出取决于你系统,但机制就是这样
```

```text
owner_write 位: 1
```

`perms` 还有个 `perm_options` 配合 `permissions(p, perms, perm_options)` 函数去改权限（`replace` / `add` / `remove`），用得不多但需要时很顺手——`add | symlink_nofollow` 给符号链接本身加权限这种活它能干。

## 性能：每次进系统调用，缓存为王

讲到这里，该面对一个一直回避的问题了：`<filesystem>` 这些操作到底快不快？

答案分两层。第一层，**单个操作的成本基本就是一次系统调用**（`stat`、`readdir`、`mkdir` 之类），加上标准库薄薄一层封装。这个成本不高，但也绝对不是"免费"——系统调用要陷入内核，有上下文切换的开销。

第二层，**大批量遍历的时候，开销会迅速累积**。`directory_iterator` 每前进一步、拿到一个条目，背后就是一次 `readdir`；你想顺便查每个条目的大小或类型，又是一轮 `stat`。几万个条目乘以每个一两次系统调用，开销就上来了。

我们拿真实遍历 `/usr/include`（21000+ 条目）跑个 benchmark，每个条目都顺便 `is_regular_file` + `file_size`，连跑三轮：

```cpp
// Standard: C++20
for (int i = 0; i < kRounds; ++i) {
    std::size_t count = 0;
    std::uintmax_t total_bytes = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        ++count;
        std::error_code ec;
        if (e.is_regular_file(ec)) {
            total_bytes += e.file_size(ec);
        }
    }
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "round " << (i + 1) << ": " << count
              << " entries, " << total_bytes << " bytes, " << ms << " ms\n";
}
```

```text
round 1: 21173 entries, 204330437 bytes, 1352 ms
round 2: 21173 entries, 204330437 bytes, 73 ms
round 3: 21173 entries, 204330437 bytes, 73 ms
```

这三行数字信息量很大。

第一轮 1352 ms，第二、三轮 73 ms，差了将近 20 倍——但代码一行没变。差别在哪？**操作系统的页缓存（page cache）**。第一轮磁盘/目录项还没缓存，每次 `stat` 都得真去存储里翻；第二轮开始这些数据全在内核缓存里，`stat` 就是查内存，快了两个数量级。这是文件系统性能的第一定律：**你的程序快不快，很大程度上取决于你访问的数据在不在内核缓存里**，而不是你用了多花哨的 API。

（绝对值别太当真——换个机器、换个目录、换个时间点，数字会波动。这里要记的是"冷热缓存差一到两个数量级"这个数量级结论，它稳得像铁律。）

### directory_entry 的缓存：能省一次 stat 就省一次

注意上面那个 benchmark 用的是 `e.is_regular_file(ec)` 和 `e.file_size(ec)`——这是 `directory_entry` 的**成员函数**，不是 `fs::is_regular_file(path)` 那种自由函数。这个区别看着不起眼，背后是一套精心设计的缓存机制。

`directory_entry` 在遍历目录、被构造出来的时候，实现通常会**顺手把这一条目的 `stat` 信息缓存起来**（因为遍历本身就已经从 `readdir` 拿到了 inode，顺手查一次属性成本很低）。于是 `e.is_regular_file()` / `e.file_size()` / `e.is_directory()` 这些成员函数，很多时候**直接命中缓存，不用再发系统调用**。

而如果你写成 `fs::is_regular_file(e.path())`、`fs::file_size(e.path())`——传一个 `path` 给自由函数——它会老老实实**再发一次 `stat`**，因为自由函数不知道你手里这个 `path` 刚刚在遍历时已经被查过了。

我们做个对照实验，两组都遍历 `/usr/include` 算总字节，区别只在"用 `directory_entry` 成员"还是"用自由函数重新 stat"，都先 warm up 避开冷缓存干扰：

```cpp
// Standard: C++20
// A: 用 directory_entry 缓存的成员(遍历时已 stat 过,命中缓存)
uintmax_t sum_cached(const fs::path& root) {
    std::uintmax_t total = 0;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        std::error_code ec;
        if (e.is_regular_file(ec)) total += e.file_size(ec);
    }
    return total;
}

// B: 每个条目重新调 fs::is_regular_file / fs::file_size(path) -> 多一次 stat
uintmax_t sum_restat(const fs::path& root) {
    std::uintmax_t total = 0;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        std::error_code ec;
        if (fs::is_regular_file(e.path(), ec)) total += fs::file_size(e.path(), ec);
    }
    return total;
}
```

两轮跑出来（页面缓存都是热的）：

```text
cached (entry members) : 204330437 bytes, 79 ms
re-stat (free fns)     : 204330437 bytes, 108 ms
re-stat / cached       : 1.37x
```

```text
cached (entry members) : 204330437 bytes, 71 ms
re-stat (free fns)     : 204330437 bytes, 110 ms
re-stat / cached       : 1.55x
```

热缓存下，用 `directory_entry` 成员比用自由函数重新 stat 快 **1.4 到 1.5 倍**——就因为后者每个条目多一次 `stat` 系统调用，21000 多个条目乘起来，差出三四十毫秒。这就是标准库给 `directory_entry` 设计那套缓存的全部动机：**遍历时已经拿过的属性，别再要一遍**。

::: warning 遍历时用 directory_entry 成员,别传 path 给自由函数
批量遍历并查询属性时，`e.is_regular_file()`、`e.file_size()`、`e.is_directory()` 这些成员函数命中 `directory_entry` 内部缓存；`fs::is_regular_file(e.path())` 这种自由函数会重新 `stat`。21000 个条目能差出 1.5 倍。遍历循环里**永远优先用 entry 的成员函数**，只有在"这个条目可能在我手里这段时间被改过、需要刷新"时才重新 stat。
:::

总结一下文件系统的性能心智模型：单个操作 = 一次系统调用，不算贵也不算便宜；大批量遍历的成本 = 条目数 × 每条目系统调用次数，省调用就是省钱；而一切之上还有个更狠的变量——内核页缓存，冷热之间差一两个数量级。这套模型跟具体 API 无关，写任何文件系统密集的代码都成立。

## 小结

`<filesystem>` 的核心就一句话：**用一个 `path` 类型 + 一套迭代器 + 一批操作函数，把跨平台文件系统操作统一掉**。几条关键结论收一下：

- **path 是地基**：`operator/` 拼接（右边带根会吃掉左边，别手滑加 `/`）、`parent_path` / `filename` / `stem` / `extension` 分解（`extension` 含那个点）、`lexically_normal` / `lexically_relative` 纯词法归一化（不碰磁盘）。
- **两级迭代器**：`directory_iterator` 只看当前层，`recursive_directory_iterator` 递归到底；遍历不锁目录、不是一致快照，并发改目录行为未定义。
- **操作看名字就会用**：`create_directory` 建单层、`create_directories` 建 `mkdir -p`；`copy` 拷目录要 `copy_options::recursive`；`remove_all` 递归删并返回条目数。
- **双错误路径**：抛 `filesystem_error` 的异常版（主干、必须成功的操作），传 `error_code&` 的不抛版（遍历容错、禁异常场景）——遍历目录树几乎必用后者，单个失败不能拖垮整趟扫描。`error_code` 机制本身见第 66 篇。
- **符号链接的 status vs symlink_status**：`status` 跟随（看链接指向的对象），`symlink_status` 不跟随（看链接本身）；`is_symlink` 用的是后者，`is_regular_file` / `is_directory` 用的是前者。
- **性能三定律**：单操作 = 一次系统调用；批量遍历省调用就是省钱（用 `directory_entry` 成员命中缓存，别传 `path` 给自由函数重 stat，21000 条目差 1.5 倍）；一切之上，内核页缓存冷热差一两个数量级。

文件**内容**的读写——`ifstream`、`ofstream`、二进制 vs 文本模式、读写大文件——那是第 56 篇的内容。本篇聚焦的就是文件系统的"骨架"：路径、属性、结构、操作。两篇合起来，跨平台文件处理的工具箱就齐了。

## 参考资源

- [cppreference: Filesystem library](https://en.cppreference.com/w/cpp/filesystem) —— `<filesystem>` 总览与组件索引
- [cppreference: std::filesystem::path](https://en.cppreference.com/w/cpp/filesystem/path) —— `path` 的拼接、分解、词法变换（`lexically_normal` / `lexically_relative`）
- [cppreference: std::filesystem::directory_entry](https://en.cppreference.com/w/cpp/filesystem/directory_entry) —— 迭代器条目与"缓存过的属性成员"机制
- [cppreference: std::filesystem::copy_options](https://en.cppreference.com/w/cpp/filesystem/copy_options) —— `copy` 的位掩码选项
- [cppreference: std::filesystem::file_status](https://en.cppreference.com/w/cpp/filesystem/file_status) —— `status` 与 `symlink_status` 的跟随语义
