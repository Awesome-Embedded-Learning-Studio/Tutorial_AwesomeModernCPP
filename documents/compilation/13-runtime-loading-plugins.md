---
chapter: 13
difficulty: intermediate
order: 13
platform: host
reading_time_minutes: 18
tags:
- cpp-modern
- host
- intermediate
- 工具链
- 链接器
title: "运行时加载与插件:程序自己找库、取符号、再卸载"
description: "程序自己找库、取符号、再卸载,这是程序跑起来之后才有的第三种库用法。dlopen 四件函数的最小例,清旧错、取符号、末了复查的三步,void* 转函数指针在 -Wpedantic 下的真编译对照,找不到的三种形态逐字节对拍,RTLD_NOW 当场拒绝与 RTLD_LAZY 调用才崩,链接期 -z,defs 与 dlopen 立场对照,RTLD_GLOBAL/LOCAL 与 LAZY/NOW 四格矩阵,插件工厂 extern C 入口配不透明指针加函数表,一个宿主两个后端换 so 不重编,卸载的引用计数与悬垂函数指针,RTLD_NEXT 包装 malloc 接上位置无关那篇的预载工具,-ldl 自 glibc 2.34 并入 libc 实测,Windows 三件机制对照,CMake 的 MODULE 与 PREFIX 空串。"
cpp_standard: [11, 14, 17, 20]
---

# 运行时加载与插件:程序自己找库、取符号、再卸载

咱们这一卷讲到现在,库的用法其实只摆过两种:静态库走的是静态库那篇的路子,链接期把成员整个抽进了可执行文件。动态库走的是动态库总览那篇往后的路子,-lcalc 把名字登进了 NEEDED,运行期再由动态链接器照着 NEEDED 的单子拉起。两种用法的共同点也在这里:依赖在链接的那一刻就写死了,程序这一辈子认下的每个依赖,都是链接命令定下的。今天咱们开第三种:程序已经跑起来了,它自己把一个库装进了进程,自己按名字把要用的函数取出来,用完了再自己卸掉。干这几件活的函数,POSIX 给的是 dlopen、dlsym、dlclose 加 dlerror,一个 <dlfcn.h> 头文件全包了,咱们待会儿挨个上手。

符号可见性那篇讲导出名字的那部分,末尾咱们留过话:导出表收干净之后,跨工具链的接口怎么定名字,dlsym 按名取函数的活怎么干,还得回到咱们这一篇来接着讲。定名字的办法,上一篇已经把 ABI 头的四件套立成了标准做法:函数声明、类型定义、宏常数、包含依赖,今天咱们把它落到一个真用场上:插件。它还有另一重身份:链接期躲过去的未定义符号,换到运行期加载的路上会以什么形态爆出来,咱们今天一并看真输出。咱们用的标本还是 libcalc,算法跟旧的一模一样,100 的底税加 17 的税率,封顶取整出的还是 120,后端却从链死的库变成了运行时装进来的 .so。笔者的实验都在 WSL2 里跑,全套的工具链是 GCC 16.2.1、binutils 2.47、glibc 2.44 与 CMake 4.4.3,这一篇里的每一段输出都是真实跑出来的。

## 最小用法:开库、取符号、收尾卸库

```cpp
// four.cpp 节选 —— dlopen 四件的最小用法,错误分支从略
void* handle = dlopen("./libcalc.so", RTLD_NOW);    // 开库,换回句柄;RTLD_NOW 是 flags 的一档,后面细讲
dlerror();                                          // 清掉旧的错误状态
int (*total_tax)(void) = (int (*)(void)) dlsym(handle, "total_tax");
const char* err = dlerror();                        // 取指针,再查一次
std::printf("base=100 total=%d\n", total_tax());    // 当普通函数用
dlclose(handle);                                    // 还句柄
```

```text
$ gcc -fPIC -shared libcalc.c -o libcalc.so
$ g++ -std=c++17 four.cpp -o four
$ ./four; echo $?
base=100 total=120
exit=0
```

四个函数咱们挨个认一遍。dlopen 收的是一个路径加一组 flags,把库装进了进程之后交回一个 void*,行话里的称呼是句柄:后面取符号、卸库,出示的全是它。dlsym 收的是句柄加一个字符串名字,把符号的地址找出来交还。dlclose 把手里的句柄还回去,装载的引用计数减一,减到了零才真卸。dlerror 的脾气最特殊:参数一个都不收,专门交出最近一次 dl 系列调用的错误文本,而且读一次就清了零,所以咱们拿到就得存进变量里。取符号的那三行写得绕,您可能已经注意到了。dlerror() 的空跑是为了清掉旧的错误状态,dlsym 干的才是取指针的正事,末了的 dlerror 负责复查。绕的根子在 dlsym 的失败信号不够用:它失败了返回 NULL,可一个符号的地址真可能是 NULL,比如一个值恰好为零的导出变量,光看返回值咱们分不出两种情况谁是谁。还有一个小机关值得咱们点名:dlerror 读一次就清零,连着读两次会把错误读丢了,笔者的宿主第一版就在这里打印过 (null)。

还有一处您心里可能犯嘀咕:dlsym 还回来的是 void*,也就是一个货真价实的对象指针,咱们硬把它转成了函数指针。ISO C++ 对这样的转换没有保证,C 标准干脆下了禁令,POSIX 却给了承诺:从 dlsym 得来的 void*,是可以转成函数指针用的。编译器站在哪一边?咱们把同一份源码存成 .c 与 .cpp 各编一次,-Wpedantic、-Wall、-Wextra 全都开到了位:

```text
$ g++ -std=c++17 -Wpedantic -Wall -Wextra -c ped_cast.cpp -o /dev/null; echo $?
0
$ gcc -std=c17 -Wpedantic -Wall -Wextra -c ped_cast.c -o /dev/null; echo $?
ped_cast.c: In function 'main':
ped_cast.c:5:30: warning: ISO C forbids conversion of object pointer to function pointer type [-Wpedantic]
    5 |     int (*total_tax)(void) = (int (*)(void)) dlsym(handle, "total_tax");
      |                              ^
0
```

同一个转换的待遇分了岔:C 那边挨了警告,C++ 这边的反应是一片安静。差别的根子写在标准的措辞里:C 把它定成了不合规,所以编译器必须给出诊断。C++ 把它归为了条件性支持,实现了就顺理成章地合法,出声与否倒是不强制。真要一字不漏地追求可移植,拿 memcpy 把地址拷进函数指针,两边的标准都点头。日常咱们的代码跑在 Linux 与 macOS 上,直接转了就好,背后有 POSIX 给咱们撑腰。

## 找不到的三种形态

这一套的报错长什么样,咱们主动找三回麻烦。链接期的 undefined reference 您在链接器那篇就认识了,换到运行期加载的路上,找不到的东西会换上三种形态。

```text
$ ./miss 1
handle=(nil)
dlerror: ./libnope.so: cannot open shared object file: No such file or directory
$ ./miss 2
sym=(nil)
dlerror: ./libcalc.so: undefined symbol: base_taxx
```

头一场找的麻烦是库文件本身不存在,dlopen 交回了 NULL,dlerror 的文本您一眼就能读懂,报错的抬头正是咱们传进去的路径串。两场都是被程序接住的失败,退出码也是清一色的 0。这里咱们把一个流传很广的写法纠正掉:老教程常让您在运行以前 export LD_LIBRARY_PATH,再去 dlopen 一个带斜杠的全路径,这两件事其实是凑不到一起的。名字里带了斜杠,dlopen 就当成了普通路径直接打开,为搜索准备的那一整套机制根本不会发生,环境变量的帮忙也就无从谈起。名字里不带斜杠的时候才轮到搜索出场,从环境变量到系统缓存的那一串档位是库搜索那篇讲过的,咱们不在这儿展开。第二场的麻烦出在名字上:库还好好地待着,名字却取错了,咱们故意把 base_tax 多打了一个 x,dlsym 交回了 NULL,报错的抬头跟着手里握名字的一方走,这跟链接期 undefined reference 的抬头是目标文件,讲的是同一个道理。第三场最有意思:库自己引用了一个谁都没定义的函数。咱们写一个 gap.c,导出的 gap_call 调用了 missing_thing,全工程里没人给出它的定义,链接的时候咱们也不加管束:

```text
$ gcc -shared -fPIC gap.c -o libgap.so
$ ./gap_host N; echo $?
dlopen NULL, dlerror: ./libgap.so: undefined symbol: missing_thing
exit=1
$ ./gap_host L; echo $?
./gap_host: symbol lookup error: ./libgap.so: undefined symbol: missing_thing
exit=127
$ stdbuf -oL ./gap_host L; echo $?
dlopen ok
./gap_host: symbol lookup error: ./libgap.so: undefined symbol: missing_thing
exit=127
```

同一个坏库的两遍跑,换上不同的 flags,下场就完全不同了。RTLD_NOW 让 dlopen 把该解析的符号当场查完:缺一个就整体拒绝,交回的是 NULL,错误文本明明白白地摆在眼前。RTLD_LAZY 的脾气宽容得多,函数符号的解析推迟到头一次调用才做,于是 dlopen 过了关,崩在了调用那一行,进程的退出码是 127。您再留意 stdbuf 那一遍的小细节:默认管道下连 dlopen ok 都看不见,它其实执行了,只是 stdout 的块缓冲还没冲刷,进程就死了,换行缓冲一开才现了形。同一个缺陷的处理上,咱们前后见识了三道立场,管的各是一段。链接器默认对 .so 里的未定义符号宽容,因为合着别的库一起解析是常态,符号可见性那篇里露过面的 `-Wl,-z,defs` 是您主动拧紧的开关,咱们给 gap.c 加上它再链,当场报了 undefined reference,主角换成了 missing_thing。dlopen 的 RTLD_NOW 是装进进程那一刻的最后防线,写插件的工程习惯也就有了着落:插件一律用 RTLD_NOW 装载,缺符号的报错就落在 dlopen 那一行,别拖到用户点到某个功能的时候才崩。

## 两根 flags 轴:GLOBAL 管给谁,LAZY 管何时

dlopen 的第二个参数是 flags,一根轴挑的是解析的时机,就是刚见过的 LAZY 与 NOW,另一根轴挑的是符号给谁用,手里可选的就是 RTLD_GLOBAL 与 RTLD_LOCAL。咱们拿两个会互相配合的插件当标本,A 导出了一个 plug_a_value,B 引用了它。宿主的用法是 ./matrix 加两串字母,头一串挑 A 的 flags,末一串挑 B 的:字母 G 代表的是 GLOBAL、L 懒、N 急,不写 G 的时候就是 LOCAL,四格咱们全跑了:

```text
$ ./matrix LN N; echo $?
B dlopen NULL: ./libplugb.so: undefined symbol: plug_a_value
exit=1
$ ./matrix GN N; echo $?
sum=111
exit=0
$ ./matrix LL L; echo $?
./matrix: symbol lookup error: ./libplugb.so: undefined symbol: plug_a_value
exit=127
$ ./matrix GL N; echo $?
sum=111
exit=0
```

两根轴干的事,四格输出把两根轴的关系摆得清清楚楚。GLOBAL 与 LOCAL 决定的是 B 解析符号时看不看得见 A:LOCAL 是缺省档,库的导出不进进程的全局符号池,B 找 plug_a_value 扑了空。GLOBAL 把 A 挂进了池子,B 的引用就有了着落,111 就是 100 加 11 算出来的。那个池子您其实早认识了,就是符号可见性那篇讲查找顺序时说过的单子:可执行文件排头、预载库跟进、NEEDED 依序排开,GLOBAL 装载的库也排进队伍一起参与。LAZY 与 NOW 在这里的作用,跟上一节形态三那一场是一致的:配上 LOCAL 的时候,NOW 在 dlopen 的当场就报,LAZY 拖到调用的时候,才会以 symbol lookup error 收了场。还有半句咱们得点明,四格的输出里有一个容易读歪的地方:GLOBAL 把符号给到位之后,LAZY 与 NOW 拿到的结果是一样的,GL 那一行的 111 不是 NOW 的功劳,靠的是 A 挂进了全局池,解析的时机在这里没有改变结果。什么时候用 GLOBAL?一组分工明确、互相引用的插件就是候选,比如好几个插件都依赖同一个公共件的场合,装公共件的时候挂 GLOBAL。插件之间的隔离才是常态,所以 LOCAL 才被定成了缺省,咱们拿不准就不写 G。

## 插件工厂:一张函数表,一个不透明指针

拿着函数指针一个个地 dlsym,两个接口的时候还凑合,十几个就是自讨苦吃了:名字靠字符串,类型靠的是手转,错了一处要等运行期才知道。工程上的标准做法,就是把上一篇立好的 ABI 头办法落地。宿主与插件共享的就是一份四件套齐全的头文件,咱们把 libcalc 的后端改造成插件,头文件长的是这个样子:

```cpp
// calc_plugin.h 节选 —— 宿主与插件共享的唯一一份 ABI 头
#define CALC_ABI_VERSION 1                       // 宏常数:接口代次号

struct CalcBackend;                              // 类型定义:不透明指针,只有声明

struct CalcPluginApi {                           // 函数表:宿主只认这张表
    int abi_version;
    CalcBackend* (*create)(void);
    void         (*destroy)(CalcBackend*);
    int (*base_tax)(const CalcBackend*), (*tax_rate)(const CalcBackend*);   // 取值的一对,同形
    int          (*total_tax)(const CalcBackend*);
};
const CalcPluginApi* calc_plugin_entry(void);    // 函数声明:入口,extern "C" 包住
```

咱们从头往下读。CALC_ABI_VERSION 的身份是宏常数,宿主与插件各自编译时拿到的都是同一个数,对不上的时候就拒绝合作。CalcBackend 只有声明而没有定义,这样的指针行话叫不透明指针:宿主知道有这么个东西,却摸不到它的内部:连 sizeof 都写不出来,能做的只是把它原样递回去。中间的 CalcPluginApi 是一张函数表,字段装的全是函数指针:create 造的是后端、destroy 收的也是后端,剩下的三个就是咱们算了一整卷的数。真正的导出函数只有一个 calc_plugin_entry,声明照上一篇的手法用 `extern "C"` 与 `#ifdef __cplusplus` 包住,C 与 C++ 的两端都能包含它。那篇里咱们讲过,修饰名是认编译器的,跨模块按名取函数的时候,名字必须是裸的。类型倒是不吃修饰,要过边界的只是指针的值,所以函数表里尽管装 C++ 的函数。

```cpp
// plugin_standard.cpp 节选 —— 标准后端,CalcBackend 的定义只住在这一侧
struct CalcBackend { int base; int rate; };      // 补全头文件里那个不透明声明
static CalcBackend* create_impl() { return new CalcBackend{100, 17}; }
static int total_tax_impl(const CalcBackend* self) {   // destroy/base_tax 同形,从略
    int amount = self->base + self->rate; if (amount > 500) amount = 500;   // clamp_cap,照符号可见性那篇的算法
    return (amount + 9) / 10 * 10;               // near_ceil
}
static const CalcPluginApi kApi = { CALC_ABI_VERSION, create_impl, /* 其余同形 */ };
extern "C" const CalcPluginApi* calc_plugin_entry(void) { return &kApi; }
```

插件这一侧把 CalcBackend 的定义补齐了,成员装的就是底税与税率,取整封顶的算法搬进了 total_tax_impl,数字跟原来的一分不差。有一个笔者真翻过的车得记下:struct 别放进匿名 namespace,那会造出一个只在匿名空间里可见的新类型,跟头文件里的全局 CalcBackend 对不上号,函数指针的类型立刻合不拢,编译器当场报了 invalid conversion,拿 static 修饰函数就够用了。宿主这一侧咱们写得干净:整份代码只包含 calc_plugin.h 一个头,链接的对象里一个插件都没有。取入口走的还是清旧错、取符号、末了复查的三步,拿到的表也要对一次代次:

```cpp
// host.cpp 节选 —— 宿主:只包含 ABI 头,不链接任何插件
const CalcPluginApi* api = entry();              // entry 来自 dlsym,查错同 four.cpp
if (api->abi_version != CALC_ABI_VERSION) {      // 代次对不上,当场拒绝
    std::fprintf(stderr, "ABI 版本不符: 要 %d, 来 %d\n", CALC_ABI_VERSION, api->abi_version);
    return 1;
}
CalcBackend* backend = api->create();
std::printf("base=%d total=%d\n", api->base_tax(backend), api->total_tax(backend));
api->destroy(backend); dlclose(handle);          // 收尾:还对象,还句柄
```

```text
$ g++ -std=c++17 host.cpp -o host
$ ./host ./standard.so; echo $?
base=100 total=120
exit=0
$ ./host ./discount.so; echo $?
base=80 total=90
exit=0
```

两条同形的 -fPIC -shared 命令编出了 standard.so 与 discount.so,plugin_discount 跟标准版是一个模子里出来的,只是后端的两个数换成了 80 与 9,咱们不重贴了。要看的都跑了出来:同一个 host 二进制咱们没重编过一个字节,换一个 .so 就换了一副算法,base=100 total=120 稳稳接上了前面的基线。宿主的 NEEDED 里连一个插件的名字都没有,依赖变成了运行时的一条路径参数,插件的能力,说到底也就落在这里了。入口要是忘了挂 `extern "C"` 呢?咱们真做了一版没有它的,拿符号可见性那篇里认识的 nm -D 看一眼导出表、再让宿主按名去取:

```text
$ nm -D mangled.so | grep entry
00000000000011f2 T _Z17calc_plugin_entryv
$ ./host ./mangled.so; echo $?
dlsym: ./mangled.so: undefined symbol: calc_plugin_entry
exit=1
```

_Z17calc_plugin_entryv 是它的新名字,编译器那篇教过解码的修饰名又露面了。dlsym 拿的是裸名字,自然扑了空。导出表里的它看得见、按名字却取不着,符号可见性那篇讲导出名字的那部分留的话,到这儿兑现了。还有一层关系咱们得点明:CalcBackend 是插件里的 new 造出来的,凭什么叫宿主去收?其实并没有叫,destroy 也是插件里的函数,宿主只是把指针原样递了回去。该记下的就一条:对象出生在了哪个模块,收尾的权力就归哪个模块,配好对的 create 与 destroy,两边各管各的。背后有实在的机制:同一进程里的两个模块未必共享同一套 operator new,比如插件静态链了自己的运行库,跨过去的 delete 就是未定义行为。不透明指针把模块间的边界守得严严实实,宿主连完整的类型都没有,想越界都无从下笔了。上一篇留下的工厂用法,到这儿就齐了。

## 卸载:引用计数,悬垂的函数指针

插件的好处就好在能卸,可卸载的机制细节,值得咱们亲眼看两回。头一回看引用计数:dlopen 对同一个路径的第二次调用不再真装载,只把计数加了一,dlclose 相应地只减一,减到了零才真卸。咱们给插件配了一个会打印装卸的全局对象,咱们让宿主 dlopen 两回、dlclose 两回,中间的空档还要接着用:

```text
$ ./refcount; echo $?
[trace] 装进进程
h1=0x5e1672763040 h2=0x5e1672763040 同一?yes
第一次 dlclose 返回 0
dlclose 之后还能用: total=120
[trace] 卸出进程
第二次 dlclose 返回 0
main 收尾
exit=0
```

句柄的地址每次运行都不同,咱们留着本次的原样。证据全在顺序里:装进进程只打印了一回,两次 dlopen 拿回的是同一个句柄。第一次 dlclose 的结果只是减计数:total_tax 照样算出了 120,库还好好地活着。卸出进程的打印夹在第二次 dlclose 与 main 收尾之间,真卸发生在了第二回。这套计数的思路跟 shared_ptr 是一路的,拿了几个句柄,卸载的时候就得还上几个。第二回看反面:卸干净了,缓存下来的函数表还在手里,咱们偏要去调它:

```text
$ ./dangling; echo $?
[trace] 装进进程
[trace] 卸出进程
已 dlclose,再调 base_tax
exit=139
```

咱们连跑五回,五次拿到的都是 139,它的来历就是 128 加 11,内核报 SIGSEGV 的老编码。dlclose 减到了零,库的映射整个撤了下去:函数表、后端对象、那片代码全没了,咱们手里的指针全成了悬垂。所以插件系统的纪律就一条:dlclose 之后,从这个库拿到的所有指针一律作废,函数表、对象、静态资源里递出来的字符串,一个都不能再碰了。真要常驻的插件,不少工程的做法是干脆不调 dlclose,装载的状态一直留到进程退出,把回收的整件事交给操作系统,反而省了心。

## RTLD_NEXT:把 malloc 包一层

位置无关那篇里咱们用 LD_PRELOAD 把 base_tax 整个顶掉过,当时还留了话:内存剖析一类的工具,不少就是预载自己的 malloc 与 free 起家的。顶掉其实容易,可包装工具要的不止是顶掉,是包一层:统计完了,还得把活递给真的 malloc。递给谁?靠的是 RTLD_NEXT 这个特殊的伪句柄:它让 dlsym 从搜索顺序里位于当前库之后的模块接着找,取的是下一个同名定义。符号可见性那篇的单子上,预载的 mwrap 排在 libc 前面,从 mwrap 的位置往后数,下一家恰好是真的 malloc,转发就接上了。咱们把关键几行摆出来:

```c
// mwrap.c 节选 —— 预载层:统计 malloc/free,其余转发给下一家
static void* (*real_malloc)(size_t);
static char   boot_pool[1 << 16];          /* 解析期间的垫背:静态内存 */
static int    resolving = 0;
static void resolve(void) {
    resolving = 1; real_malloc = (void* (*)(size_t)) dlsym(RTLD_NEXT, "malloc"); resolving = 0;
}
void* malloc(size_t n) {
    if (real_malloc == 0) { if (resolving) return boot_alloc(n); resolve(); }   /* 回环防御,正文细说 */
    return real_malloc(n);                 /* 计数后原样转发 */
}
```

咱们让报告在析构里走 write 系统调用、绕开 printf,免得输出的动作本身又进了 malloc。咱们把它跑起来:

```text
$ gcc -shared -fPIC mwrap.c -o mwrap.so
$ LD_PRELOAD=$PWD/mwrap.so ./host ./standard.so; echo $?
mwrap: malloc 过 80766 字节, free 调用 11 次
base=100 total=120
exit=0
```

宿主照常算出了 120,统计也拿到了:这个进程前前后后过了 80766 字节的 malloc,连 libstdc++ 起家的分配都数了进去。boot_pool 那一小块的来历,咱们得讲点历史:glibc 2.34 以前,dlsym 住在独立的 libdl 里,它解析符号时的内部分配会再进预载层的 malloc,而真函数指针还没到手,就原地打转了,垫一块静态内存才算解了围。今天的 glibc 2.44 上呢?咱们拿计数探针量过:解析期间预载层的 malloc 一次都没被叫到,把垫背的分支整个删掉再跑,跑出来的 80766 一字不差,因为 libc 内部的分配走的是内部别名、不经过 PLT,预载层的拦截根本无从发生。那这道防御还值不值得写?值得:垫背的手法不挑实现,换一个仍走 PLT 的平台或链接布局,它就是防回环的那道闸。写介入工具的经典一关就在这儿,您日后翻 ASan(AddressSanitizer,内存错误检查工具)与 heaptrack(堆内存剖析工具)的源码,一进门就能碰上它的身影。

## -ldl 的今昔

老教程的构建命令里,链接的那一步总缀着一个 -ldl,咱们这一篇从头到尾没写过它,写出来的程序照样个个链得动。今天的 glibc 里 dl 系列住在哪儿?咱们直接查 libc 的导出表:

```text
$ nm -D /usr/lib/libc.so.6 | grep -E ' T (dlopen|dlsym|dlclose|dlerror)@@'
0000000000093c90 T dlclose@@GLIBC_2.34
0000000000093cd0 T dlerror@@GLIBC_2.34
0000000000094520 T dlopen@@GLIBC_2.34
00000000000945e0 T dlsym@@GLIBC_2.34
```

```text
$ ls -l /usr/lib/libdl.so 2>&1; echo $?
ls: cannot access '/usr/lib/libdl.so': No such file or directory
2
$ nm /usr/lib/libdl.a; echo $?
0
$ readelf -d /usr/lib/libdl.so.2 | grep -E 'SONAME|NEEDED'
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x000000000000000e (SONAME)             Library soname: [libdl.so.2]
```

四个名字后面跟着的版本号说明了来路:glibc 2.34 起,原来单过的 libdl 并进了 libc,咱们这套 2.44,合并已经过去好几年了。开发那一侧的残迹,咱们也都看了:libdl.so 这个开发侧的链接名连文件都不在了,libdl.a 还剩下的是一个空档案:nm 在里面读不出任何符号。libdl.so.2 只是给旧二进制的 NEEDED 留的兼容壳,它自己依赖的只有 libc。所以今天的 -ldl 写不写都行:写了,链到的是那个空档案,咱们实测加了 -ldl 的程序,NEEDED 的清单与不加时一字不差。老教程让咱们写,也不是白写的:2.34 以前 libdl.so 是个真库,dlopen 的实现就住在里面,缺了它的时候,链接器会还您一句 undefined reference,主角就是那个取不到的 `dlopen'。老教程其实没有写错,它们面对的是当时的 glibc。今天这路依赖并进了 libc,写与不写的产物一个样。

## Windows 那边的对应

这一套在 Windows 上有整整齐齐的对应:LoadLibraryA 与 LoadLibraryW 对 dlopen、GetProcAddress 对 dlsym、FreeLibrary 对 dlclose,句柄的类型叫 HMODULE。错误查询要走的是两步:第一步的 GetLastError 拿错误码,第二步的 FormatMessage 把码翻成文本,没有咱们这边 dlerror 一步到位的省心。排查导出名的时候,dumpbin 的 /EXPORTS 干的就是 nm -D 的活,看的就是一个函数在不在导出表里。还有一个 32 位时代留下的名堂:__stdcall 调用约定会改导出名,装饰成形如 _name@12 带参数字节数的样子,GetProcAddress 按裸的名字去取就取不到,声明处的约定对不上也会这样。搜索的那一头,装载器头一个搜的是 EXE 自己的目录,库搜索那篇对照过的两边相反的档位,咱们今天就不重讲了。

## CMake 视角

手敲 -fPIC -shared 的插件构建,日常的活儿交给 CMake 来管。插件在 CMake 里有一个专门的库类型 MODULE:产出的同样是 .so,语义却是给 dlopen 用的,不是给链接用的。咱们把工程摆出来:

```cmake
# CMakeLists.txt 节选
add_library(standard MODULE plugin_standard.cpp)
set_target_properties(standard PROPERTIES PREFIX "")   # discount 同形
add_executable(host host.cpp)
```

```text
$ cmake -B build -S . && cmake --build build
$ ./build/host ./build/standard.so
base=100 total=120
$ ./build/host ./build/discount.so
base=80 total=90
```

MODULE 与 SHARED 的分别,CMake 自己会把关:您要是把 MODULE 目标写进 target_link_libraries,configure 当场就拒绝了,咱们真试了一把:

```text
CMake Error at CMakeLists.txt:8 (target_link_libraries):
  Target "standard" of type MODULE_LIBRARY may not be linked into another
  target.  One may link only to INTERFACE, OBJECT, STATIC or SHARED
  libraries, or to executables with the ENABLE_EXPORTS property set.
```

报错把话说全了,MODULE 的定位就是不被链接。这道把关跟咱们的语义正好咬合:宿主压根不该链插件,链上了,依赖又写死了,前面的活就白干了。PREFIX 的空串管产物名:插件的名字是给人和给配置文件看的,lib 的前缀纯属多余,去掉了它,standard.so 也就干净利落了。咱们翻过 --verbose 的编译行,MODULE 自动带上了 -fPIC,跟 SHARED 拿的是同一个待遇,一个字都不用您操心。运行期找插件的那点事,BUILD_RPATH 的机制,库搜索那篇的 CMake 节讲过,咱们的插件路径既然走参数,咱们通常也用不上它。导出的头要是想跨 Linux 与 Windows 两头使,CMake 的 generate_export_header 能生成两边通吃的头,上一篇的 CMake 节真跑过,咱们不重演。

## 接下来:进程自己是怎么起来的

咱们这一篇干的活,全是进程活着的时候自己动的手:dlopen 自己找库、dlsym 自己取符号,装载的时机攥在您代码的手里。可进程自己是怎么从一句 ./host 走到 main 的?execve 一类的系统调用负责把磁盘上的程序换成新进程,换完把控制权交给可执行文件头部登记的动态链接器,它照着 NEEDED 的单子把整棵依赖树拉起来,重定位做完了才轮到 _start 初始化运行库,最后执行到的才是 main。开卷那篇远远望过一眼的 interpreter,动态库总览那篇逐行读过的 NEEDED,位置无关那篇给 GOT 填表的动态链接器,都是这套流程里的角色。整条路怎么走?下一篇整篇讲的就是它,咱们下一篇见。
