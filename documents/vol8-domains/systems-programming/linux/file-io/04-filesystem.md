---
title: "std::filesystem:目录与元数据"
description: "C++17 把目录、路径与元数据收进了标准库:本篇实测 libstdc++ 的 directory_iterator 完全不排序(顶层序与手搓 readdir 逐项相同,ext4 大目录走 HTree 吐名字散列序,跨进程跨工具稳定)、同一个 ENOENT 在 exists/is_directory/status/file_size 手里得到四种对待(存在性查询把 not_found 当答案连 ec 都清零,file_size 当错误返 uintmax_t(-1) 哨兵)、迭代中目录被 rm -rf 的两种结局(平铺版吃 glibc 32 KiB 缓冲里的陈旧名单,3001 条只见 1022 条静默收尾,递归版在下降点真报 ENOENT)、nofollow 改链接权限在 Linux 吃 EOPNOTSUPP、space() 的 free 与 available 差 5.09% 恰是 ext4 root 预留(与 df 对表)、万文件遍历 2.16/3.42/3.43/16.22 ms 四档解剖(strace 证明差价在每条目构造 fs::path 不在 syscall,is_regular_file 吃 d_type 缓存零新增 syscall,file_size 每条一次 stat)、operator/ 与 lexically_normal 的反直觉、follow_directory_symlink 的环靠内核 40 层链接上限兜住而库把 ELOOP 压住不报(keep.txt 被访问 41 次,ec 全程干净)、双树 diff 实战中 mtime 误报正是 rsync 快速检查的同款代价"
chapter: 8
order: 4
platform: host
difficulty: intermediate
cpp_standard: [17, 20]
reading_time_minutes: 24
prerequisites:
  - "POSIX 文件 I/O:open/read/write 与 fd 的一生"
  - "页缓存与持久性:write() 返回之后发生了什么"
related:
  - "OS 资源的 RAII 范式:fd、HANDLE 与映射的同一副骨架"
  - "错误处理范式:从 errno 到 expected"
  - "Win32 文件 I/O:句柄、CreateFileW 与同步读写"
tags:
  - host
  - cpp-modern
  - intermediate
  - 系统编程
  - POSIX
  - 实战
current_status:
  title: 正在打磨
  detail: 本批刚写完,正在通读打磨,表述与实测口径可能随时调整
  variant: polishing
---

# std::filesystem:目录与元数据

上一篇收尾的时候咱们留了一句话:下一篇回到库这一层。前三篇握在咱们手里的,都是裸 POSIX 的东西,open/read/write 陪 fd 走完了它的一生,mmap 把文件贴进了地址空间,页缓存篇连 write 返回之后的事都看了个遍。这一篇咱们换一层:C++17 起,目录、路径与文件元数据进了标准库,头文件就是咱们今天的 `<filesystem>`,同一份代码拿到 Windows 的平台上也照样编得过。它管的是目录树的遍历、属性的查询与路径的代数。文件内容的读写它可不插手,那活儿还是 fd 与映射的地盘,咱们本篇最后的双树 diff 也只看元数据,一个字节的内容都不读。

标准库这层皮底下是什么?编号的约定咱们在这里交代:本篇的实验按 E1 到 E6 编了号,与仓库 `code/volumn_codes/vol8/systems-programming/linux/file-io/04-filesystem/` 的 01 到 06 目录一一对应,与上一篇的 E 系各管各的,您别把两套对混了。其中 E4 拿 strace 给了答案:libstdc++(GCC 自带的 C++ 标准库实现)的 directory_iterator 与手搓的 opendir/readdir,syscall 的序列逐行同形。所以真正值得咱们看的,是这层包装在 syscall 之外做了什么。一头的活是它替咱们省下的,E4 会把省下的 syscall 数给您看,顺手也把包装自己的价量了出来。另一头是它没有往上报的错:E2 的截断不声不响地收了尾,后面那一声 ELOOP 的事,咱们在输出里连 ec 的影子都找不着。最容易误判的两处,咱们都备了实验。头一处是咱们以为迭代有序,其实 libstdc++ 根本不排序,序是文件系统给的(E1)。另一处是咱们以为没报错就等于结果对,其实同一场 rm -rf 能让遍历静默丢掉三分之二的条目(E2)。

六组实验的原始输出全部入了册,您随时可以对表。环境的口径咱们照例交代清楚:实验跑在笔者的台机上,CPU 用的是 AMD Ryzen 7 9700X,系统跑在 WSL2 的环境里,内核的构建是 6.18.33.2-microsoft-standard-WSL2,g++ 的版本是 16.2.1,strace 用的是 7.2,身份是非 root 的普通用户,E3 的 EACCES 用例靠的就是它。数据树的根 `/home/charliechen/l04_scratch` 在真盘 ext4 上:页缓存篇立过的纪律咱们沿用,/tmp 是 tmpfs 的地界,咱们做语义对比会被它搅浑。编译的口径一律是 `-std=c++20 -O2`。老工具 unique_fd、sys_call、errno_code 沿用 [RAII 篇](../../thinking/01-raii-paradigm.md)和[错误处理篇](../../thinking/02-error-paradigm.md)的定义,咱们不在这里重讲。本篇只添了一件新的:opendir 拿到的 `DIR*` 也交给 RAII 管,咱们给它起名 unique_dir,骨架跟 unique_fd 是一个模子的,后面您见到就能认出来。

## E1:三件套与不排序的 libstdc++

单层遍历的主角是 `directory_iterator`:您给它一个路径,它会把目录里的条目挨个交给咱们。解引用拿到的类型叫 `directory_entry`,它身上挂着条目的全部门牌:`path()` 给的是完整路径(构造时传入的父目录拼上名字),判型的 `is_directory()`、`is_regular_file()`、`is_symlink()` 也都直接挂在条目上。递归的主角叫 `recursive_directory_iterator`,它比单层版多一件事要干:碰到目录条目的时候自动下降,当前的深度得用 `depth()` 问迭代器本人,directory_entry 的身上可没有这个信息。

空口说当然没意思,咱们把三种走法放到同一棵树上跑:e1_trio 建了一棵五层的小树,顶层的名单里混着普通文件、隐藏文件、指向目录的链接与指向文件的链接,另外还留了一根指向空处的悬空链接,然后咱们让三件套各走一遍。第三件是手搓的 opendir/readdir,充当 POSIX 的对照组:

```cpp
// e1_trio.cpp(节选):同一棵树,fs 与手搓各走一遍
for (auto& e : fs::directory_iterator(kRoot))                    // [A] 单层
    order_a.push_back(e.path().filename().string());

for (fs::recursive_directory_iterator it(kRoot), end; it != end; ++it)  // [B] 递归
    order_b.push_back(it->path().lexically_relative(kRoot).string());  // 为什么用 lexically_relative,E5 讲

unique_dir d{::opendir(kRoot)};                                  // [C] 手搓,DIR* 交给 RAII
while (dirent* de = ::readdir(d.get()))                          // 原始序里含 . 和 ..,要自己滤
    if (std::strcmp(de->d_name, ".") && std::strcmp(de->d_name, ".."))
        order_c.push_back(de->d_name);
```

```text
$ ./e1_trio
tree root: /home/charliechen/l04_scratch/e1_tree  (mid 先建、alpha.txt 最后建;readdir 吐的序两者都不像)

== [A] fs::directory_iterator(单层,默认) ==
  link_mid       type=link->dir
  link_zeta      type=link->file
  alpha.txt      type=file
  mid            type=dir
  broken_link    type=link->file
  zeta.txt       type=file
  .hidden        type=file

== [C] opendir+readdir(顶层,原始 d_name 序,含 . 和 ..) ==
  d_name=link_mid       d_type=DT_LNK
  d_name=link_zeta      d_type=DT_LNK
  d_name=alpha.txt      d_type=DT_REG
  d_name=.              d_type=DT_DIR
  d_name=..             d_type=DT_DIR
  d_name=mid            d_type=DT_DIR
  d_name=broken_link    d_type=DT_LNK
  d_name=zeta.txt       d_type=DT_REG
  d_name=.hidden        d_type=DT_REG

== 判定 ==
directory_iterator 顶层序 == readdir 序(去 . ..): yes
directory_iterator 顶层序 == 字典序:              no(非排序)
recursive 的 depth-0 条目序 == [A] 顶层序:        yes(DFS 先序,顶层兄弟序不动)
```

判定行把话挑明了:[A] 与 [C] 的序一模一样(去掉 `.` 与 `..` 之后),跟字典序其实没有半点关系。libstdc++ 是压根没有排序这一步的,标准对目录迭代的顺序也只说了未指定,它的实现就是把 readdir 给的序原样端上来。那这个序到底是谁定的?咱们追下去,发现定序的就是 ext4。它的大目录走的是 **HTree**(哈希索引树,ext4 大目录的索引格式,kernel 文档 directory 一章里有它的布局),查找按名字的散列定位,吐序也就成了散列的顺带产物。至于小目录什么时候变成散列的序,咱们没往下挖,kernel 文档里也没有按条目数量切换的说法,本机的实测倒是给出了一条诚实的边界:眼前这棵顶层才 7 个条目的小树,序就已经既非创建序也非字典序了。E4 那个一万文件的目录里,开头的几个名字长这样:

```text
readdir 前8: f00408 f00721 f02711 f02893 f08519 f03842 f05713 f09678
字典序前8: f00000 f00001 f00002 f00003 f00004 f00005 f00006 f00007
```

您看,f00408 抢了头名,咱们在开头八个位置里找不着 f00000 的影子。这个序既不是创建序(mid 的创建比 alpha.txt 早,却排在了它后面),当然也不是字典序。可它对同一批名字是完全确定的:咱们跨进程、跨工具都拍过,/usr/bin/ls -U、find 给的也是同一个序,重跑的序也纹丝不动,因为散列只认名字的集合,与谁建的、谁后建的都无关。依赖顺序的代码怎么办?排序咱们自己来:咱们把名字收进 vector 排一把,或者照 E6 的做法塞进 map,红黑树顺手就把序办了。

> 笔者本机的 `ls` 是 eza 的别名,它的 `-U` 语义跟 GNU ls 的不一样,头一回对拍的时候,差点让笔者得出一个误判,说 readdir 的序不稳定。您要拍目录的原序,笔者劝您认准 /usr/bin/ls -U。

[C] 那一列 d_type 是咱们要多看两眼的:它是 readdir 顺手带回的类型字段(DT_REG 普通文件、DT_DIR 目录、DT_LNK 符号链接),非链接条目的判型,内核在目录项里就给全了类型,省下的就是一趟一趟的 stat。链接的条目要另算:is_directory() 这一类判型走的是跟随语义,链接背后的类型缓存里没有,问盘还是免不了的——[A] 那一列里 link_mid 的 link->dir 标签就是这么来的,类型缓存说的只是链接,它指向的是不是目录,问了盘才晓得。man 3 readdir 的手册页也交代过,d_type 不是每个文件系统都肯填的,填不了的场合给的是 DT_UNKNOWN,那会儿判型也只能再补一趟 stat 了。ext4 是会填的。这个字段能省下多少 syscall?E4 的实验还会给它单独算一回价。

[B] 的输出咱们略去了,看点倒是两条:递归一路向深处走,顶层兄弟的序与 [A] 完全一致。指向目录的符号链接 link_mid 只是报了名,它底下是没有什么下文的,因为默认的选项不下降目录链接,所以环压根进不来。这个默认值安全吗?咱们把这个问题记在手上,到了 follow_directory_symlink 一节再打开重看。

## E2(上):同一个 ENOENT,四种态度

错误处理的这一块,`<filesystem>` 的每个操作都备了两副面孔:不带 error_code 的重载,失败的时候就抛 `filesystem_error`。带了 error_code 出参的,失败的时候并不抛,错误码就放进了 ec 里。同一个不存在的目录,咱们两副面孔都喂一遍:

```cpp
// e2_forms.cpp(节选)
try {
    fs::directory_iterator it{kMissing};          // 无 ec 重载:失败即抛
} catch (const fs::filesystem_error& e) {
    // what() / code() / path1() 三个字段都在这儿
}

std::error_code ec;
fs::directory_iterator it{kMissing, ec};          // ec 重载:不抛
// 失败时 ec.value()==2,it 是收尾迭代器(空壳)
```

```text
$ ./e2_forms
== [1] directory_iterator(不存在目录)—— 抛异常版 ==
  caught std::filesystem::filesystem_error
  what()       = "filesystem error: directory iterator cannot open directory: No such file or directory [/home/charliechen/l04_scratch/e2/no_such_dir]"
  code().value()    = 2 (ENOENT=2)
  ...(code().message() = "No such file or directory",略)...
  code() == errc::no_such_file_or_directory : true
  path1()      = "/home/charliechen/l04_scratch/e2/no_such_dir"

== [2] 同一操作,error_code 重载 ==
  构造返回后:ec.value()=2 (ENOENT=2)
  ...(ec.message() 一行同上,略)...
  ec == {} 判失败:true(ec 非默认值即失败,可直接当哨兵)
  失败时拿到的是收尾迭代器:it == end(不抛,得空壳)
```

抛出来的 filesystem_error 是 system_error 的亲戚,但它多带了 `path1()`/`path2()`,出事的路径直接长在异常身上,报错的时候您不用自己拼上下文。ec 版交回的则是一个收尾迭代器,跟容器算法里失败就给 end() 的做法是同一个惯例,循环自然就空转过去了。

真正有意思的是非成员函数。咱们把同一记 ENOENT 喂给四个查询函数,拿到的对待完全不同:

```text
== [3] 非成员函数的 error_code 版,同一路径 ==
  fs::exists             ret=false  ec.value()=0   ec.message()="Success"
  fs::is_directory       ret=false  ec.value()=2   ec.message()="No such file or directory"
  fs::status             type=-1 (not_found 枚举值 = -1) ec.value()=2
  fs::file_size          ret=false  ec.value()=2
    file_size 返回值 = 18446744073709551615 (uintmax_t(-1)=错误哨兵)
  ...(部分行的 ec.message() 字段有删节,数值未动,完整版在存档 e2_forms.out)...
```

咱们一个一个看。在 `exists` 的眼里,文件不在这件事本身就是答案:它返回的是 false,ec 也被清成了零,cppreference 的写法是 status 已知就调 ec.clear(),not_found 也算一份已知的 status。`is_directory` 交回的也是 false,可它把 ENOENT 原样留在了 ec 里。`status` 给出 not_found 的类型,类型本身就是它的答案,ec 只是记了个原因。`file_size` 把文件不在当成了错误,交回来的哨兵是 uintmax_t(-1),也就是输出里那一大串的 18446744073709551615。哨兵的用处,是拿一个一眼就能认出的特殊值占住返回位,免得把查不到的失败,看成真有这么大的文件。四种态度摆在了一起:您问在不在,不在就是它的答案。您问多大,不在就成了错误。同一个 errno 在不同的问题底下,身份是不同的。

写代码的时候,您得按问题选函数,判读的时候 return value 与 ec 要一起看。光看 ret 而不看 ec,咱们就会把权限挡住的失败,与查过了确实不在的答案混为一谈。反过来咱们光看 ec,您又解释不了 exists 为什么抹掉了查询失败的信息。两种混同的实测对照,E3 的末尾会给您看。

## E2(下):目录在迭代中没了

第二场实验咱们换个问题:迭代走到一半的时候,目录被别人 rm -rf 了,迭代器会是什么反应?删除不能来自咱们自己,自己删自己的节奏不可控,所以 e2_vanish 用了双进程的编排:子进程慢慢地迭代,每条的间隔是 2 毫秒,走到第 10 条的时候放一个 marker 文件。父进程盯着的 marker 一出现,它就动手跑起了一条 rm -rf。大目录那边放了 3000 个顶层文件加 1 个子目录,基线的条目数是 3001。小树这边摆的是 20 个文件加两个子目录,基线的条目数是 32。另有一趟 `./e2_vanish solo` 不删除的基线,全程 3001 条都见到了,属性查询的成绩是 3000 成 0 败。

```cpp
// e2_vanish.cpp(节选):慢速迭代,删除来自另一个进程
for (fs::directory_iterator end; it != end;) {
    ++steps;
    if (really_delete && steps == 10) touch(marker_a);   // 通知父进程动手
    std::error_code attr_ec;
    auto sz = it->file_size(attr_ec);   // 不吃缓存的属性:每条现问盘上
    if (attr_ec) ++attr_fail; else if (sz == 0) ++attr_ok;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));   // 慢速,给 rm 让出窗口
    std::error_code inc_ec;
    it.increment(inc_ec);               // 增量的失败数单独记
    if (inc_ec) { ++incr_fail; break; }
}
```

```text
$ ./e2_vanish      # 另有 solo 基线一趟,文中已述
[A] directory_iterator,迭代中外部删除
  step=1     name=f0888      attr_ec=0(Success)
  step=2     name=f2525      attr_ec=0(Success)
  ...(step 3 到 6 同为 Success,略)...
  step=200   name=f1431      attr_ec=2(No such file or directory)
  ...(step 400/600/800/1000 全是 ec=2,略)...
  [A 小结] 共见 1022 条(基线应为 3001);increment 失败 0 次;属性查询成功 84、失败 938
  树还在吗:已被 rm -rf

[B] recursive_directory_iterator,迭代中外部删除
  step=1    depth=0 t00
  ...(step 2 到 16 都是顶层文件,略)...
  step=17   depth=0 sub_a
  step=17 increment 失败:2 (No such file or directory) —— 标准把这算错误,递归下降打不开子目录
  [B 小结] 共见 17 条(基线应为 32 = 20 文件 + 2 目录 + 10 子文件);树还在吗:已被 rm -rf
```

[A] 的三个数字请您连起来读:increment 的失败数是 0,属性查询的失败数是 938,最终见到的条目是 1022。rm -rf 明明把整棵树都端了,可是迭代器其实一声错都没报过。为什么?咱们得从 glibc 的 readdir 说起:它在用户态留了 32 KiB 的缓冲,靠 getdents64(读目录内容的系统调用)一趟一趟地装满,之后的 readdir 调用,干的是从缓冲里往外发名字的活。删除发生的时刻,缓冲里的名单是删除之前抄下的,所以陈旧名字照吐不误。属性查询就不一样了,file_size 是不吃缓存的,每条问的都是盘上的现状,而盘上的东西已经没了,所以从第 85 条附近起,attr_ec 全变成了 2。缓冲吐尽了之后,下一趟 getdents64 就对着一具空壳返回了 EOF:目录的 fd 还开着,内容却已经被清空了。迭代就这么收了尾,3001 条的目录只见 1022 条,“不抛、不设 ec、不多走一条”。这就是静默截断的现场。

[B] 的表现就不同了。子目录 sub_a 的名字在删除前进过缓冲,条目也就照常产出了。轮到它下降打开 sub_a 的时候,目录已经没了,这一步 increment(ec) 真的报了 ENOENT,标准是把下降打不开子目录当成错误的,递归版的增量是有报告义务的。同一场 rm -rf 的实验里,平铺版静默地截断,递归版却当场报了错,两种结局您都见识过了。

> 这一套是双进程的软实时编排,您复跑的时候,1022 与 938 这样的具体数字会随节奏出入,但三类现象(陈旧名单、属性的 ENOENT、EOF 处的静默收尾)场场都在,存档 README 里也是这么交代的。

::: warning 没报错,不等于结果对
咱们做清单类的遍历,收尾无异常并不代表数就全了。最便宜的一道核对,咱们拿条目数去对基线:同一目录在不删除的环境下重跑一遍,数出来的就是基线,差得多就是被截断了。要紧的扫描还可以在收尾后用 exists() 复核根目录的存在,或者照着实验里的样子,对关键的属性逐条查 ec。您要是想让递归遍历的每一步都问明白,增量咱们就用 ec 版。平铺版可是连这层保护都没有的,它的增量压根不查盘。
:::

## E3:权限、符号链接与 5.09% 的预留

改权限的路子是 `fs::permissions`,perms 位跟 perm_options 的搭配决定行为,“add 是叠加、remove 是摘除、replace 是整组替换”。咱们改完拿裸的 `::access(X_OK)` 对拍:

```cpp
// e3_perm_space.cpp(节选)
fs::permissions(p, fs::perms::owner_exec, fs::perm_options::add);   // 644 -> 744
::access(p.c_str(), X_OK);                                          // 对拍:可执行

fs::symlink_status(link);   // 不跟随:看链接自身(Linux 上恒 0777)
fs::status(link);           // 跟随:看目标
fs::permissions(link, fs::perms::owner_write,
                fs::perm_options::remove | fs::perm_options::nofollow);  // Linux: 不支持

fs::space_info sp = fs::space(dir, ec);   // capacity / free / available 三件
```

```text
$ ./e3
== [1] fs::permissions 三步 ==
  初始            : rw-r--r-- (644)
  add owner_exec  : rwxr--r-- (744)
  ::access(X_OK)  : 可执行
  remove owner_exec: rw-r--r-- (644)

== [2] 符号链接:fs::status(跟随) vs fs::symlink_status(不跟随) ==
  status(link)          type=1 perms=rw-r--r-- (644)   <- 跟随,看到的是 real.txt
  symlink_status(link)  type=3 perms=rwxrwxrwx (777)   <- 链接自身,Linux 恒 0777
  status(dangling)      type=-1 (not_found=-1)  <- 跟随悬空链接 = 文件不存在
  symlink_status(dangling) type=3 (symlink=3)  <- 不跟随,链接本身在
  nofollow 改链接自身权限:ec=95 ("Operation not supported")  <- Linux 对 lchmod 的态度
  目标 real.txt 权限有没有被误伤:rw-r--r-- (644)
  默认(跟随)remove owner_write 后目标权限:r--r--r-- (444)
```

[2] 这组输出的信息量不小,咱们分三处看。头一处咱们看 status 与 symlink_status 的分工,差别的关键就在跟随与不跟随。前者看到的是目标 real.txt 的 644,后者看到的才是链接自身,Linux 上符号链接的权限恒为 0777。悬空链接的对照更干脆:跟随过去的下场是文件不存在,type 也记成了 not_found。不跟随的话,链接本身还是活着的,type 也只是记了个 symlink。第二处看的是 nofollow:您想改链接自身的权限,Linux 回了 ec=95,EOPNOTSUPP 是它的名字,说的就是操作不支持的错。内核压根没提供改链接权限的通路,fchmodat 带 AT_SYMLINK_NOFOLLOW 的请求它也不认,本机的 6.18 内核实测也是这个态度。链接的权限位在 Linux 上就是个恒定的 0777,放着也没人去读它的值。第三处看不带 nofollow 的默认版:它会跟随到目标上,remove owner_write 真的改了 real.txt 的权限,444 就是这么来的。您想改链接,误伤的却是目标,所以 Linux 上这一步要么不支持,要么改错对象,两版输出正好把两面都摆出来了。

咱们再看空间查询,它交给咱们三个数。三个数各自的分工是:capacity 记的是容量,free 记的是全部空位,available 记的是普通进程实际能用的量。free 与 available 之间差的那一块,恰好就是 ext4 给 root 留的应急空间:

```text
== [3] fs::space("/home/charliechen/l04_scratch") ==
  capacity  = 1081101176832 (1006.85 GiB)
  free      = 1014503858176 (944.83 GiB)   <- 块总量减已用(含 root 预留)
  available = 959511502848 (893.61 GiB)   <- 非特权进程能用到的
  free - available = 54992355328 (5.09%) <- ext4 root 预留的痕迹
  space(不存在路径): ec=2, 字段=18446744073709551615(哨兵 uintmax_t(-1))

== df 对表(同一路径) ==
df: total=1081101176832 used=66597920768 avail=959510900736
```

差出来的 54992355328 字节除以容量正好是 5.09%。man mke2fs(8) 的 -m 条目写得明确,预留的默认百分比就是 5%,实测多出来的 0.09 个百分点,是统计口径带来的差。这笔预留咱们在 df 里也能对上:total 与 capacity 是逐字节相同的,df 的 avail 与 space().available 差着 602112 字节(约 588 KiB),那是两次采样之间盘上有活动的正常漂移。所以哪天您见着盘还剩一大截、却说装不下的怪事,原因其实落在预留上,算术是无辜的。路径不存在的时候,space 交回的是 ec=2,三个字段给的全是哨兵值,哨兵值又是 E2(上)里见过的 uintmax_t(-1)。

最后把 chmod 000 的目录请出来,给 E2(上)的四种对待补上权限侧的最后一例。无权限的目录,iterator 构造时拿到的 ec=13(EACCES,权限拒绝的意思)。在它底下 exists 一个真不存在的文件,ec 里留的是 13,而 ENOENT 的场合 ec 是 0。两个返回 false 的结果在 ret 上长得一模一样,身份却是完全不同的:权限挡住的意思是没查成,文件不在的意思是查过了、确实不在,exists 认的只是后一种答案。您要是写过 exists 为 false 就创建的逻辑,这就是该看 ec 的地方。

## E4:性能解剖,差价在 fs::path 不在 syscall

这一场咱们问的问题很朴素:标准库的迭代比手搓贵多少,贵在哪儿?e4_bench 建了 10000 个文件的目录,四条路线咱们各跑三轮取中位,预热做过了,页缓存也是热的,量的是 syscall 与包装的成本,盘速不在测量的范围里:

| 路线 | 三轮轮值(ms) | 中位 |
|---|---|---|
| a 手搓 opendir/readdir(只拿名字) | 2.15 / 2.16 / 2.20 | 2.16 |
| b directory_iterator(只拿名字) | 3.40 / 3.42 / 3.48 | 3.42 |
| c b + entry.is_regular_file() | 3.42 / 3.43 / 3.49 | 3.43 |
| d b + entry.file_size(ec) | 15.97 / 16.22 / 17.99 | 16.22 |

数字得配上 syscall 的对表才能读出味道,咱们拿 strace 的单发模式各跑一趟,a 与 b 的序列逐行同形:

```text
# strace -e trace=openat,getdents64,close,newfstatat,statx ./e4_bench one b
openat(AT_FDCWD, "/home/charliechen/l04_scratch/e4_big", O_RDONLY|O_CLOEXEC|O_DIRECTORY) = 3
getdents64(3, 0x618190a10180 /* 1024 entries */, 32768) = 32752
...(getdents64 共 11 趟:32768 字节的缓冲每趟装约 1024 条,10 趟有货)...
getdents64(3, 0x618190a10180 /* 0 entries */, 32768) = 0
close(3)                                = 0
```

b 与 a 的 getdents64 都是 11 趟,newfstatat 都是 5 次进程启动的基线,咱们连每趟装多少条都数过,是一致的。所以 a 到 b 多出来的 1.26 ms 不在内核:directory_iterator 每交出一个条目,都要把父目录的路径加名字拼成 fs::path,存进 directory_entry 的身上,字符串的分配要吃堆内存,析构的时候还得还回去,一万条摊下来每条的价约是 126 ns,这是纯用户态的包装价。顺带一处唯一的 flag 差异:glibc 的 opendir 会带上 O_NONBLOCK,libstdc++ 自己的 openat 不带,语义上是没什么影响的。

c 与 b 的速度几乎没差,is_regular_file 白捡了 E1 里说过的 d_type 缓存,新增的 syscall 是零。零的说法有个边界:这棵树上的条目全是普通文件,换到有链接的树,跟随语义的判型就得一条一条补 stat 了。d 可就换了天地:newfstatat 的计数从 5 涨到了 10005,每个条目都摊上了一次 stat。file_size 要问的东西,dirent 里可是没有的,所以只能现问。3.42 ms 变成了 16.22 ms,4.7 倍全是这笔 stat 的开销。E1 目录里的 e1_cache 用 200 条的小目录单独验证过同一件事:type 模式全程只有 5 次基线 newfstatat,size 模式就涨到了 205 次。实验的编号虽然挂在 E1,它的证据在这里正好能用上。落到工程的实践里,在万文件的树上光数名字,跟每条都问一遍大小的做法相比,差出来的有四倍多。咱们想让批量扫描快一点,诀窍就是别问目录项里没有的东西。真要问的话,咱们就得把 stat 的价算进预算。

## E5:path 的代数与几处反直觉

路径的拼接用的是 operator/,它干的活是“安在右边”,它的语义可不是字符串相加,脾气也就跟直觉有了出入。咱们让 e5_paths 把边角全扫了一遍:

```text
== [1] operator/ 的四种脾气 ==
  fs::path("base") / "leaf"    -> "base/leaf"
  fs::path("base/") / "leaf"   -> "base/leaf"
  fs::path("base//") / "leaf"  -> "base//leaf"
  fs::path("base") / ""        -> "base/"
  fs::path("base") / "/abs"    -> "/abs"
  fs::path("base") / "./leaf"  -> "base/./leaf"
  p = "root"; p/="sub"; p/="file.txt" -> "root/sub/file.txt"

== [2] lexically_normal(纯词法) ==
  "./a/../b"                   -> "b"
  "a/b/.."                     -> "a/"
  "a/../.."                    -> ".."
  "../b"                       -> "../b"
  "a//b///c"                   -> "a/b/c"
  "a/./b"                      -> "a/b"
  "/../../a"                   -> "/a"
  "./"                         -> "."
  ""                           -> ""
```

[1] 里最扎眼的要数 `fs::path("base") / "/abs"`:右侧带着根目录的斜杠,operator/ 就把它当成了绝对路径,左侧就被整个顶掉了,拼接的对手如果是外部输入,您拼出来的可能干脆就是右侧那个绝对路径。尾部分隔符的规则也值得记:尾巴上的 base/ 会被吸收,双斜杠的 base// 会原样残留(库只认最后一个分隔符当边界,前面的都留着),词法上它是等价的,字符串上就难看了。日志里见到了别慌,拿 lexically_normal 扫一遍就平了。拼空串的场合则会添一个尾斜杠。

[2] 的 lexically_normal 是纯词法的归一化,`./a/../b` 抵消成 `b` 的过程它连盘都不碰,里面的 a 是不是目录、存不存在,它是一概不管的。`/../../a` 的归一结果是 /a,因为根目录的上面还是根。`../b` 打头的 .. 没有根可以抵消,原样地留着,空串归一完了也还是空串。您真要按盘上的现状解析路径,那活儿是 canonical 的,它是会访问文件系统的。

```text
== [3] path::value_type 与 c_str()(Linux 侧) ==
  sizeof(path::value_type) = 1
  string_type 即 std::string:yes
  printf("%s", p.c_str()) = /usr/bin/env(可直接喂 C API)
  ...(Windows 侧的括注一行,内容已写进下文,略)...

== [4] 迭代 path 的元素 ==
  "/usr/local/include" -> [/] [usr] [local] [include]
  "a/b/c" -> [a] [b] [c]
  "a/b/" -> [a] [b] []
  "/" -> [/]
  "." -> [.]
  ".." -> [..]
  "/abs/path/../x" -> [/] [abs] [path] [..] [x]
```

[3] 给的是 Linux 侧的口径,咱们一行行看:path::value_type 定的是 char,string_type 也就是 std::string 了,c_str() 交回的 `const char*` 能直接喂 C API,e1_trio 里的 fopen 吃的就是它。Windows 那边的 value_type 是 wchar_t,c_str() 给的也是 const wchar_t*。cppreference 写明了,c_str() 与 native().c_str() 是恒等的,换一个写法也是什么都不换的,真要喂窄字符的 C API,您得显式做一次窄转换(比如 string()),或者干脆改用宽字符的 API。这些差异想对拍的话,您看 [目录枚举与 NTFS 家族](../../windows/file-io/04-dir-enum.md),Windows 侧目录枚举的那篇镜像文章,那边就是现成的对拍点。[4] 里藏着个小机关:路径的迭代是按斜杠切段的,`a/b/` 的尾部斜杠后面还跟着一个空段,拿它当文件名就闹笑话了,处理段的时候记得滤空。

还有一个笔者踩过的意外,是在拿 fs::relative 求相对路径的时候发现的:它的内部走的是 weakly_canonical,会把路径里存在的部分解析成真身,符号链接就被解开了。e1_trio 想展示迭代器吐出的原始路径,用的就是 lexically_relative(),不然 link_mid 底下的条目显示出来的全是真身 mid 的路径,实验就成了自说自话。

## follow_directory_symlink:E1 留下的环

E1 里咱们说过,默认的选项不下降目录链接,环也就进不来了。咱们把 follow_directory_symlink 打开试试,libstdc++ 自己会不会检测环?e1_loop 的树很小:根上摆着 loop 目录和真文件 keep.txt,loop 里的 inner 本身是一根链接,链接的目标写着 `..`。咱们从 loop 里看这个 ..,它落到的却是树根,而不是 loop 自己,咱们跟着它进去,就回到了根,环也就这么成了。结果它转起来了:

```text
$ ./e1_loop
== 默认选项:directory_options::none ==
  depth=0 loop (symlink=n)
  depth=1 loop/inner (symlink=Y)
  depth=0 keep.txt (symlink=n)
  共 3 条,正常终止(目录链接不下降,环根本没被走进去)

== follow_directory_symlink ==
  step=1     depth=0   path=loop
  step=2     depth=1   path=loop/inner
  ...(step 3 到 8 每步加深一层,path 在 loop 与 inner 之间往复变长;step 9 到 82 延续爬深,step 83 到 99 已转入 keep.txt 的收拢段,一并略)...
  step=100   depth=46  path=loop/inner/.../loop/inner/keep.txt
  退出时状态:steps=123,it==end ? yes, last_ec.value()=0
  结论:123 步停了,keep.txt(全树就一个真文件)被访问 41 次 —— 同一个环转了 41 圈,遍历结果已被污染
```

它确实停了,但停下的机制三层都是巧合。头一层的巧合在 libstdc++ 自己不做环检测,咱们在标准里也找不到要求,cppreference 的原话是目录结构里有环的时候 end 迭代器可能不可达,检测这活儿留给了写程序的您。第二层在下降的这步上:它用的是 openat(父fd, 名字) 的单组件相对打开,每步解析的只有一个符号链接,永远够不着链接层数的上限:

```text
openat(80, "inner", O_RDONLY|O_CLOEXEC|O_DIRECTORY) = 81
openat(81, "loop", O_RDONLY|O_CLOEXEC|O_DIRECTORY) = 82
```

真正把迭代器拦下来的,咱们后来追到的是第三层:判型的属性查询。newfstatat(stat 家族的现代形态,按完整路径查询)拿到的路径里,符号链接摞到了第 41 个,内核回了 ELOOP——解析一条路径时符号链接最多 40 层,这是 path_resolution(7) 写死的上限,第 41 个正好把它撞破了:

```text
newfstatat(AT_FDCWD, "/home/charliechen/l04_scratch/e1_loop/loop/inner/loop/inner/...(中略,loop/inner 重复了 41 对)...", ...) = -1 ELOOP (Too many levels of symbolic links)
```

最妙的是这一声 ELOOP 的下场:库把它压住了,ec 里也是干干净净的,抛出的异常一个都没有。迭代器一声不吭地停止了下降,DFS 就收尾到了 end()。所以您看到的退出状态是 it==end 且 ec 为 0,keep.txt 在结果里出现了 41 次,全程它是连一次错都没报过的。三层巧合都凑齐了才有这个“恰好会停”的结局,咱们换一套实现或者缓存策略,它还停不停就没有保证了。您真要跟随目录链接,自保的路子是标准的:咱们拿 fs::canonical 配一个 visited 集合,见过的真身就调 it.disable_recursion_pending(),别让迭代器替您兜底。

## E6 实战:双树 diff 与 mtime 的一次误报

三件套、错误形态、权限、性能、路径的代数,连同那个环也一路都过了一遍,咱们收一个真的活儿:两棵目录树的差异清单。判据用的是存在性加 size 加 mtime,一个字节的内容都不读。收清单的 walk 用 std::map 存,键用的是相对路径,序的问题红黑树顺手就解决了:

```cpp
// e6_diff.cpp(节选):两棵树各收一张清单
static std::map<std::string, Info> walk(const char* root)
{
    std::map<std::string, Info> out;
    std::error_code ec;
    for (auto& e : fs::recursive_directory_iterator(
             root, fs::directory_options::skip_permission_denied, ec)) {
        Info info;
        info.type = e.is_directory(ec) ? 'd' : e.is_regular_file(ec) ? 'f' : 'l';
        if (info.type == 'f') info.size = e.file_size(ec);
        info.mtime = e.last_write_time(ec);
        out[fs::relative(e.path(), root).string()] = info;
    }
    return out;
}
```

这段代码把本篇的东西串了起来:收树靠的是递归迭代,问属性的时候咱们逐个带上 ec(E2 的姿势),skip_permission_denied 的选项让无权的目录中止不了整场扫描,遍历跳过了它接着往下走。键里的 relative 在这两棵树里没有链接可解析,倒是也不惹事,您的树里要有链接,E5 的提醒还记得吧。有一句顺带的交代:mtime 拿回来的是 std::chrono::file_clock 的时间点(C++17 起 `<chrono>` 里专为文件时间戳配的钟),打印的时候得走一趟 clock_cast 转到系统钟,排版的活再交给 strftime。

两棵对照树咱们是拿 setup 建的(setup 是 e6_diff 建树时要跑的那个模式),排布上特意做了安排:左右各留一个只有对侧有的目录与文件,一个内容真变了的 mod.txt,一个四字节换四字节的 samesize.txt,一个只碰了时间戳的 keep.txt,再留一个三样全同的 stable.txt:

```text
$ ./e6_diff
left 7 条 / right 7 条;判据 size+mtime,不读内容

== 新增(右有左无) ==
  + dir_new                d size=0 mtime=17:31:04
  + only_right.txt         f size=4 mtime=17:31:04
== 删除(左有右无) ==
  - dir_old                d size=0 mtime=16:31:04
  - only_left.txt          f size=4 mtime=16:31:04
== 修改(两边都有,元数据对不上) ==
  M keep.txt               mtime (5->5 字节,16:31:04->17:31:04)
  M mod.txt                size mtime (5->9 字节,16:31:04->17:31:04)
  M samesize.txt           mtime (4->4 字节,16:31:04->17:31:04)

未变 1 条(stable.txt:内容+size+mtime 全同)。

== 对照:真 diff -rq ==
Only in /home/charliechen/l04_scratch/e6_right: dir_new
Only in /home/charliechen/l04_scratch/e6_left: dir_old
Files /home/charliechen/l04_scratch/e6_left/mod.txt and /home/charliechen/l04_scratch/e6_right/mod.txt differ
Only in /home/charliechen/l04_scratch/e6_left: only_left.txt
Only in /home/charliechen/l04_scratch/e6_right: only_right.txt
Files /home/charliechen/l04_scratch/e6_left/samesize.txt and /home/charliechen/l04_scratch/e6_right/samesize.txt differ
(diff -rq exit=1)
```

清单与 diff -rq 的报告一一对应,整目录的增删也折叠成了一条,粒度与 diff 是对齐的。最有意思的例子是 keep.txt:两边的内容一字不差,setup 只把左侧的时间戳拨慢了一小时,元数据判据就把它报成了 M,读内容的 diff -rq 那边根本不报。您要是觉得眼熟,那是因为 rsync 的快速检查用的就是 size 加 mtime 这套判据,省了读内容的钱,代价就是时间戳被碰过的文件会白传一遍。samesize.txt 补的是另一半:同字节换不同内容,size 的判据抓不住,靠 mtime 才露的馅。要是连 mtime 都是完全一致的,元数据判据就到头了,分高下的活儿咱们只能靠读内容。边界就摆在这儿了,什么时候必须读内容,您心里有数了。

## 另一侧怎么看

std::filesystem 是跨平台的,可它脚下的地层并不跟着跨。Windows 上 path::value_type 换成了 wchar_t,c_str() 交回的是 const wchar_t*,E5 的两条静态断言就是留好的跨平台对拍点。目录枚举到了 Win32 那边,走的是 FindFirstFileW 与 FindNextFileW,NTFS 的硬链接、junction 与符号链接家族,和 ext4 的散列序、恒 0777 的链接各有一比,咱们到 Windows 侧讲目录枚举的镜像篇再对读。眼下已就位的 Win32 地基,您看 [Win32 文件 I/O](../../windows/file-io/01-win32-file-io.md)。

<ReferenceCard title="参考资源">
  <ReferenceItem
    :id="1"
    title="std::filesystem"
    publisher="cppreference"
    url="https://en.cppreference.com/w/cpp/filesystem"
  />
  <ReferenceItem
    :id="2"
    title="std::filesystem::directory_iterator"
    publisher="cppreference"
    url="https://en.cppreference.com/w/cpp/filesystem/directory_iterator"
  />
  <ReferenceItem
    :id="3"
    title="std::filesystem::recursive_directory_iterator"
    publisher="cppreference"
    url="https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator"
  />
  <ReferenceItem
    :id="4"
    title="std::filesystem::exists"
    publisher="cppreference"
    url="https://en.cppreference.com/w/cpp/filesystem/exists"
  />
  <ReferenceItem
    :id="5"
    title="readdir(3)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man3/readdir.3.html"
  />
  <ReferenceItem
    :id="6"
    title="getdents64(2)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man2/getdents.2.html"
  />
  <ReferenceItem
    :id="7"
    title="path_resolution(7)"
    publisher="Linux man-pages (man7.org)"
    url="https://man7.org/linux/man-pages/man7/path_resolution.7.html"
  />
  <ReferenceItem
    :id="8"
    title="ext4 Directory Structure"
    publisher="The Linux kernel documentation"
    url="https://docs.kernel.org/filesystems/ext4/directory.html"
  />
  <ReferenceItem
    :id="9"
    author="Michael Kerrisk"
    title="The Linux Programming Interface"
    publisher="No Starch Press"
    :year="2010"
    url="https://man7.org/tlpi/"
  />
</ReferenceCard>
