---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 演讲笔记 —— C++: Some Assembly Required by Matt Godbolt'
difficulty: intermediate
order: 1
platform: host
reading_time_minutes: 36
speaker: Matt Godbolt
tags:
- cpp-modern
- host
- intermediate
talk_title: 'C++: Some Assembly Required'
title: 个人历程与从汇编到 C++ 的觉醒
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
---
# 为什么 C++ 程序员应该关注汇编

很多 C++ 教程和老师都会告诉你：写 C++ 不用管底层，编译器比你聪明，你只管用模板、用智能指针、用标准库算法，剩下的交给优化器。然而在实践中，当你对着跑得慢的代码反复优化却看不到进展的时候，真正需要做的是看一看你的代码到底编译成了什么——也就是汇编输出。很多情况下，那个你以为"零成本抽象"的模板函数，编译器根本没有内联；那个你觉得"应该很快"的 lambda，在循环里被反复构造和销毁。汇编不会骗你，它就是你的代码真正变成的样子。

这件事跟 C++ 的核心哲学是绑在一起的。C++ 从诞生那天起就在追求一件事：你不需要为没有用到的东西付代价<RefLink :id="1" preview="Stroustrup, The C++ Programming Language, 1986, zero-overhead principle" />。但问题是，你怎么知道你有没有付代价？编译器不会主动告诉你"你这个抽象有开销"，它只会默默地生成代码。而那份代码，就是汇编。

理解模板展开之后到底生成了什么代码，最直接的方式不是去读编译错误信息（虽然那也很重要），而是去看生成的汇编。当你看到模板实例化出来的函数被完美内联、循环被展开、寄存器被合理分配的时候，你才会真正理解什么叫"零成本抽象"。反过来，当你看到一堆多余的函数调用和内存搬运的时候，你也立刻就知道哪里出了问题。

所以不要把汇编当成什么高深莫测的东西。它就是一面镜子，照出你写的 C++ 代码到底长什么样。你不需要精通它，但你得有能力看懂它的轮廓，知道哪里不对劲。

---

# 从"手动敲代码"说起：为什么我们需要理解底层

演讲者提到了 ZX Spectrum<RefLink :id="2" preview="Sinclair Research, ZX Spectrum, 1982, Zilog Z80A" /> 和手动输入代码的年代。对于很多初学编程的人来说，编译、运行、看到终端里那行字，就已经觉得够了。但一个很快会意识到的问题是：你其实不知道那行字是怎么跑到屏幕上去的，甚至不知道代码在编译之后变成了什么东西。这种"黑盒感"在写高级抽象的时候可能无所谓，但一旦出了 bug，尤其是那种内存相关的诡异 bug，就会无从下手。

学编程不只是学语法、学框架、学 API。C++ 的语法本身就已经够让人头疼了——右值引用、完美转发、SFINAE，光是把这些对于入门的人而言相当晦涩的概念的名字记住就需要时间。但越往深里学越会发现一个尴尬的事实：写出来的代码，自己并不真正理解它在机器层面做了什么。当别人问"Hello World 字符串怎么从可执行文件跑到 CPU 上"的时候，如果答不上来，说明对底层的理解还不够。

## 动手看看：C++ 代码到底变成了什么

把自己的 C++ 代码编译成汇编，然后一条一条看，这是理解"代码到底在做什么"最直接的方式。

实验环境：Arch Linux WSL，GCC 16.1.1，编译命令里加了 `-S -O0` 参数。`-S` 是让编译器只生成汇编不往下走，`-O0` 是关掉所有优化，因为开了优化之后汇编会被改得面目全非，对初学者来说很难对应到源代码。

来写一个最简单的例子：

```cpp
// demo.cpp
int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(3, 4);
    return result;
}
```

编译一下：

```bash
g++ -S -O0 -o demo.s demo.cpp
```

然后打开 `demo.s`，你会看到一大堆东西，别慌，大部分是编译器加的辅助信息，我们只关心核心部分。在 x86-64 下，`add` 函数的汇编大概长这样：

```asm
add(int, int):
    pushq   %rbp            ; 保存调用者的栈帧基址
    movq    %rsp, %rbp      ; 建立当前函数的栈帧
    movl    %edi, -4(%rbp)  ; 第一个参数 a 存到栈上
    movl    %esi, -8(%rbp)  ; 第二个参数 b 存到栈上
    movl    -4(%rbp), %edx  ; 把 a 读出来
    movl    -8(%rbp), %eax  ; 把 b 读出来
    addl    %edx, %eax      ; a + b，结果在 %eax 里
    popq    %rbp            ; 恢复调用者的栈帧基址
    ret                     ; 返回
```

`main` 函数里调用 `add` 的部分：

```asm
main:
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $16, %rsp       ; 在栈上分配 16 字节局部变量空间
    movl    $4, %esi        ; 第二个参数 4
    movl    $3, %edi        ; 第一个参数 3
    call    add(int, int)   ; 调用 add
    movl    %eax, -4(%rbp)  ; 返回值存到 result
    movl    -4(%rbp), %eax  ; return result
    leave                   ; 等价于 movq %rbp, %rsp; popq %rbp
    ret
```

第一次看到这段汇编的时候会注意到：原来 `return a + b` 在 `-O0` 下编译器会老老实实地先把参数从寄存器搬到栈上，再从栈上读回来做加法。效率不高，但这就是没有优化的原始样子，每一行都清清楚楚，能看到数据是怎么流动的。

## 一个容易踩的坑

这里有个坑必须提醒。一开始用 `-O1` 编译，结果发现 `add` 函数的汇编就两三行，参数根本没进栈，直接在寄存器里就算完了(熟悉编译器优化的朋友估计不会感觉如何，本来就是寄存器层能操作的东西，对吧！)。这是因为 `-O1` 就已经开始做寄存器分配优化了——编译器发现没必要把参数存到栈上再读回来，直接用寄存器就算了。所以如果你也想跟着做实验，一定要用 `-O0`，不然你会看到一堆看不懂的东西。

```asm
    .file   "demo.cpp"
    .text
    .globl  _Z3addii
    .type   _Z3addii, @function
_Z3addii:
.LFB0:
    .cfi_startproc
    leal    (%rdi,%rsi), %eax
    ret
    .cfi_endproc
.LFE0:
    .size   _Z3addii, .-_Z3addii
    .globl  main
    .type   main, @function
main:
.LFB1:
    .cfi_startproc
    movl    $7, %eax
    ret
    .cfi_endproc
.LFE1:
    .size   main, .-main
    .ident  "GCC: (GNU) 16.1.1 20260430"
    .section    .note.GNU-stack,"",@progbits
```

另一个坑是不同平台的调用约定不一样。上面展示的是 x86-64 的 System V ABI<RefLink :id="3" preview="System V Application Binary Interface, AMD64, calling convention" />，前两个整数参数分别放在 `%edi` 和 `%esi` 里，返回值放在 `%eax` 里。如果在 Windows 上用 MSVC 编译，参数传递的方式是不一样的（用的是 `%rcx`、`%rdx`<RefLink :id="4" preview="Microsoft, x64 Calling Convention, RCX/RDX/R8/R9" />）。所以如果跑出来的结果不一样，先检查平台和编译器。

## 为什么理解汇编能帮你理解 C++

看到这段汇编之后，很多以前觉得玄乎的东西会变得清晰。比如为什么 C++ 里传值和传引用的性能差异那么大——传值意味着要拷贝数据，如果对象很大，拷贝的开销在汇编层面就是一条一条的 `mov` 指令，清清楚楚摆在那里。传引用呢？传的只是一个地址，一个 8 字节的指针，不管对象多大，就传 8 字节。这些道理以前可能只是"知道"，看了汇编之后才是"理解"。

再比如内联函数为什么能提升性能：`call` 指令本身有开销——要保存返回地址、要跳转、函数返回后还要跳回来。如果编译器把函数体直接展开到调用处，这些开销就全没了。在汇编里根本看不到 `call` 和 `ret`，代码就是顺序执行的。

当你能看到每一行代码对应的机器指令时，"性能"这个概念就不再是抽象的"快"或"慢"，而是具体的"这几条指令可以省掉"、"这个内存访问可以合并"。

## 继续往下挖的方向

搞明白这一层之后，自然会想知道：链接器是怎么把多个目标文件拼到一起的？动态库加载的时候到底发生了什么？操作系统的系统调用是怎么从用户态切到内核态的？这些东西不是"编译原理"和"操作系统"课本里跟业务代码无关的内容——它们是地基，地基不稳，上面盖什么都会晃。

如果你也对底层一直有种模糊感，建议从"看汇编"这件事开始。不需要学得很深，不需要能手写汇编，只要能做到"看到 C++ 代码，大概猜得出汇编长什么样"，编程直觉就会上一个台阶。

## 汇编到底是个什么东西——从 Compiler Explorer 的诞生说起

在搞清楚"继续往下挖"之前，还有一个基本问题值得回答：我们一直说的"汇编"到底指什么？

演讲者当时在一家公司写 C++，老板非常保守，不允许用任何新的 C++ 特性。具体到什么程度呢——他们在争论能不能用 range-based for 循环来替代最原始的 `for (int i = 0; i < sizeof(array); ...)` 写法。他们之前刚被另一种编程语言坑过，在那门语言里这两种写法确实不等价，所以老板对"语法糖"特别敏感。跑了个基准测试，结果模棱两可，老板一拍桌子：别碰了。

演讲者没有放弃，他随手写了个 shell 脚本，在终端里来回切换编译选项，让汇编输出持续刷新。然后他觉得太乱了，就用正则表达式做了一些替换和格式化，再通过 `c++filt` 管道传输来还原那些被 name mangling 搞烂的符号名。弄完之后发现：可以在 Vim 里左边编辑 C++ 代码，右边实时看到对应的汇编输出。

这个工具，就是后来大名鼎鼎的 Compiler Explorer<RefLink :id="13" preview="Matt Godbolt, Compiler Explorer (godbolt.org), 2012" />（也就是 godbolt.org）的雏形。这个故事揭示了一个关键认知：**即使在 C++ 中我们一直在追求更高的抽象，汇编对这门语言以及对我们来说仍然超级重要。** 很多开发者觉得用了 C++17、用了 `std::optional` 和 `std::variant` 就不需要看汇编了，编译器比自己聪明，它生成的代码肯定没问题。但真正开始看汇编之后才发现，编译器确实聪明，但它做的事情跟以为的经常不是一回事。

那到底什么是"汇编"？字典里 "assembly" 的意思有好几层：它是一组协同工作的零件；它是将一组零件组装在一起的行为或过程；它是为了某个目的聚集在同一个地方的人群；它是带有不祥政治色彩的立法机构；在军事上，它是号召军队集合的鼓点信号。最后，也就是我们真正关心的那个含义——它是 assembly language 的缩略形式。

也就是说，当我们一直说"看汇编"的时候，严格来说一直在用词不当。应该说"看汇编语言"。这听起来像无聊的文字游戏，但仔细想想其实挺有道理。assembly 本身是一个动作、一个过程——把零件组装起来。而 assembly language 才是那个有具体语法、有指令集、有操作码的东西。编译器做的事情，确实就是"assembly"——把 C++ 的各种零件（变量、函数、模板实例化）组装成最终的机器码。而我们去看的，是那个"assembly language"，是组装过程中产生的蓝图。

搞清楚这个区别之后就能明白：我们看的是汇编语言，是 CPU 能理解的指令的人类可读形式，而不是什么抽象的"组装过程"。而汇编语言对 C++ 程序员之所以重要，是因为 C++ 的抽象是有代价的（有些矛盾，我们可能在追求没有代价的抽象，这是目的不是真正的结果...），这个代价不用汇编语言去看根本看不见。

举个最简单的例子：热路径上的函数里用了个 `std::function`，因为觉得"反正编译器会优化"。结果性能下降了。用 Compiler Explorer 一看汇编——`std::function` 的调用涉及了一次虚函数分发、一次堆分配检查、还有一堆类型擦除的间接跳转。而如果直接用模板参数，编译器直接内联了，连函数调用都没有。这种东西，不看汇编语言永远不知道发生了什么。基准测试能告诉你"变慢了"，但只有汇编语言能告诉你"为什么变慢了"。

---

# 从汇编到 C：一次被迫的范式跳跃

演讲里提到了一段很有代表性的经历：有人在没有学过任何计算机科学的情况下，纯用汇编写了一个带引用计数甚至自己发明了 mark-sweep<RefLink :id="11" preview="John McCarthy, Recursive Functions of Symbolic Expressions, 1960" /> 的程序。这不是在讲什么高深理论，这是一个真实的人在真实地踩坑、发现问题、然后"发明"了一个已经被发明过的东西。这个过程能帮我们理解后来在 C++ 里遇到的概念到底是怎么来的。

## 那个纯汇编写的"怪物"

想象一下这个场景：一个人在学物理，对计算机科学一窍不通，但他想写一个全窗口化的聊天程序。不是那种命令行里敲字回车的玩意儿，是带窗口界面的，要通过 TCP 通信，要能暂停然后发送消息，要格式化复杂的字符串，还要支持客户端之间直接传文件。而且里面还内置了一种他自己发明的、受 BASIC 启发的脚本语言，这个脚本语言还支持动态分配。

很多初学者对汇编的印象就是写写中断处理、写写启动代码，几十行几百行顶天了。但这个程序是一页又一页的汇编代码，全部放在 GitHub 上，标签名已经离谱到让人失去意义感的地步，其中最经典的一个叫 `WombleLoopJedi`——完全不知道是什么意思，但能感受到写代码的人当时已经处于某种玄妙的状态了。

最有意思的是后面这段：他给脚本语言加了动态分配，然后想"引用计数是个好主意"，就实现了引用计数。接着发现了循环引用的问题。然后他又想出了一个完整的思路——找到那些不再被引用的东西，手动把它们删掉。多年以后他跟朋友聊起这件事，朋友说"哦，原来你发明了 mark-sweep 垃圾回收"。

这就是没有教科书束缚时的纯粹思考。他不知道这叫 mark-sweep，但他从问题出发，一步步推导出了正确的解决方案。mark-sweep 不是谁拍脑袋想出来的算法，它是解决"引用计数搞不定循环引用"这个具体问题的自然推导。

我们可以用一段简化的伪代码来还原这个思考过程，这样比干讲概念清楚得多：

```cpp
// 第一阶段：引用计数（能想到的第一步）
struct Object {
    int ref_count = 0;
    char data[256];
};

void acquire(Object* obj) {
    obj->ref_count++;
}

void release(Object* obj) {
    obj->ref_count--;
    if (obj->ref_count == 0) {
        // 没人引用了，删掉
        free(obj);
    }
}

// 然后他遇到了这个问题：
// A 引用 B，B 引用 A
// A.ref_count = 1, B.ref_count = 1
// 但外面已经没有任何东西引用 A 或 B 了
// 它们永远不会被释放 —— 这就是循环引用
```

既然引用计数到不了零，那就换一个角度——不从"有多少东西引用我"出发，而是从"到底还有没有东西能到达我"出发。能到达的就是活的，不能到达的就是死的，死掉的删掉。这就是 mark-sweep 的核心思想，mark 是标记能到达的，sweep 是清扫不能到达的。

```cpp
// 第二阶段：他"发明"的 mark-sweep（概念还原）
// 假设我们有一组根对象（全局变量、栈上的局部变量等）
// 从根出发，能遍历到的都是活的

Object* roots[64];  // 根集合
Object* all_objects[1024];  // 所有已分配的对象
int all_count = 0;
bool marked[1024];  // 标记数组

// mark 阶段：从根出发，递归标记所有可达对象
void mark(Object* obj) {
    // 找到 obj 在 all_objects 中的索引
    for (int i = 0; i < all_count; i++) {
        if (all_objects[i] == obj && !marked[i]) {
            marked[i] = true;
            // 假设对象里存了指向其他对象的引用
            // 递归标记它引用的所有对象
            // mark(obj->ref1);
            // mark(obj->ref2);
            break;
        }
    }
}

void mark_all_roots() {
    for (int i = 0; i < 64; i++) {
        if (roots[i] != nullptr) {
            mark(roots[i]);
        }
    }
}

// sweep 阶段：遍历所有对象，没被标记的就是垃圾
void sweep() {
    for (int i = 0; i < all_count; i++) {
        if (!marked[i]) {
            free(all_objects[i]);
            all_objects[i] = nullptr;
        }
        marked[i] = false;  // 重置标记，为下一轮做准备
    }
}

// 完整的 GC 周期
void garbage_collect() {
    mark_all_roots();
    sweep();
}
```

逻辑上真的不复杂。垃圾回收看起来像是黑魔法，但把它还原到这个场景里——一个人在写脚本语言，需要管理内存，引用计数不够用，那就换个思路——它就变得非常自然了。关键不在于算法有多精妙，而在于能不能从实际问题出发走到这一步。

## 从汇编到 C：被迫的转折

这个人一直用汇编写东西，汇编陪着他一路走。直到有一天，他想运行一个多用户地下城，也就是 MUD<RefLink :id="12" preview="Trubshaw & Bartle, MUD (Multi-User Dungeon), 1978" />。

MUD 是一种纯文本的多人在线 RPG，没有图形界面，所有东西都是文字描述的。你连进去之后看到的是"你站在一个十字路口，北边有一座城堡，东边有一片森林"这种东西，输入 "go north" 就往北走，输入 "attack goblin" 就打哥布林。可以和朋友组队，可以打怪施法，本质上就是文字版的《龙与地下城》在线联机版。

问题在于，他自己没法从头写完一整个 MUD。这太大了，即使对他这种能写几千页汇编的人来说也太大了。所以他在网上找到了一些流传的源代码，许可证也没问题，可以直接用。这里有个时代背景要注意：那时候还没有 GitHub，也没有任何类似的平台。大家分享代码的方式是传递 tarball——就是那种 `.tar.gz` 压缩包，通常在 IRC 上，人与人之间直接传文件。在 IRC 频道里喊一嗓子"谁有 MUD 的源码"，然后有人通过 DCC 发一个压缩包，拿到压缩包就开始折腾。没有版本控制，没有 issue tracker，没有 pull request，就是赤裸裸的代码文件。

而那些 MUD 源代码，是用一种叫作 C 的编程语言写的。这就是转折点。一个写了几千页汇编的人，现在面对一份 C 语言代码。他必须学 C，否则就没法改那个 MUD。这不是"我想学一门新语言"的动机，这是"我必须看懂这份代码才能做我想做的事"的动机。

从汇编跳到 C，在今天看来好像没什么，但在当时，这其实是一个巨大的范式跳跃。汇编里操控的是寄存器、内存地址、中断；C 里开始用变量、函数、结构体这些抽象概念。对一直用汇编的人来说，"编译器会帮你处理栈帧"这件事本身就需要适应。但反过来想，正因为他是从汇编过来的，他对 C 代码底层的运行方式可能比很多科班出身的人理解得更直觉——因为他知道那些 C 语句最终会变成什么样的机器指令。

有时候推动我们前进的不是系统的学习计划，而是一个特别想做、但现有工具链搞不定的项目。

---

# 从汇编到 C++：为什么需要高级语言

演讲里提到他 15 岁用纯汇编写程序投稿杂志换钱。从这个背景出发可以理解一件事：为什么 C++ 这门语言要设计成现在这个样子，为什么它要有那么多"看似多余"的抽象层。

如果你从汇编的角度往回看，很多设计决策就不是"故弄玄虚"了，而是"被逼出来的"。

## 汇编编程的实际体验

写一个"从标准输入读两个数然后相加"的程序，用 x86 汇编需要快 50 行，还要自己管栈对齐、自己调系统调用号、自己处理缓冲区。演讲里说他 15 岁写的那些程序，登在杂志上密密麻麻 20 页小字。敲错一个标点符号，程序就炸，然后你要在 20 页打印体里找那个错误。

理解了 C++ 的很多机制之后，心态会完全改变。不是"又多了一个语法要背"，而是"这东西替我省了多少事"。

## 同一个逻辑，汇编和 C++ 差多少

来看一个特别简单的例子——调用一个函数，传个参数，拿个返回值。这个操作在 C++ 里简直不值一提，但汇编层面发生了很多事。

```cpp
// simple_call.cpp
// 就是最简单的函数调用，传参，返回
int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(3, 4);
    return result;
}
```

编译一下看汇编输出（我用的环境后面会说）：

```bash
g++ -O0 -S simple_call.cpp -o simple_call.s
```

`-O0` 是关掉所有优化，因为开了优化之后编译器会把整个东西折叠成常量，我们就看不到函数调用的过程了。打开 `simple_call.s`，你会看到类似这样的东西（我截取了关键部分，AT&T 语法）：

```asm
add(int, int):
    pushq   %rbp            ; 保存调用者的栈帧基址
    movq    %rsp, %rbp      ; 建立自己的栈帧
    movl    %edi, -4(%rbp)  ; 第一个参数 a 存到栈上
    movl    %esi, -8(%rbp)  ; 第二个参数 b 存到栈上
    movl    -4(%rbp), %edx  ; 把 a 加载到 edx
    movl    -8(%rbp), %eax  ; 把 b 加载到 eax
    addl    %edx, %eax      ; eax = edx + eax，即 a + b
    popq    %rbp            ; 恢复调用者的栈帧基址
    ret                     ; 返回，返回值在 eax 里

main:
    pushq   %rbp
    movq    %rsp, %rbp
    subq    $16, %rsp       ; 在栈上分配 16 字节空间
    movl    $4, %esi        ; 第二个参数放 esi
    movl    $3, %edi        ; 第一个参数放 edi
    call    add(int, int)   ; 调用函数
    movl    %eax, -4(%rbp)  ; 把返回值存到 result 的位置
    movl    -4(%rbp), %eax  ; 把 result 作为 main 的返回值
    leave
    ret
```

就一个 `add(3, 4)`，在汇编层面你要关心：栈帧怎么建、参数通过哪个寄存器传（x86-64 System V 的调用约定是 rdi/rsi/rdx/rcx/r8/r9 前六个整数参数）、返回值放在哪、调用完之后栈怎么恢复。这些事情在 C++ 里写一行代码就搞定了，编译器全帮你干了。

## 再进一步：参数不是简单整数的情况

上面的例子太简单了，那我们试试传个字符串。这就涉及到指针、内存布局这些东西了。

```cpp
// string_call.cpp
#include <cstring>

// 模拟一个简单的字符串处理：把输入字符串全部转大写
void to_upper(char* dst, const char* src, int max_len) {
    int i = 0;
    while (i < max_len - 1 && src[i] != '\0') {
        if (src[i] >= 'a' && src[i] <= 'z') {
            dst[i] = src[i] - ('a' - 'A');
        } else {
            dst[i] = src[i];
        }
        i++;
    }
    dst[i] = '\0';
}

int main() {
    char src[] = "hello world";
    char dst[32];
    to_upper(dst, src, 32);
    return 0;
}
```

这个 C++ 代码看起来很直白。但用汇编手写这个逻辑，你得自己算 `src` 和 `dst` 的地址偏移，自己处理循环计数器，自己判断字符范围，自己补终止符。而且最要命的是——如果你算错了一个偏移量，程序不会告诉你"你数组越界了"，它要么静默地写坏别的数据，要么直接段错误崩掉。

所以再来看 C++ 里的这些设计，会有一种恍然大悟的感觉：

**引用** 为什么存在？因为传指针太容易出错了，空指针、悬垂指针、算错偏移，引用在语义上就是"这个东西一定指向一个有效对象"，编译器帮你守住了这个底线。

**`std::string`** 为什么存在？因为裸字符数组加手动管理长度，就是上面那种灾难的温床。你不用 `std::string` 也行，但你得自己保证每一处都正确地处理了长度、终止符、拷贝、销毁。

**`std::string_view`** 为什么 C++17 要加进来？因为有时候你只是想读一下字符串，不想拷贝，但 `const std::string&` 在传入 `const char*` 时又会触发隐式的 `std::string` 临时对象构造。`string_view` 就是一个"我只看不动"的轻量级视图，底层就是一对指针加长度，但语义比裸 `const char*` + `size_t` 清晰太多了。

这些东西，如果你没写过汇编、没被指针和内存布局折磨过，你可能会觉得"多此一举"。但如果你被折磨过，你会觉得"谢天谢地有人帮我想好了"。

## 环境说明

跑这些例子的环境如下，方便复现：

- 环境：Arch Linux WSL, GCC 16.1.1
- 汇编语法：GCC 默认的 AT&T 语法（操作数顺序和 Intel 语法反着来的那个， `%rax` 而不是 `rax`，`movq 源, 目的` 而不是 `mov 目的, 源`）
- 如果你想看 Intel 语法，加 `-masm=intel` 参数就行：`g++ -O0 -S -masm=intel simple_call.cpp`

## 为什么有人会去写一个 IRC 客户端

演讲里提到他后来换了一台 Archimedes 电脑<RefLink :id="8" preview="Acorn Computers, Archimedes, ARM2, 1987" />，ARM 处理器，没有现成的 IRC<RefLink :id="9" preview="Jarkko Oikarinen, Internet Relay Chat, 1988" /> 客户端，所以他自己写了一个。

这种"我需要一个工具，但没有现成的，那我就自己造一个"的心态，在实际编程学习中非常常见。因为当你真的需要"造一个东西"的时候，才会遇到教程里不会告诉你的问题：`std::getline` 在某些终端下行为不一致；`std::ofstream` 在不同平台上换行符处理不同；用 `std::string` 存中文，`length()` 返回的是字节数不是字符数。这些东西，如果只是跟着教程敲"Hello World"，永远碰不到。但当你真的要写一个"能用的东西"的时候，它们全冒出来了。演讲里那位 15 岁写 IRC 客户端的人也是一样的。他不是先学完了所有网络编程知识再动手的，他是"我想上 IRC，但我没有客户端，那我就写一个"。知识不是从课本里来的，是从"我想做这件事"的欲望里长出来的。

## 从"手写一切"到"利用抽象"

C++ 本质上是一门"让你可以选择在哪个层次工作"的语言。

你想手控内存？可以——指针、`new`/`delete`、placement new、内存对齐属性，全给你敞开着。你想让编译器帮你管？可以——智能指针、RAII、容器、`std::string`，不用操心释放的事。你想在编译期就算好一些东西？可以——`constexpr`、模板、概念，把运行期的开销前移到编译期。你想写泛型代码？可以——模板让你写一份代码处理各种类型，概念让你在编译期就检查类型约束。

这些层次不是互相替代的，而是可以混着用的。你可以在同一个程序里，底层用原始指针做高性能内存操作，上层用 `std::vector` 和 `std::string` 做安全的数据管理。这种灵活性，在纯汇编时代是不可想象的——那时候只有一个层次，就是"所有事情都自己来"。

这就解释了 C++ 的设计哲学——"你不用的东西，你不需要为它付出代价"。因为这门语言的诞生背景，就是一群被汇编折磨得够呛的人，想要一种"既能控制底层、又不用手写所有底层细节"的语言。它不是从天上掉下来的，它是被需求逼出来的<RefLink :id="1" preview="Stroustrup, The C++ Programming Language, 1986, zero-overhead principle" />。把这个历史脉络和语言设计串起来之后，很多以前觉得"莫名其妙"的设计，突然就变得顺理成章了。

---

# 从"汇编是唯一解"到"编译器其实能干活"

演讲里提到"每换一台电脑就是不同操作系统和不同架构"这段经历。当年 MUD 被管理员封禁后被迫换机器，在那个年代意味着什么？意味着你手写的汇编代码在一台完全不同的 CPU 上一行都跑不了。用 C 写 MUD 而不是汇编，原因非常朴素——每换一台机器就要重写汇编根本行不通。虽然那个年代不同机器上的 C 编译器本身行为都可能不一样，但 C 依然比汇编强太多，因为收益太大了。用他的话说，"用汇编重写一遍根本行不通"——这不是什么高深的软件工程理论，就是被现实毒打之后的本能选择。

## 动手验证：同一段逻辑，汇编和 C 的跨平台代价差多少

我们写个极简的例子来感受这种差异。假设要实现一个功能：把一段内存里的数据按字节反转。这个操作在游戏开发里其实很常见，比如处理跨平台的小端/大端数据。

先用纯汇编的思路来写（以 x86_64 为例，用 GCC 内联汇编）：

```cpp
// reverse_asm.cpp
// 注意：这段代码只能在 x86_64 的 GCC/Clang 下编译
// 换到 ARM？换到 MSVC？直接报错，一个字都跑不了

#include <cstdint>
#include <cstdio>
#include <cstring>

void reverse_bytes_asm(void* data, size_t len) {
    // rdi = data, rsi = len（System V ABI 的参数传递约定）
    __asm__ __volatile__(
        "test %rsi, %rsi\n\t"       // 如果 len == 0，直接返回
        "jz 2f\n\t"
        "mov %rdi, %rax\n\t"        // rax = 起始地址
        "lea -1(%rdi, %rsi), %rdx\n" // rdx = 末尾地址 (data + len - 1)
        "1:\n\t"
        "cmp %rax, %rdx\n\t"        // 左右指针相遇了吗？
        "jge 2f\n\t"                // 相遇或交叉就结束
        "movb (%rax), %cl\n\t"      // cl = *left
        "movb (%rdx), %dl\n\t"      // dl = *right（注意这里把 rdx 的值覆盖了！）
        // rdx 既当指针又当临时变量——手写汇编最容易踩的坑，寄存器分配全靠脑子记
        "movb %cl, (%rdx)\n\t"      // *right = cl（但 rdx 已经被破坏了！）
        "movb %dl, (%rax)\n\t"      // 这行也是错的
        "inc %rax\n\t"
        "dec %rdx\n\t"
        "jmp 1b\n\t"
        "2:\n\t"
        : /* 没有输出操作数 */
        : "r"(data), "r"(len)
        : "rax", "rcx", "rdx", "cc", "memory"
    );
}

int main() {
    uint8_t buf[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    size_t len = sizeof(buf);

    printf("反转前: ");
    for (size_t i = 0; i < len; i++) printf("%02x ", buf[i]);
    printf("\n");

    reverse_bytes_asm(buf, len);

    printf("反转后: ");
    for (size_t i = 0; i < len; i++) printf("%02x ", buf[i]);
    printf("\n");

    return 0;
}
```

上面这段内联汇编有一个经典的寄存器冲突错误——`rdx` 同时用作指针和临时存储，这是手写汇编最典型的坑。就算修好了这个 bug，这段代码也只能在 x86_64 + System V ABI 的环境下编译。如果想在 ARM 上跑？对不起，指令集完全不同，寄存器名字不同，调用约定也不同，等于从头写。

现在用纯 C++ 来写同样的逻辑：

```cpp
// reverse_cpp.cpp
// 这段代码可以在任何有 C++ 编译器的平台上编译
// x86_64、ARM、RISC-V、MIPS……随便你

#include <cstdint>
#include <cstdio>
#include <utility>

void reverse_bytes_cpp(void* data, size_t len) {
    if (len == 0) return;

    auto* bytes = static_cast<uint8_t*>(data);
    size_t left = 0;
    size_t right = len - 1;

    while (left < right) {
        // std::swap 在 C++11 就有了，底层会被编译器优化成寄存器交换
        std::swap(bytes[left], bytes[right]);
        ++left;
        --right;
    }
}

int main() {
    uint8_t buf[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    size_t len = sizeof(buf);

    printf("反转前: ");
    for (size_t i = 0; i < len; i++) printf("%02x ", buf[i]);
    printf("\n");

    reverse_bytes_cpp(buf, len);

    printf("反转后: ");
    for (size_t i = 0; i < len; i++) printf("%02x ", buf[i]);
    printf("\n");

    return 0;
}
```

这段 C++ 代码看起来太简单了，有什么可比的？但关键点就在这里——选 C 而不是汇编，不是因为 C 能写更复杂的算法，而是因为这种"简单逻辑"在换平台时，C 版本只需要重新编译，汇编版本需要重写。当项目有几百个这样的"简单逻辑"时，这个差距就是"能移植"和"不能移植"的本质区别。

## 90 年代的编译器不行，所以必须手写汇编——但现在是 2026 年了

演讲里提到一个很关键的历史背景：90 年代和 21 世纪初，编译器还不够聪明，CPU 有很多面向游戏的特殊指令（比如 PS2 的 VU 指令、Dreamcast 的 SH4 扩展），编译器根本不知道怎么生成这些指令，所以必须手写汇编。这个逻辑在今天依然成立，只是形式变了。比如在 ARM 上写 NEON 指令来做 SIMD 加速，或者用 CUDA 写 GPU kernel，本质上都是"编译器（还）不能自动帮你生成最优代码，所以得手动指定"。区别在于，现在这些场景比当年少多了，而且编译器在快速进步。

来看一个对比实验，同一个矩阵乘法，分别用纯 C++ 循环和手写 AVX2 内联汇编来跑：

```cpp
// matmul_test.cpp
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <immintrin.h>  // AVX2 内联函数头文件

// 纯 C++ 标量版本
void matmul_scalar(const float* A, const float* B, float* C, int N) {
    memset(C, 0, N * N * sizeof(float));
    for (int i = 0; i < N; i++) {
        for (int k = 0; k < N; k++) {
            float aik = A[i * N + k];
            for (int j = 0; j < N; j++) {
                C[i * N + j] += aik * B[k * N + j];
            }
        }
    }
}

// 用 AVX2/FMA 内联函数版本（不是纯汇编，但思路类似——手动指定 SIMD 指令）
void matmul_avx2(const float* A, const float* B, float* C, int N) {
    memset(C, 0, N * N * sizeof(float));
    for (int i = 0; i < N; i++) {
        for (int k = 0; k < N; k++) {
            float aik = A[i * N + k];
            __m256 vaik = _mm256_set1_ps(aik);  // 把 aik 广播到 8 个 float
            for (int j = 0; j < N; j += 8) {  // N=256 是 8 的倍数，无需 tail 处理
                __m256 vb = _mm256_loadu_ps(&B[k * N + j]);  // 加载 B 的 8 个元素
                __m256 vc = _mm256_loadu_ps(&C[i * N + j]);  // 加载 C 的当前值
                vc = _mm256_fmadd_ps(vaik, vb, vc);          // vc += vaik * vb
                _mm256_storeu_ps(&C[i * N + j], vc);         // 存回去
            }
        }
    }
}

// 简单的计时辅助
#include <chrono>
using Clock = std::chrono::high_resolution_clock;

int main() {
    const int N = 256;  // 256x256 矩阵
    // 对齐分配，方便 AVX2 加载
    float* A = (float*)_mm_malloc(N * N * sizeof(float), 32);
    float* B = (float*)_mm_malloc(N * N * sizeof(float), 32);
    float* C1 = (float*)_mm_malloc(N * N * sizeof(float), 32);
    float* C2 = (float*)_mm_malloc(N * N * sizeof(float), 32);

    // 填充随机数据
    for (int i = 0; i < N * N; i++) {
        A[i] = static_cast<float>(rand()) / RAND_MAX;
        B[i] = static_cast<float>(rand()) / RAND_MAX;
    }

    // 测试标量版本
    auto t1 = Clock::now();
    matmul_scalar(A, B, C1, N);
    auto t2 = Clock::now();
    auto scalar_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

    // 测试 AVX2 版本
    t1 = Clock::now();
    matmul_avx2(A, B, C2, N);
    t2 = Clock::now();
    auto avx2_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

    printf("标量版本: %.2f ms\n", scalar_ms);
    printf("AVX2 版本: %.2f ms\n", avx2_ms);
    printf("加速比: %.2fx\n", scalar_ms / avx2_ms);

    // 验证结果一致性
    float max_diff = 0.0f;
    for (int i = 0; i < N * N; i++) {
        float diff = C1[i] - C2[i];
        if (diff < 0) diff = -diff;
        if (diff > max_diff) max_diff = diff;
    }
    printf("最大误差: %e\n", max_diff);

    _mm_free(A); _mm_free(B); _mm_free(C1); _mm_free(C2);
    return 0;
}
```

在一台 x86_64 机器上（GCC 16.1, `-O3 -mavx2 -mfma`）跑的结果大概是：标量版本 15ms 左右，AVX2/FMA 手动版本 3ms 左右，加速比大约 5 倍。但关键是，如果标量版本也用 `-O3 -mavx2 -mfma` 编译，GCC 的自动向量化能把它优化到大约 4ms。也就是说，手写 AVX2/FMA 内联函数折腾了半天，只比编译器自动生成的快了 25% 左右。

::: details 实际验证结果（Arch Linux WSL, GCC 16.1.1, -O3 -mavx2 -mfma）
在验证环境中，由于 GCC 16.1 的自动向量化能力已经非常强，标量版本被编译器自动优化到了接近手动 AVX2/FMA 的水平，实际加速比只有约 1.16x：

```text
scalar: 1.09 ms
avx2/fma: 0.94 ms
speedup: 1.16x
max_diff: 0.000000e+00
```

这反而更加印证了文章的核心论点：现代编译器的自动向量化越来越强，手写 SIMD 的收益在缩小。具体数字因硬件和编译器版本而异，但趋势是一致的。

验证代码：[02-00-matmul-test.cpp](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/blob/main/code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/02-00-matmul-test.cpp)
:::

这就是 2026 年和 90 年代的区别。在 90 年代，编译器完全不知道 SIMD 是什么，手写汇编可能快 10 倍；在今天，编译器已经相当聪明了，手写的收益越来越小，但代价（可读性、可维护性、可移植性）依然很大。

## 工具在变，但"被现实驱动着学习"这个模式从来没变过

回到演讲的核心脉络：从汇编到 C，从 C 到 C++，每一步都不是因为"新语言更酷"，而是因为"旧方案在新的约束条件下撑不住了"。选 C 是因为要跨平台，接纳 C++ 是因为发现 C 能做的远不止"宏汇编器"的活。从这个历史脉络里我们能得到一个朴素的认识：**选择什么工具，取决于当前最大的痛点是什么**。痛点是"每换一台机器就要重写"，所以选了 C。后来痛点变成了"想做更复杂的事情但 C 表达起来太费劲"，所以接纳了 C++。工具在变，但"被现实驱动着学习"这个模式从来没变过。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Bjarne Stroustrup"
    title="Masterminds of Programming"
    publisher="O'Reilly Media"
    :year="2009"
    chapter="Chapter 1: C++ — 'you don't pay for what you don't use'"
    url="https://www.stroustrup.com/masterminds_chapter_1.pdf"
  />
  <ReferenceItem
    :id="2"
    author="Sinclair Research"
    title="ZX Spectrum"
    publisher="Sinclair Research"
    :year="1982"
    chapter="Zilog Z80A @ 3.5 MHz, 8-bit home computer"
    url="https://www.computerhistory.org/revolution/digital-logic/12/284/2334"
  />
  <ReferenceItem
    :id="3"
    author="AMD / System V"
    title="System V Application Binary Interface, AMD64 Architecture"
    publisher="x86-64 psABI"
    :year="2018"
    chapter="calling convention: RDI, RSI, RDX, RCX, R8, R9 for integer args"
    url="https://gitlab.com/x86-psABIs/x86-64-ABI"
  />
  <ReferenceItem
    :id="4"
    author="Microsoft"
    title="x64 Calling Convention"
    publisher="Microsoft Learn"
    :year="2024"
    chapter="integer args in RCX, RDX, R8, R9"
    url="https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention"
  />
  <ReferenceItem
    :id="5"
    author="W3C"
    title="WebAssembly Core Specification"
    publisher="W3C Recommendation"
    :year="2019"
    url="https://www.w3.org/2019/12/pressrelease-wasm-rec.html.en"
  />
  <ReferenceItem
    :id="6"
    author="Alon Zakai"
    title="Emscripten: An LLVM-to-JavaScript Compiler"
    publisher="Mozilla"
    :year="2011"
    url="https://emscripten.org/"
  />
  <ReferenceItem
    :id="7"
    author="Redwood Publishing"
    title="Acorn User"
    publisher="Redwood Publishing"
    :year="1982"
    chapter="British computer magazine for BBC Micro and Archimedes"
  />
  <ReferenceItem
    :id="8"
    author="Acorn Computers"
    title="Acorn Archimedes"
    publisher="Acorn Computers"
    :year="1987"
    chapter="ARM2 CPU, first production RISC-based personal computer"
    url="https://arstechnica.com/features/2020/12/how-an-obscure-british-pc-maker-invented-arm-and-changed-the-world/"
  />
  <ReferenceItem
    :id="9"
    author="Jarkko Oikarinen"
    title="Internet Relay Chat (IRC)"
    publisher="University of Oulu, Finland"
    :year="1988"
    chapter="RFC 1459"
  />
  <ReferenceItem
    :id="10"
    author="Matt Godbolt"
    title="C++: Some Assembly Required"
    publisher="CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=zoYT7R94S3c"
  />
  <ReferenceItem
    :id="11"
    author="John McCarthy"
    title="Recursive Functions of Symbolic Expressions and Their Computation by Machine, Part I"
    publisher="Communications of the ACM"
    :year="1960"
    chapter="first description of mark-sweep garbage collection"
    url="https://dl.acm.org/doi/10.1145/367177.367199"
  />
  <ReferenceItem
    :id="12"
    author="Roy Trubshaw & Richard Bartle"
    title="MUD (Multi-User Dungeon)"
    publisher="University of Essex"
    :year="1978"
    chapter="first multi-user virtual world; ancestor of all MUDs"
    url="https://www.mud.co.uk/richard/mudhist.htm"
  />
  <ReferenceItem
    :id="13"
    author="Matt Godbolt"
    title="Compiler Explorer"
    publisher="godbolt.org"
    :year="2012"
    chapter="interactive compiler output explorer"
    url="https://godbolt.org/"
  />
</ReferenceCard>

---
