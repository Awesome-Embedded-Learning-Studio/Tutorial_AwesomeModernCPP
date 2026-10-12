---
chapter: 13
difficulty: intermediate
order: 11
platform: host
reading_time_minutes: 15
tags:
- cpp-modern
- host
- intermediate
- 工具链
- 链接器
title: "符号可见性:控制动态库导出什么"
description: "glibc 动态链接器解析符号的完整顺序,三组实验立住:可执行文件自带定义谁都顶不动,LD_PRELOAD 抢在依赖前面,两个普通库同名看 NEEDED 登记顺序。nm -D 揭开默认全导出的现状,三招收窄挨个实测:-fvisibility=hidden 配 default 属性单点放开,hidden 与 static 的差别拿符号表对拍,挡住顶替的效果拿介入实验对拍,-Bsymbolic 在链接期绑定库内引用,版本脚本成批圈定导出名,再看导出表瘦身的字节、strip 剥不动的 .dynsym、C++ 名字修饰与 extern C,收在 CMake 的 C_VISIBILITY_PRESET。"
cpp_standard: [11, 14, 17, 20]
---

# 符号可见性:控制动态库导出什么

咱们在 09 里看着 LD_PRELOAD 把 libcalc 内部那声 base_tax() 也顶掉了,total 也从 117 变成了 1016。当时咱们说好了,谁排前面的完整规则,还有把口子收窄的手段,都留到了今天专门来讲。09 还撂下过一句:口子开着的时候确实方便,可危险也是真的。今天咱们把两头一起兑现:顺序为什么长成了这样,危险又是从哪儿进来的,库作者的手里又有哪些招数,能把口子收窄成只露接口的样子。

咱们要看的顺序,其实就一句话:进程里的一个名字,运行的时候到底谁说了算。可执行文件的定义会不会被别人顶掉,预载的库凭什么总赢得轻松,两个普通依赖重了名的时候又听谁的,这些咱们都拿实验来立。标本还是咱们熟悉的 libcalc,只是这一回咱们把源码重写成了两个翻译单元,还多配了两个内部助手,后面的好几场对拍,反复出场的就是它们俩。

环境咱们照例交代:笔者的实验都在 WSL2 里跑,工具链是 GCC 16.2.1 配 binutils 2.47 加 glibc 2.44 加 CMake 4.4.3 的组合,这一篇里的每一段输出都是真实跑出来的。您那边的版本要是不同,地址与字节数之类的细节可能有出入,机制本身倒是多年没变。

## 谁排前面:三组对拍立住完整顺序

库的骨架,咱们扫一眼就有了数:calc_api.c 管的是对外的一半,base_tax 返回的还是 100,加上了 tax_rate 的 17 之后,再交给内部的助手去收尾。剩下的活都归了 calc_core.c,两位助手就住在这个文件里:一位是负责取整的 near_ceil,干的活是把数目抬到十位,另一位是负责封顶的 clamp_cap,超过 500 的部分全拦下来。头文件里摆着的只有两个接口的声明,助手们的名字连一次都没露过,笔者也从没打算把它们交出去:

```c
// calc_core.c —— 库的内部件:两个助手,从没打算给外人用
int near_ceil(int amount) { return (amount + 9) / 10 * 10; }      /* 取整到十位 */
int clamp_cap(int amount) { return amount > 500 ? 500 : amount; } /* 超 500 封顶 */
```

构建的命令您全都认识,-fPIC 与 -shared 都是 08、09 里练过的。咱们头一步就把基线跑出来,头一组的实验紧跟着做:可执行文件自己带了一份定义,它到底能不能被顶掉?main_def.c 里笔者写的就是一份返回 55 的 base_tax,咱们还顺手把 09 的 LD_PRELOAD 请了回来,预载的 librival.so 里也有一个 base_tax,返回的是 200。

```text
$ gcc -fPIC -shared calc_api.c calc_core.c -I. -o libcalc.so
$ gcc main.c -L. -lcalc -o app && LD_LIBRARY_PATH=. ./app
base=100 total=120
$ gcc main_def.c -L. -lcalc -o app_def && LD_LIBRARY_PATH=. ./app_def
base=55 total=80
$ nm app_def | grep base_tax
0000000000001149 T base_tax
$ LD_PRELOAD=$PWD/librival.so LD_LIBRARY_PATH=. ./app_def
base=55 total=80
```

基线里 117 过 clamp_cap 的时候原样通过,再被 near_ceil 取整成了 120,后面的所有对拍,您都拿它当基准。base=55 倒是不奇怪,定义就攥在 app 自己的手里,咱们上面拿 nm 核出的那行大 T,正是它的真身。要紧的是 total=80:55 加上 17 得了 72,near_ceil 把它取整到了 80。这下清楚了:libcalc 内部 total_tax 的那声 base_tax(),调的也是可执行文件里的 55,库自家写的 100 一次都没被用过。预载进来的 200,连一次被用上的机会都没捞着:可执行文件是顶不掉的,连预载的库也顶不动它。

第二组的实验,轮到两个普通依赖重名了。librival.so 里除了 base_tax,还有一个只有它才有的 rival_note,main_rival.c 把两个函数都用上了,链接器也就一个都省不掉了。同一份 main_rival.c 的源码,咱们按两种顺序各链了一份,再拿 08 里认过的 readelf -d 看登记,末尾咱们捎上第三组:预载的 librival 顶一顶 NEEDED 里的排头:

```text
$ gcc main_rival.c -L. -lcalc -lrival -o app_calc_first
$ gcc main_rival.c -L. -lrival -lcalc -o app_rival_first
$ LD_LIBRARY_PATH=. ./app_calc_first
base=100 note=7
$ LD_LIBRARY_PATH=. ./app_rival_first
base=200 note=7
$ readelf -d app_calc_first | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so]
 0x0000000000000001 (NEEDED)             Shared library: [librival.so]
$ readelf -d app_rival_first | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [librival.so]
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so]
$ LD_PRELOAD=$PWD/librival.so LD_LIBRARY_PATH=. ./app_calc_first
base=200 note=7
```

赢家跟着链接行的顺序切换了,NEEDED 里谁排在了前面,赢的也就是谁(两边都还挂着第三个 NEEDED:libc.so.6,咱们掐掉了)。NEEDED 里的排头 libcalc,还是输给了预载进来的 librival,09 顶掉 base_tax 的那一出,换到眼下的标本照样成立。

咱们把三组对拍凑到一块儿,完整的顺序也就立住了。glibc 的动态链接器解析任何一个符号,都会把进程里的模块从头问过去:可执行文件排头,接着是 LD_PRELOAD 预载的库,再往后才是 NEEDED 清单里的依赖,按登记的顺序来。头一个给出定义的模块赢了,排在后面的同名定义,连被问一句的机会都没有。09 里那个现象的根子,咱们也找到了:库内部调用之所以能被顶,是因为查找走的是进程级的全局顺序,动态链接器可不认识“库的自己人”。咱们讲的是 glibc 的做法,别的平台排法未必相同,可执行文件不被顶的这一点,倒是普遍这么实现的。

危险也就有了具体的模样。您想想:程序链了两个第三方库,两家各有一个没打算导出的内部助手,偏偏重了名。排在后面的库,会把前面那个库的内部调用悄悄顶掉,链接器一声不吭就放行了,运行期的行为就这么变了。这样的问题不炸也不闹,表现出来的只是数字对不上,查起来能耗掉您一下午,对策咱们也有了,就是把库导出的名单收短,三招咱们挨个上。

## 默认全导出:您没打算给的,其实全给了

收窄的事动手以前,咱们得摸清现状,用的工具就是 nm -D。它读的是动态符号表:.dynsym,08 里咱们拿 size -A 分节对拍的时候还见过它露脸。它还有个更直白的身份:库的导出表。名字只要落了进去,进程里的任何模块都看得见,链接也就是一句话的事。咱们看 libcalc.so:

```text
$ nm -D libcalc.so
0000000000001129 T base_tax
0000000000001190 T clamp_cap
0000000000001162 T near_ceil
0000000000004020 D tax_rate
0000000000001134 T total_tax
```

输出里其实还站着四行带 w 的弱引用,是启动代码登记的杂项,与咱们的主题无关,咱们掐掉了。剩下的五行才是要看的东西:base_tax 与 total_tax 是接口,导出本来就是它们的本分。可 near_ceil、clamp_cap 是笔者塞进 .c 文件的内部助手,您翻遍头文件也找不到它们,它们连同 tax_rate 也整整齐齐地躺在导出表里,带着完整的地址与大写字母。您要是头一回看这一眼,大概会跟笔者当时一样愣一下:没写进头文件的符号,也是可能被导出的。

gcc 的默认可见性是 default,不加修饰的全局符号一律公开,助手函数与模块级的全局变量,走的全都是公开的路。每个导出的名字,都是一份对外的承诺:调用方 extern 声明一句就能用上,您想改名、想删掉,都得掂量掂量背后的依赖。名字多了,重名的概率也跟着涨,上一节里查起来能耗掉您一下午的麻烦,就是从这儿来的。

## 收窄一:-fvisibility=hidden,默认收好,单点放开

头一招是编译器的开关 -fvisibility=hidden。visibility 就是标题里“可见性”的英文原词,它说的是一个符号允不允许跨模块被看见。default 与 hidden 是它的两档取值:default 是公开的一档,hidden 是隐藏的一档,命令行上的这个开关,管的是整库的默认档。咱们给同一份源码加上它重编,您再试着把调用方链上去:

```text
$ gcc -fvisibility=hidden -fPIC -shared calc_api.c calc_core.c -I. -o libcalc_hidden.so
$ gcc main.c ./libcalc_hidden.so -o app_hidden
/usr/bin/ld: /tmp/ccfzrPfR.o: in function `main':
main.c:(.text+0xa): undefined reference to `total_tax'
/usr/bin/ld: main.c:(.text+0x11): undefined reference to `base_tax'
collect2: error: ld returned 1 exit status
```

咱们拿 nm -D 验过,五个名字一个也不剩了,全都出了导出表,链接也当场翻了脸。undefined reference 您在 05 就认识了,可这一回定义明明就在库的手里,凭什么还报?因为静态链接器从 .so 上解析符号的时候,看的同样是导出表。hidden 把名字从 .dynsym 里除了名,等于同时对两个链接器关了门:静态的找不到,动态的也找不到。所以一刀切全 hidden 是行不通的,库总得给自家的接口留门。留门的写法,是在接口的声明上挂一个 visibility 属性,取值拨回了 default,接口的两个函数就显式公开了。头文件改成了下面这样,calc_core.c 的部分一个字都不用动:

```c
/* calc.h —— 默认收窄的前提下,接口靠属性显式公开 */
#define CALC_API __attribute__((visibility("default")))

CALC_API int base_tax(void);
CALC_API int total_tax(void);
```

咱们重编了再拍:

```text
$ gcc -fvisibility=hidden -fPIC -shared calc_api.c calc_core.c -I. -o libcalc_hidden_api.so
$ nm -D libcalc_hidden_api.so | grep -v ' w '
0000000000001109 T base_tax
0000000000001114 T total_tax
$ ./app_hidden_api
base=100 total=120
```

导出表里就剩下了两个接口,助手们都下去了,程序照样跑得好好的,base 与 total 分毫不差地贴着基线。咱们常见的那些 CALC_API、MYLIB_EXPORT 宏,它们的真身就是一对搭档:全局默认收,靠的是 -fvisibility=hidden,单点的放开,靠的是头文件里的属性。Windows 那边的 __declspec(dllexport) 干的也是同一件事,CMake 还有把它两头统一的办法,咱们到 CMake 视角再看。

## hidden 不是 static:全库私有,跨翻译单元可用

读到这儿您可能琢磨上了:这不就是 static 么?05 里咱们讲过 static 的内部链接性,名字的有效范围只有自家的翻译单元,不进链接器的全局裁决,咱们还真不能把它当 static。差别就长在咱们的标本上:near_ceil 住在 calc_core.c,调用它的 total_tax 住在 calc_api.c,跨了翻译单元。您把 near_ceil 改成 static,calc_api.c 那声调用就立刻没了着落。笔者实测过,.so 默认允许未定义的符号留到运行期,加上 `-Wl,-z,defs` 才会当场报出 05 里见过的 undefined reference,主角换成了 near_ceil。hidden 倒没有这样的限制,它划的私有范围是整个库:.so 链接的时候,助手还以全局符号的身份参与库内的解析,只是成品对外的导出表里,再没有它的名字了。

咱们眼见为实:在 .o 里,助手是 GLOBAL 搭 HIDDEN:绑定是全局的,可见性是隐藏的。等链成了 .so,它们就变成了 LOCAL,还住在普通符号表 .symtab 里(nm 不带 -D 读的就是它,见到小写 t 的就是它们),只是不在 .dynsym 里了。clamp_cap 的两行一模一样,咱们只摆 near_ceil:

```text
$ gcc -fvisibility=hidden -fPIC -c calc_core.c -o core_hidden.o
$ readelf -sW core_hidden.o | grep near_ceil
     3: 0000000000000000    46 FUNC    GLOBAL HIDDEN     1 near_ceil
$ readelf -sW libcalc_hidden_api.so | grep near_ceil
    15: 000000000000113f    46 FUNC    LOCAL  DEFAULT   11 near_ceil
```

私有化还顺手改了库内部的调用写法。两版 total_tax 的反汇编,咱们挑出了 call 与取数的几行摆开:

```text
$ objdump -d --no-show-raw-insn -M intel libcalc.so        # 默认版,节选
    115b:	call   1040 <near_ceil@plt>
$ objdump -d --no-show-raw-insn -M intel libcalc_hidden_api.so   # hidden 版,节选
    1121:	mov    edx,DWORD PTR [rip+0x2ee9]        # 4010 <tax_rate>
    1138:	call   113f <near_ceil>
```

默认版里的助手们,是可能被顶的导出符号,内部调用照 09 里讲过的机制走了 PLT。到了 hidden 版里,编译器知道它们哪儿也去不了,调用的写法直接换成了相对寻址,连取 tax_rate 都省了过 GOT 的那一跳,因为它也一起 hidden 了。有意思的是 base_tax,它在两版里的那声 call 都是 base_tax@plt:接口公开着,内部调用也就得继续给顶替留余地了。写法上的差别落到运行期,咱们把 09 的介入实验再跑一回。介入说的就是别的模块里的同名定义顶掉库内部调用,这一回顶的对象从 09 的 base_tax 换成了助手 near_ceil,顶替者 libfakeceil.so 定义的就是同名的 near_ceil,返回值改成了加 1000,分别预载到两版的头上:

```text
$ LD_PRELOAD=$PWD/libfakeceil.so LD_LIBRARY_PATH=. ./app
base=100 total=1117
$ LD_PRELOAD=$PWD/libfakeceil.so ./app_hidden_api
base=100 total=120
```

默认版被顶得结结实实的,120 变成了 1117。hidden 版倒是纹丝没动,直连的 call 里,没有留给顶替者的位置。09 说的把口子收窄的手段,您现在见到真身了。

## 收窄二:-Bsymbolic,链接期把库内绑定办完

第二招倒是源码与编译都不动,是给链接器的指令 `-Wl,-Bsymbolic`。它管的只有一件事:库里引用自家导出符号的地方,链接期就直接绑到了自家的定义上,不再留给动态链接器去办了。咱们拿全 default 的源码加上它再链一份:

```text
$ gcc -fPIC -shared -Wl,-Bsymbolic calc_api.c calc_core.c -I. -o libcalc_bsym.so
$ nm -D libcalc_bsym.so | grep -E ' T | D '
00000000000010e9 T base_tax
0000000000001150 T clamp_cap
0000000000001122 T near_ceil
0000000000004008 D tax_rate
00000000000010f4 T total_tax
$ LD_PRELOAD=$PWD/libfakeceil.so ./app_bsym
base=100 total=120
```

您跟上一节对照着看就有意思了:导出表还是满满的五个名字,咱们一个都没藏,照样把顶替者挡在了门外。机制就写在了重定位表里。默认版给咱们的四个符号留了四条动态重定位,tax_rate 占的是一条 GLOB_DAT,三个函数占的各是一条 JUMP_SLOT,类型都是 09 里读过的。bsym 版的表里一条都没剩,链接器在链接期就把 GOT 的槽填好了,动态链接器到了运行期,就没有它的活了:

```text
$ readelf -rW libcalc.so | grep -E 'base_tax|tax_rate|near_ceil|clamp_cap'
0000000000003fd0  0000000700000006 R_X86_64_GLOB_DAT      0000000000004020 tax_rate + 0
0000000000004000  0000000900000007 R_X86_64_JUMP_SLOT     0000000000001129 base_tax + 0
0000000000004008  0000000500000007 R_X86_64_JUMP_SLOT     0000000000001162 near_ceil + 0
0000000000004010  0000000600000007 R_X86_64_JUMP_SLOT     0000000000001190 clamp_cap + 0
$ readelf -rW libcalc_bsym.so | grep -E 'base_tax|tax_rate|near_ceil|clamp_cap'
(没有输出,一条不剩)
$ readelf -d libcalc_bsym.so | grep FLAGS
 0x000000000000001e (FLAGS)              SYMBOLIC
```

FLAGS 里的那个 SYMBOLIC,就是 -Bsymbolic 落在文件里的记号。它和 hidden 的分工,您得摆开了记:hidden 改的是编译期的可见性,治的是“不该给的名字给出去了”。-Bsymbolic 改的是链接期的绑定,导出表倒是原样保留,治的是“给出去的名字被别人抢走了绑定”。代价也有:预载剖析、跟踪类工具从外部替换库函数的路,被它一并断了,09 里说过的那些靠预载 malloc 起家的工具,碰上了带 -Bsymbolic 的库就没辙了。所以 C++ 的库用它要留神,异常匹配与类型识别依赖的,都是某些符号的全局唯一,更常用的是收窄版的 -Bsymbolic-functions,绑的只是函数,放过那些必须全局一致的名字。

## 收窄三:版本脚本,不动源码成批圈定

第三招交给链接器的脚本:版本脚本,咱们把它写在一份 map 文件里,一共就写了这么几行:global 放行两个接口,local 的星号收走其余的名字,咱们的源码一个字都不用动:

```text
$ cat calc.map
{
  global:
    base_tax;
    total_tax;
  local:
    *;
};
$ gcc -fPIC -shared -Wl,--version-script=calc.map calc_api.c calc_core.c -I. -o libcalc_map.so
$ nm -D libcalc_map.so | grep -v ' w '
0000000000001109 T base_tax
0000000000001114 T total_tax
```

效果与 hidden 版的一样干净。它顺手的场合是成批收窄:拿别人一整份源码编库,不好到处插属性:一份 map 文件就办完了。咱们这里用的是匿名的写法,版本节点(map 里那对大括号包住的一块)是不起名字的,导出的符号名是干净的。您要是给节点起了名,符号名的后面会挂上 @@版本号,变成版本化的符号,那是接口代次管理的事,归 12(ABI 设计)的整篇去讲,咱们这儿一步都不多走。

## 导出多少,字节上见分晓

收下来的名字,省了多少字节?咱们拿 size -A 分节对拍,08 里咱们用过同一招:

```text
$ size -A libcalc.so | grep -E '\.dynsym|\.dynstr'
.dynsym               240     776
.dynstr               155    1016
$ size -A libcalc_hidden_api.so | grep -E '\.dynsym|\.dynstr'
.dynsym               168     760
.dynstr               126     928
```

少导出了三个名字,.dynsym 从 240 降到了 168,64 位 ELF 的一条符号记录是 24 字节,咱们算下来三个正好七十二:分毫不差。.dynstr 是符号名的字符串表,三个名字连上结尾的空字符一共 29 字节,155 到 126 的落差,严丝合缝地对得上。咱们这个库拢共五个名字,省下的当然不多,可方向是实的:导出得越多,.dynsym 与 .dynstr 的两节就越大,动态重定位的记录也越多,启动期动态链接器要解析的活就越多。

更重的负担在维护上:收得越少,日后的改动也就越自由,权衡的门道,12(ABI 设计)里专门给咱们展开。

顺着体积咱们再多问一句:04 里认识的 strip,剥得走给人看的 .symtab,可剥不走运行期要用的 .dynsym。咱们实测过,strip 之后的库里,nm -D 里照样数得出咱们的五个名字,普通符号表连 nm 都直说 no symbols 了。.symtab 丢的只是调试信息,.dynsym 是运行期解析离不开的表,strip 也得给咱们留着它。

## 导出的名字:C++ 修饰与 extern C

名单收干净了,还剩下一个您迟早会遇到的问题:导出的名字本身,长什么样?03 里咱们读过名字修饰的工序,C++ 把命名空间、参数类型统统编进了符号名:链接器才分得开这些重载。落到导出表上的样子,咱们用 C++ 写了一份三行的标本,两个重载的 discount,再写一个用 C 的方式导出的 refund:

```cpp
// calcpp.cpp —— 同一个 discount,两副签名
int discount(int percent) { return percent / 2; }
double discount(double ratio) { return ratio * 100.0; }

extern "C" int refund(int amount) { return amount / 10; }
```

```text
$ g++ -fPIC -shared calcpp.cpp -o libcalcpp.so
$ nm -D libcalcpp.so | grep -E 'discount|refund'
00000000000010fe T _Z8discountd
00000000000010e9 T _Z8discounti
000000000000111a T refund
```

两个 discount 都进了导出表,名字是 _Z 打头的修饰串,过了 c++filt 一解(它干的活就是把修饰名解回原形),i 结尾的是 discount(int),d 结尾的是 discount(double)。挂了 extern C 的 refund,名字就是干净的本名。修饰名扛住了重载,可它认编译器:g++ 与 clang++ 用的规则一致,MSVC 就是另一套了。跨编译器、跨语言认名字的场合,靠的都是 extern C 的声明,代价是它名下再也不能有重载了。跨工具链的接口怎么定名字,dlsym 按名取函数的活怎么干,这些是 13(运行时加载与插件)和 12 还要回来接着讲的,咱们在这儿认个脸熟就行。

## CMake 视角

手敲 -fvisibility=hidden 的活,CMake 有现成的目标属性:C_VISIBILITY_PRESET,C++ 目标上的同款叫 CXX_VISIBILITY_PRESET。咱们把工程摆出来,头文件用的是挂好了 CALC_API 的版本:

```cmake
cmake_minimum_required(VERSION 3.20)
project(calc_vis C)

add_library(calc SHARED calc_api.c calc_core.c)
set_target_properties(calc PROPERTIES C_VISIBILITY_PRESET hidden)

add_executable(app main.c)
target_link_libraries(app PRIVATE calc)
```

咱们拿 --verbose 对拍,真实的编译行是:

```text
$ cmake -B build -S . && cmake --build build --verbose
/usr/sbin/cc -Dcalc_EXPORTS  -fPIC -fvisibility=hidden -MD -MT CMakeFiles/calc.dir/calc_api.c.o -MF CMakeFiles/calc.dir/calc_api.c.o.d -o CMakeFiles/calc.dir/calc_api.c.o -c /tmp/calc_vis/calc_api.c
```

-fvisibility=hidden 进了每一个编译行,链接行笔者裁了。旁边那个 -Dcalc_EXPORTS 是 CMake 顺手定义的宏,本意是伺候 Windows 的导出头文件,咱们 Linux 这边用不上它。产物的导出表咱们也验过了,跟手敲版长成了一个模子:nm -D 里剩下的正是两个接口,程序跑出的也是 base=100 total=120。分工也就清楚了:目标属性管的是全局默认收窄,头文件里的宏管的是接口单点放开,一对正好是“收窄一”讲的那套机制。同一家还有个 VISIBILITY_INLINES_HIDDEN 的属性,咱们一句话带过:它把内联函数的导出也收了起来,给 C++ 的目标配上还能再省一点。想要一份宏在 Linux 与 Windows 两头都使得的,再去看 CMake 的 GenerateExportHeader,它生成的头在 Linux 展开成的是 visibility 属性,在 Windows 展开的就成了 dllexport 与 dllimport,那就是另一块话题了。

## 接下来:接口本身怎么管

咱们走到这里,09 留的口子两头都堵上了:查找的顺序,您有了三组实验的完整证据,三招的收窄,也都各自实测过了。可导出表收干净了,也只是解决了给多少、给谁,接口本身还是散的:参数怎么传,结构体的字段怎么摆,版本升了,旧程序怎么接着用。这些对外的约定合起来有个名字:ABI,咱们的下一篇 12(ABI 设计)整篇就讲它,把接口当成正经的产品来设计。
