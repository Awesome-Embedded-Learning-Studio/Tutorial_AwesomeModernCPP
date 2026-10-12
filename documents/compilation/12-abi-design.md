---
chapter: 13
difficulty: intermediate
order: 12
platform: host
reading_time_minutes: 17
tags:
- cpp-modern
- host
- intermediate
- 工具链
- 链接器
title: "ABI 设计:接口怎么定,版本怎么升"
description: "ABI 破坏现场拿升级版 libcalc 实测:结构体中间插字段、参数 int 换 double,app 不重编,数字错得悄无声息,nm 与 readelf 全看不出差别,objdump 拍下两边各按各的调用约定取参。三段名答版本升级:正式名升代新旧两库共存老程序照跑,真实名记修复,链接名永远指最新。版本脚本节点命名与 @@ 双 @ 默认版本,同名函数两代并存于一个库,COPY 重定位把可执行文件的副本定死在旧尺寸,glibc 喊完警告照常放行,构造顺序拿链接顺序对翻再拿函数局部 static 收拾,定名看 Itanium 与 MSVC 两套修饰(MSVC 侧为存档输出),extern C 配完整 ABI 头跨工具链,收在 CMake 的 SOVERSION 与 generate_export_header。"
cpp_standard: [11, 14, 17, 20]
---

# ABI 设计:接口怎么定,版本怎么升

11 的结尾,咱们把导出表收得只剩两个接口,散在外头的三个问题,原样递到了今天:参数怎么传,结构体的字段怎么摆,版本升了,旧程序怎么接着用。当时咱们也给这些对外的约定起了个总名:ABI。今天咱们就把它当正经对象来对付:定形、定名、定版本。标本还是咱们的 libcalc,咱们这回给它升了级:calc_item 记件数和单价,line_total 负责按件数乘单价算出一行的总额。这个小小的结构体,把三问里的两问都拉进了现场,您马上就会看到它比链接失败难缠得多。

环境照例交代:笔者的实验都在 WSL2 里跑,工具链用的是 GCC 16.2.1 配 binutils 2.47 加 glibc 2.44 加 CMake 4.4.3,这一篇里的每一段输出都是真实跑出来的。您那边版本不同,错的数值、字节数这类细节会变,机制本身倒是多年没变。

## ABI:头文件之外的那一层

咱们把 API 与 ABI 摆在一起认。头文件里的那几行声明,参数什么型、返回什么型,是源码层的约定:行话叫 API。您的源码包含进来能编过:这就叫 API 兼容。可是编译只把源码翻成了机器指令。指令层面还有一整套的约定得对上,咱们接着问:符号名怎么编码?参数从哪个寄存器取?结构体多大、字段排在哪?返回值又放在哪?这一层就是咱们说的 ABI:应用二进制接口(Application Binary Interface),它管的是编好的二进制之间怎么对接。两层的分别其实不绕:API 兼容是源码照编,ABI 兼容是编好的旧二进制照样接新库。前者有编译器替咱们把关,后者没有任何工具全程替您把关,咱们马上让它出事。

## 破坏现场:一个字段插进去,数字悄悄错了

咱们头一步就上真家伙。v1 的头文件里,calc_item 的字段就两个。咱们让 app 按 v1 编好,算的是 3 件单价 2500 的一行:

```c
/* calc_v1.h —— libcalc v1 的对外头文件(Include guard 省了,您别学) */
struct calc_item {
    int units;   /* 件数 */
    int price;   /* 单价,分 */
};

int line_total(struct calc_item item);
```

```text
$ gcc -fPIC -shared calc_v1.c -I. -o libcalc.so && gcc app.c -I. -L. -lcalc -o app && LD_LIBRARY_PATH=. ./app
units=3 price=2500 total=7500
```

现在库升 v2:头文件在 units 和 price 中间插了一个 int discount,实现改成了乘完减折扣。app 咱们一个字节都不动,只把 libcalc.so 换成 v2 编的:

```text
$ gcc -fPIC -shared calc_v2.c -I. -o libcalc.so && LD_LIBRARY_PATH=. ./app
units=3 price=2500 total=1620446788
$ LD_LIBRARY_PATH=. ./app
units=3 price=2500 total=-1986195468
```

您盯着这几行看:不崩、不喊、数字错,而且每次错得都不一样。咱们把能上的工具都上了:nm -D 看两版库,导出的都是同一个 line_total,一个模子刻出来的。readelf 咱们翻遍了动态段,也找不到一处报警的记录。编译错误至少会把话说到您脸上,而 ABI 破坏一个字都不说,它只是安静地算错,这就是它比编译错误危险的地方。咱们顺着调用现场找错在哪儿。app 是按 v1 编的,它认的 calc_item 是 8 个字节,机器按调用约定(参数怎么分寄存器的规范,下面咱们细说)把它整个放进 rdi。库是按 v2 编的,它认的 calc_item 有 12 个字节:头 8 字节取的是 rdi,末 4 字节取的是 rsi。于是 units 读到了 3,discount 读到 2500(app 传进去的单价),price 读的是 rsi 里残留的旧值,里面是什么没人保证过。3 乘上一个没人保证的数再减 2500,就成了那串每次都不同的错数。结构体的字段怎么摆,答案也就出来了:字段排布是 ABI 的一部分,中间插了一个,排在后面的全部错位。

参数怎么传,咱们换一个更小的改动来看:只改参数类型。v1 的库导出 int boost(int),app 是按 int 版编的:21 传进去、42 回来。咱们让库侧的 v2 把参数换成 double,函数名的拼写一个字不动:

```text
$ gcc -fPIC -shared boost_v1.c -o libboost.so && gcc app.c -L. -lboost -o app && LD_LIBRARY_PATH=. ./app
boost(21)=42
$ gcc -fPIC -shared boost_v2.c -o libboost.so
$ LD_LIBRARY_PATH=. ./app
boost(21)=0
```

这个 0 其实不是运气,咱们拿 objdump 把两边的话都对上(输出是节选):app 那头放的是 `mov edi,0x15`,库这头偏偏是 `movsd QWORD PTR [rbp-0x8],xmm0`。app 把 21 放进了 edi,库取的却是 xmm0 里的 double。这里出面管事的,就是刚才点名的调用约定:同一个进程里,整数参数走哪个通用寄存器、浮点走哪个 XMM 寄存器、返回值放在什么地方,全都得按同一套的规范来,咱们这套环境用的是 x86-64 SysV(System V 应用二进制接口,Unix 定下的规范)。平日两边倒是相安无事,是因为编译器翻的是同一份头文件、两头的动作天然一致。一旦换了头文件,符号的名称还是同一个 boost,链接器就高高兴兴把两边接上了,谁也不知道约定已经分岔了。所以参数怎么传的答案,不在于头文件写了什么,在于两边编出来的取数动作是不是同一套。

## 升级纪律:三个名字各管一段

接口变了库就得升版本,这一段咱们把 10 留下的活儿补齐。10 里咱们摆过三件套的软链:libcalc.so、libcalc.so.1、libcalc.so.1.2.3,说了三个名字各管一段,版本号三段各记什么,留到了今天。三个名字对着版本号的三段,合起来的行话叫三段名。真实名带完整的三段(libcalc.so.1.2.0),正式名只留头上的一段(libcalc.so.1,登记在库的 SONAME 里的正是它),链接名的做法干脆:libcalc.so,版本段什么都不带。咱们真做一遍:

```text
$ gcc -fPIC -shared -Wl,-soname,libcalc.so.1 -o libcalc.so.1.2.0 calc_v1.c -I. && ln -s libcalc.so.1.2.0 libcalc.so.1 && ln -s libcalc.so.1 libcalc.so
$ readelf -d libcalc.so.1.2.0 | grep SONAME
 0x000000000000000e (SONAME)             Library soname: [libcalc.so.1]
$ gcc app_v1.c -I. -L. -lcalc -o app_v1 && readelf -d app_v1 | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so.1]
```

链接名管的是链接期:-lcalc 摸到 libcalc.so、顺着软链读到真身,把它登记的正式名抄进 NEEDED。正式名管的是运行期:动态链接器从头到尾只认 libcalc.so.1。真实名就是文件本身——完整的版本都记在它身上,您在硬盘上 ls 到的也正是它。现在 v2 来了,它干的就是破坏现场里那件事:结构体加字段,ABI 变了。咱们正经的处理不走覆盖旧文件的路子,走的路是升一代:

```text
$ gcc -fPIC -shared -Wl,-soname,libcalc.so.2 -o libcalc.so.2.0.0 calc_v2.c -I. && ln -s libcalc.so.2.0.0 libcalc.so.2 && ln -sf libcalc.so.2 libcalc.so
$ gcc app_v2.c -I. -L. -lcalc -o app_v2 && readelf -d app_v2 | grep NEEDED
 0x0000000000000001 (NEEDED)             Shared library: [libcalc.so.2]
$ LD_LIBRARY_PATH=. ./app_v2 && LD_LIBRARY_PATH=. ./app_v1
v2 app: total=7200
v1 app: total=7500
```

两代库并排躺在了同一个目录里。咱们再让新程序来链接,链接名已经指向了第二代,NEEDED 记下的就是 libcalc.so.2。旧程序倒是全不知情,它要的 libcalc.so.1 还在——照旧打出 7500。版本升了旧程序怎么接着用,答案也就是它了:旧程序攥住的只是正式名,咱们只要不撤那根软链,它就一直是能跑的。

什么变更该动哪一段,咱们顺着机制念:动了 ABI、正式名升一段、旧一代原样保留。碰到只是修 bug、加新函数的改动,老约定没动、真实名的后两段往上走(1.2.0 到 1.2.1、1.3.0 都行),正式名不动——旧程序换上新文件就等于收下了修复。链接名永远指着最新的一代,它只伺候新程序的链接。破坏现场里那场换库的事故,放到这儿就有了正解:插了字段,该升的就是正式名——libcalc.so.1 原样不动,也就没了错位。

## 版本节点:一个库里住下两代接口

换文件分代的做法之外,还有更细的一档。11 里咱们用版本脚本收导出,当时的节点没起名,您要是给节点起了名,符号的尾巴上就会挂版本,那笔活今天由咱们补上。版本节点说的就是 map 文件里一对大括号包住的一块,咱们起两个名字:

```text
CALC_1.0 { global: line_total; local: *; };
CALC_2.0 { global: line_total; bulk_total; } CALC_1.0;
```

您看,2.0 节点把 1.0 认作了自己的父节点,新函数 bulk_total 只登在 2.0 的名下。同名的两代光靠脚本还不够,得请编译器的一道汇编指令 .symver 出面——由它把两个实现分别送到两个节点上:

```c
__asm__(".symver line_total_1,line_total@CALC_1.0");
__asm__(".symver line_total_2,line_total@@CALC_2.0");
int line_total_1(struct calc_item item) { /* 老算法 */ }
int line_total_2(struct calc_item item) { /* 2.0 起满 1000 减 50 */ }
```

单 @ 的意思是指名挂到某个节点,双 @(@@)额外声明自己是默认版本:不挑版本的客户端,链接器一律给它 @@ 的那个。咱们编出来看:

```text
$ gcc -fPIC -shared -Wl,-soname,libcalc.so.1 -Wl,--version-script=calc.map calc_v.c -I. -o libcalc.so && nm -D libcalc.so | grep -E 'line_total|bulk_total'
0000000000001126 T bulk_total@@CALC_2.0
00000000000010fc T line_total@@CALC_2.0
00000000000010e9 T line_total@CALC_1.0
```

一个库里住着两代的 line_total。老客户端想认的还是老版本,也有同款的路数:自己在源码里写一句一样的 .symver——把引用指到 @CALC_1.0 上,然后咱们让两个客户端链同一个库,各跑各的:

```text
$ LD_LIBRARY_PATH=. ./app_new && LD_LIBRARY_PATH=. ./app_old
new client: total=7450
old client: total=7500
```

readelf -V 把库里的版本定义树拍给咱们看,CALC_2.0 的父节点正是 CALC_1.0。两个可执行文件各自的版本需求段里(exe 中记录自己要哪一版接口的段、readelf -V 看得到),一个登记的只有 CALC_1.0,另一个登记的只有 CALC_2.0,运行期各取各的代。您系统里的 libc.so.6 就是这么干的,笔者数过,它的导出表里两千多个符号挂着 @@GLIBC_2.xx 的尾巴,几十年里加了又不能改的接口,全靠节点分代的办法接着用。不过您也看得出这活的分量:老客户端得在源码里写 .symver 指定版本,新客户端什么都不用写。所以日常的分代主力还是正式名升段,版本节点伺候的是同一个正式名底下接口只增不改的演进,glibc 是最有名的用户。

## COPY 重定位:链接期定下的副本

09 里咱们看过一幕:可执行文件直接读库里的变量 tax_rate,链接器在它的 .bss 里安排了一个副本——加载时由动态链接器拷进初值,库自己反而要回头引用 exe 里的副本。当时咱们留了一句,同一个变量在进程里有了两份记录,由此引出的问题归今天。现在咱们让它出事,标本就复活 09 的 rateapp。v1 的库导出 int tax_rate = 17,exe 咱们提前编好,跑起来会老老实实地打出 17,COPY 的重定位记录也拍了下来(09 里管它叫拷贝重定位、说的是同一个东西):

```text
$ readelf -rW rateapp | grep COPY
0000000000004018  0000000700000005 R_X86_64_COPY          0000000000004018 tax_rate + 0
```

麻烦的根子,已经写进上面的重定位记录里了:exe 侧副本的尺寸,是它链接的时候从 v1 库量来的 4 字节,量完就定死在了文件里。库侧后来把 tax_rate 换成了 double、尺寸翻倍到 8 字节,exe 咱们一个字节都不动、直接换库跑:

```text
$ gcc -fPIC -shared rate_v2.c -o libcalc.so && LD_LIBRARY_PATH=. ./rateapp
./rateapp: Symbol `tax_rate' has different size in shared object, consider re-linking
rate=1546188227
```

glibc 的动态链接器倒是看见了尺寸对不上,喊了一行 consider re-linking,喊完了照常拷贝、照常放行。拷进 4 字节槽里的,是 0.17 那个 double 的头四个字节(小端序的十六进制就是 c3 f5 28 5c),按 int 的读法一读,成了 1546188227,笔者跑了两次,分毫不差地错。这就是 COPY 重定位在 ABI 上的代价:变量的大小和类型,被可执行文件在链接期定死了,库侧再怎么动,exe 的副本就与真身错位。对策其实不玄:接口别导变量,要给就给个读数的函数,变量退回库的内部。真有旧代码躲不开、又得当场补救的,GCC 这边咱们验证过的做法,是把可执行文件也用 -fPIC 重编了一遍:实测下来,COPY 的记录归零,tax_rate 的取数换成了走 GOT 的 R_X86_64_GLOB_DAT,程序照常打出 17——副本压根就不落了。Clang 那头另有一个 -fno-direct-access-external-data 的开关,干的也是同一件事。

## 构造顺序:没人排班的那些全局对象

ABI 管函数和数据怎么对接,还有一类对象咱们得单说。全局对象是带着构造函数的,构造的时机在 main 之前。C++ 标准在这里留了空白:同一个翻译单元的内部,全局对象按定义的顺序构造,跨翻译单元的顺序——没有规定。咱们摆两个出来——logger 和 config,构造函数里互相打探对方起了没有:

```text
$ g++ main.cpp logger.cpp config.cpp -o app_a && ./app_a
logger ctor: config.ready=0
config ctor: logger.ready=1
$ g++ main.cpp config.cpp logger.cpp -o app_b && ./app_b
config ctor: logger.ready=0
logger ctor: config.ready=1
```

排在前面的早一步构造好,排在后面的看到的 ready 就是 0。您要是在构造函数里真用了对方——比如 config 拿 logger 打日志,logger 那边还是没构造的内存——轻则乱码、重则崩。这个顺序在实践中跟的是链接顺序,可它只是这套工具链的现状,不是标准给的保证,换套构建就可能换出别的排法。修法倒是老经典:把对象藏进函数里,行话的名字叫函数局部 static。对象写进了函数体,构造就要等控制流头一次走到了那一行才发生。咱们给两个对象各配一个访问函数,函数体里放上一个 static 的实例,头一次调用到它的时候才构造,往后交回的都是同一个。config 的构造函数里要 logger,调 logger() 就顺手把 Logger 构造好了再交回来。两种链接顺序咱们再跑一遍,输出的就是一个样:

```text
$ g++ main.cpp logger.cpp config.cpp -o app_a && ./app_a
logger ctor
config ctor: logger.ready=1
```

另一种链接顺序咱们也跑了,输出与上面的一字不差。顺序从链接顺序的手里,交到了调用关系的手里:谁依赖谁,排在前面构造的就是被依赖的一方。C++11 起这趟构造还自带了线程安全,并发首调也只有一个线程真的在构造。动态库里的全局对象是同一个修法,库里多个翻译单元之间的构造顺序,同样没人替您担保。至于加载的那一整套流程,14(程序的启动)会带着咱们整篇走一遍,咱们点到接口层就收。

## 定名:两套互不相认的编码,一个 extern "C"

三问到这儿交齐了卷,还剩一条 03 立下的旧线:名字。03 里咱们拿 c++filt 解过 _Z 打头的修饰名,也说过那套编码的出处是 Itanium C++ ABI,同时留了个问题:跨编译器、跨平台,用的还是不是同一套编码?答案今天有了:Linux 这边用的是一套,MSVC 那头又用着互不相认的另一套。咱们拿同一份源码在 Linux 这边编一遍:

```text
$ g++ -c test.cpp -o test_name.o && nm test_name.o
0000000000000021 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEESt17basic_string_viewIcS3_E
0000000000000012 T _ZN14charlies_tools5splitERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEc
0000000000000000 T _ZN3Foo8someFuncEiPKc
```

MSVC 那边的样本,笔者在 Windows 机器上对同一份代码跑过 dumpbin,符号长成了下面这样(编译器版本 19.44.35217,这几行是笔者当时的存档输出,咱们在这边的 Linux 上不重跑它):

```text
D:\DownloadFromInternet>dumpbin /SYMBOLS test.obj
00C 00000000 SECT4  notype ()    External     | ?someFunc@Foo@@QAEXHPBD@Z (public: void __thiscall Foo::someFunc(int,char const *))
00D 00000010 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@D@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,char))
00E 00000020 SECT4  notype ()    External     | ?split@charlies_tools@@YAXABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V?$basic_string_view@DU?$char_traits@D@std@@@3@@Z (void __cdecl charlies_tools::split(class std::basic_string<char,struct std::char_traits<char>,class std::allocator<char> > const &,class std::basic_string_view<char,struct std::char_traits<char> >))
```

打头的是 ?,@@ 管的是分段,与 _Z 的那套毫无亲缘。GCC 和 Clang 在 Linux 上用的都是 Itanium 这套,MSVC 倒是自成一家,两家的符号互不认识。所以 C++ 的库想跨编译器给人用,直接导出修饰名是走不通的,得换成两边都认的命名:C 的名字。11 里咱们已经用 `extern "C"` 导出过 refund,过了修饰这一关,符号就成了函数的本名,代价您也见过:它名下是没有重载的。Windows 那头还有几处与 ELF 不同的细节,10 里咱们对照过找库的顺序,剩下的今天一并交掉。导入库 .lib 一个后缀担两种身份的事,10 里咱们已经认过:静态库是它、给 DLL 配的导入库也是它。剩下的差别在运行期才露头:链接期认的都是 .lib,到了运行期,装载器才去请 DLL 的本尊。导出的写法是 __declspec(dllexport) 与 __declspec(dllimport),一个配给编库的场合,一个配给用库的场合,干的是 11 里全局收默认、单点放开那对机制的活。命名习惯也不同:DLL 的文件名通常不带版本号,mylib.dll 就算叫到了头,ELF 靠三段名分代的活,Windows 的习惯是交给安装布局去办,不同版本各住各的目录。

`extern "C"` 只定了名字,一份能跨工具链的头文件,还得把其余的也定住。咱们把要紧的东西压成四件套:函数声明、类型定义、宏常数、包含依赖。咱们拿 libcalc 改一版出来:

```c
/* calc_abi.h —— 对外的完整 ABI 头:四件套齐了才算完整 */
#include <stddef.h>                  /* 包含依赖:size_t 的来处 */
#define CALC_MAX_ITEMS 64            /* 宏常数:接口用到的数 */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct calc_item {           /* 类型定义:布局写死在头里 */
    int units;
    int price;
} calc_item;
typedef struct calc_pool calc_pool;  /* 不透明指针:只声明,不定义 */
int          calc_line_total(const calc_item* item);  /* 函数声明 */
calc_pool*   calc_pool_create(size_t n);
void         calc_pool_destroy(calc_pool* pool);
const char*  calc_build_id(void);
#ifdef __cplusplus
}
#endif
```

四件套的分量不是并列的,咱们分两档说。承重的一档是函数声明配类型定义。少了声明,调用方就不知道名字和类型了。少了类型定义,编译器就算不出 calc_item 的尺寸,参数在栈上、寄存器里的排布也就乱了,破坏现场那场事故的根子正是它。配套的一档是宏常数和包含依赖,缺了不会当场翻脸:两边的口径各写各的、头文件站不住,病都埋在了暗处。咱们拿 gcc 和 g++ 各编一个调用方,链的是同一个库(nm 的导出表只挑了两条):

```text
$ gcc -fPIC -shared calc_abi.c -I. -o libcalcabi.so && gcc main_c.c -I. -L. -lcalcabi -o app_c && g++ main_cpp.cpp -I. -L. -lcalcabi -o app_cpp
$ nm -D libcalcabi.so | grep calc_
0000000000001166 T calc_build_id
0000000000001133 T calc_pool_create
$ LD_LIBRARY_PATH=. ./app_c && LD_LIBRARY_PATH=. ./app_cpp
C caller:    total=7500 id=calc-abi-demo
C++ caller:  total=10000 pool=ok id=calc-abi-demo
```

两位调用方吃的是同一个库和同一份头,导出的名字全是裸名。您还该多看一眼头文件里的 calc_pool,它走的是不透明指针的路数。调用方的手里只有指针——拿着到处传,它却造不出、也拆不开一个 calc_pool 的实例。库里的布局怎么改都不牵连旧代码——因为它根本不碰布局。工厂函数在哪儿把对象造出来、造出来的东西怎么按名字领走,咱们留到 13(运行时加载与插件),那是运行期找人的正题。

## 哪些改动算 ABI 事件

现在咱们能把 11 里那句收得越少、改动越自由,落成具体的单子。动了旧二进制取数或排布的全算 ABI 事件。往结构体中间加字段——破坏现场已经验过了。改参数类型——咱们拿两副 ABI 也验过了。至于换返回类型、调字段顺序、改对齐的,咱们一样躲不掉、都得升正式名。不算事件的:修 bug、加新函数、给头文件补注释,真实名的后两段走一走就够。

虚函数的事咱们单拎出来说。给原本没有虚函数的类加头一个,对象的头部要插进一个虚表指针,虚表(vtable)说的就是虚函数表,咱们实测旧 exe 静默打出垃圾值。已经有虚函数、又没人继承的叶子类,在末尾追加新的虚函数,旧的虚表槽位一个不动,旧 exe 的结果照常正确,只是新的函数它够不着。KDE(Linux 桌面环境项目)的二进制兼容政策,给的正是这个口径。您回头看 11 的三招收窄,收的就是单子的长度:导出的名字越少、单子越短,您日后能动的地方就越多。配上前面的升级纪律,一个库的接口纪律也就齐了:把对外的面收窄、变量不导,结构体要么不动,要么动了就配着升代走,函数签名定下来了就不改,要改就加新的。

## CMake 视角

手敲 soname 链接选项、手养软链的活,CMake 用一对目标属性替咱们接管。头一个的名字是 VERSION,另一个的名字是 SOVERSION。咱们把标本摆成一个小工程:

```cmake
cmake_minimum_required(VERSION 3.20)
project(calc_abi C)
include(GenerateExportHeader)
add_library(calc SHARED calc_abi.c)
set_target_properties(calc PROPERTIES VERSION 1.2.0 SOVERSION 1)
generate_export_header(calc)
add_executable(app_c main_c.c)
target_link_libraries(app_c PRIVATE calc)
```

VERSION 记真实名的三段,SOVERSION 记正式名的头一段。咱们构建出来一验,三件套齐齐整整地躺在了 build 里,中间那根软链的名字正是 SOVERSION 的 1,库里的 SONAME 登记的也是它,app 裸跑就过(CMake 给构建树的 app 写了指向 build 的 rpath,10 里交代过它的来路):

```text
$ cmake -B build -S . && cmake --build build
$ ls -l build/libcalc.so*
lrwxrwxrwx 1 charliechen charliechen    12 Oct  1 14:46 build/libcalc.so -> libcalc.so.1
lrwxrwxrwx 1 charliechen charliechen    16 Oct  1 14:46 build/libcalc.so.1 -> libcalc.so.1.2.0
-rwxr-xr-x 1 charliechen charliechen 15264 Oct  1 14:46 build/libcalc.so.1.2.0
$ readelf -d build/libcalc.so.1.2.0 | grep SONAME
 0x000000000000000e (SONAME)             Library soname: [libcalc.so.1]
$ ./build/app_c
total=7500 id=calc-abi-cmake
```

generate_export_header 是 11 的 CMake 节里欠下的那块话题,今天咱们兑现——它在构建目录里生成一份 calc_export.h,咱们把要紧的几行请出来看:

```c
#    ifdef calc_EXPORTS
        /* We are building this library */
#      define CALC_EXPORT __attribute__((visibility("default")))
#    else
        /* We are using this library */
#      define CALC_EXPORT __attribute__((visibility("default")))
#    endif
```

calc_EXPORTS 的定义只在编库时由 CMake 给出,用库的场合就没有。两个分支在 Linux 上展开成的都是同一个 visibility 属性,因为 ELF 的世界里不分导入与导出。同一对的分支换到 Windows 上做 configure,展开的就换成 __declspec(dllexport) 与 __declspec(dllimport),一份宏就把两头都伺候了。11 里单点放开的那半手工活,它替咱们自动办了(全局收默认的那一半,11 的 CMake 节里目标属性已经管上了)。倒是有一件事它不管:`extern "C"` 的包裹,生成的头里一个字都不提,仍旧得您自己在头文件里写——前面 calc_abi.h 里那一对 #ifdef __cplusplus,占的就是这个位置。发布的场合再配一句 install(TARGETS calc ...),三件套进了 lib,头文件进了 include,分代的重活,前面手做的那遍,您已经全认识了。

## 接下来:运行时再找人的用法

走到了这里,接口的几件大事都有了交代:形靠的是调用约定和结构体布局,名靠的是 `extern "C"` 配一份完整的 ABI 头,版本靠的是三段名加版本节点。可它们都有一个共同点:依赖在链接期就写死了,NEEDED 里记的就是名字——程序一启动就得把库找齐。还有一种用法倒是不走这一步——程序跑起来之后再按需打开一个库文件、按名字把函数取出来用,插件的活法就是这么来的。那套运行期找人的机制,正是 13(运行时加载与插件)的正题,咱们把不透明指针的工厂,也留在那儿等您。
