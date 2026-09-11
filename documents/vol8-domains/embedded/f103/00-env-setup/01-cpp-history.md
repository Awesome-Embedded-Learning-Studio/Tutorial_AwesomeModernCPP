---
title: "C++ 简史：这些坏印象是哪来的"
description: "开篇用四份固件破了 C++ 笨重和 OOP 的迷思，这一篇回答剩下的问题：坏印象从哪来。从 1979 年的 C with Classes 讲到 Cfront 之死、九十年代 Embedded C++ 砍特性事件、再到 MISRA/AUTOSAR 把 C++ 收编进安全标准——全部史实带出处，结尾把嵌入式的现状坐标摆给您"
chapter: 0
order: 1
tags:
  - host
  - cpp-modern
  - beginner
  - 基础
difficulty: beginner
platform: host
reading_time_minutes: 12
related:
  - "为什么是 C++,凭什么?"
---

# 咳咳，你前面对C++吹的天花乱坠，我很好奇之前的“流言蜚语”怎么来的

> 笔者不是编程语言历史专家，是查询资料得出下的结论，如有错误，还请嘴下扰人，Issue请~

现在，我们至少终于可以为"单片机上 C++ 跑不动"的措辞判下死刑了——战争结束了，现在！但是笔者想说的是，不要停下来！我们最好想一想，一个工程师圈子集体相信了三十年的说法，总得有个源头。答案是：它不是造谣，它是历史——而且大概率是 1990 年代攒下的真事。那个年代的 C++ 编译器什么水平、那个年代的芯片什么内存、那个年代发生了什么著名的"砍特性"事件。笔者为此还特意决定去搜索一下，这里也给大家休息一下，唠唠嗑算是！其实，**从 C++ 诞生的第一天起，它瞄准的就是咱们这块 64 KB Flash 的板子, 而不是上位机**

## 1979：它生下来就是系统语言

1979年，那年，咱们的C++语言发明者 Bjarne Stroustrup，在 Bell 实验室（熟悉嘛？这个实验室还诞生了另外两个了不起的东西，一个是Unix，另外一个，就是C语言）下正在搞他的博士课题。

> Bjarne Stroustrup 在贝尔实验室开始折腾他的 "C with Classes"，出发点吧，也不是"给桌面程序员一个更好的语言"，而是他要做的博士课题：在分布式系统里做模拟，SIMULA 的类好用，但是实在是太慢，C 快，贼快，超级快，但写大规模程序太容易把代码搅成一团，捏着鼻子写太难受了。本杰明说要把两者捏在一起<RefLink :id="1" preview="Mahmutbegović, C++ in Embedded Systems, Packt, 2025, Ch.1" />。

1983年，Bjarne Stroustrup 决定把这门语言用更加简洁的方式替代称呼，并且，我们这样叫了他43年——C++。

1985 年的 10 月 14 日，第一个正式编译器 Cfront 1.0 和《The C++ Programming Language》第一版同天发布<RefLink :id="2" preview="Stroustrup, isocpp.org, Celebrating the 30th Anniversary of the first C++ compiler, 2015" />。从这一天开始，C++终于慢慢走上正轨，并且伴随到C++98，C++03，C++11。。。与之走到今天的C++26.

似乎从这里看来，"C++ 是桌面语言，硬塞进单片机才显得笨重"这个印象从源头上就不成立：一门为了写操作系统级别的系统软件而生的语言，"直接操作硬件"和"不付多余的运行时开销"是它的出厂设定，不是后来补的承诺。Stroustrup 自己后来专门做过一场给嵌入式开发者的主题演讲，讲的就是 C++ 在资源管理、可靠性、零开销抽象上能为嵌入式做什么<RefLink :id="3" preview="Stroustrup, Keynote: What can C++ do for embedded systems developers?, NDC Conferences" />。这不是他退休后的情怀演讲，这套诉求从 1979 年就没变过。

嘶。。。那，这个印象是从哪开始歪的？

## Cfront 噶了

编译器！你背叛我！是的，居然是编译器。咱们看 Cfront 的架构，笔者认为，这真是收到了C With Class那套的思路啊，这个编译器更加像是。。。翻译器？因为他压根就不直接生成机器码的编译器，而是把 C++ 翻译成 C，再借各平台的 C 编译器出活。。。

别笑，真别笑。这个设计其实放在当时真算挺聪明的，那个时候大家都是认C的，嘿你说你最后搞出来了C，这样的话我们就能够以非常，非常小的代码，就铺满了几乎所有机器上。大伙在被bug狠狠攻击的时候，在食堂吃口热乎饭的时候会说：嘿伙计们，你们知道C++不，听说好像要搞OOP那套哦。

嗯？那代价是啥呢？兄弟们，生成C，意味着代价是它生成的代码质量永远受限于底下的 C 编译器。你在咋样，都是编译的preprocessor环节，有啥出息呢？一辈子只能低三下四的被C编译器受影响。

Cfront的死刑是1993年判定的，开发团队试图给 Cfront 4.0 加异常支持失败，整个编译器就地退役<RefLink :id="4" preview="Wikipedia, Cfront" />。筐的一下，大伙就知道Cfront这套，很有可能走不下去了。

咱们站在嵌入式工程师的角度品一下这件事：那个年代您如果用 C++，用的很可能就是 Cfront 系或同时代的产品，而**当年 C++ 最著名的"贵"特性恰恰就是异常**——展开表、运行时库、不可预测的栈回溯。编译器自己都死在异常上，工业界的恐惧不是空穴来风。再加上早年 GCC 的模板实例化膨胀、iostream 拖库这些真问题，"C++ 又大又慢"在 1990 年代是**可复现的观察**，这下C++否定了他自己，在一些层面上！

## Embedded C++：一次著名的砍特性，和它的反面教材

哟！咱们快进到了九十年代中期，东芝、日立、富士通、NEC 这几家日本芯片巨头牵头的 Embedded C++ 技术委员会觉得：C++ 对嵌入式来说太大了，得造一个"嵌入式专用版"。1996 年 9 月，草案出台<RefLink :id="5" preview="Perforce, A Brief History of MISRA C++, 2021" />。这个叫 EC++ 的方言砍起特性来毫不手软：模板、异常、RTTI、多重继承、命名空间、新式转换，全砍；标准库里 STL 和 locales 整个拿掉，iostream 换了个简化版<RefLink :id="6" preview="Wikipedia, Embedded C++" />。当时的编译器厂商也真金白银跟进过，Green Hills 出过专门的 EC++ 编译器<RefLink :id="7" preview="EE Times, Green Hills Unveils Compiler for Embedded C++" />。

然后呢？咱们直接看结局，它比任何论证都有说服力。

EC++ 本身实际使用寥寥。市场真正捧起来的，是它的一个变种 Extended EC++，**把模板加回来的版本**<RefLink :id="5" preview="Perforce, A Brief History of MISRA C++, 2021" />。砍模板的人赌的是"模板会膨胀代码"，用模板的人发现的是"模板是零开销抽象的载体"：上一篇咱们实测的那条"模板版与 HAL 版逐条指令相同"，在九十年代末就已经有人用 Extended EC++ 投过票了。

ISO 标准委员会的回应更值得咱们记一辈子：他们没有认可 EC++，而是发布了一份性能技术报告（Performance Technical Report），逐个特性给出时间和空间开销的模型，以及高效的实现技术<RefLink :id="5" preview="Perforce, A Brief History of MISRA C++, 2021" />。

啊，看不懂？其实就是——你们这批搞嵌入式的，天天说“Oh C++ Cost Too Much”，费解，我们委员会的说辞是——“Take what you really want”，而不是做这个事情不用走路就把腿砍了。**一个语言的子集会跟着标准漂移变成孤儿，一份开销清单永远有效**——EC++ 的代码最终没法跟标准 C++ 互通，而 `-fno-exceptions` 这种按项目的编译开关活到今天还是工程正解。上一篇您在 libestdx 工具链文件里看到的 `-fno-exceptions -fno-rtti`，就是这条路线的直系后代。

## 标准化年代：从 C++98 到安全行业的用脚投票

1998 年，第一版 ISO 标准 C++98 落地，语言进入稳步演进期。真正的大转折是 2011 年的 C++11：`constexpr` 把计算推到编译期，`<atomic>` 给多核裸机编程立了标准内存模型，这两个特性对嵌入式的分量，咱们后面每一站都会反复尝到。此后 14、17、20、23 一路小步快跑，C++20 的 concepts 正是上一篇那道"配错方向编译期报错"的底层机制<RefLink :id="8" preview="cppreference.com, History of C++" />。

而比语言演进更能说明问题的，在咱们看来是**规范侧的态度**。安全攸关行业是对"语言开销可控"最挑剔的客户，它们的时间线是这样走的<RefLink :id="9" preview="Parasoft, Breaking Down the AUTOSAR C++14 Coding Guidelines" />：2008 年 MISRA C++:2008 发布，基线是 C++03；2017 年汽车行业的 AUTOSAR 出了 AUTOSAR C++14 指南，全称就叫"在关键与安全相关系统中使用 C++14 语言的指南"；2023 年两边合并，MISRA C++:2023 发布，基线直接跳到 C++17，把 AUTOSAR 的规则整体吸收<RefLink :id="10" preview="Perforce, What You Need to Know About the Next MISRA Standard" />。

咱们品一下这条时间线意味着什么：自动驾驶的核心软件正在用 C++17 写。这个对"每条指令都要说得清开销"的行业，在 EC++ 诞生二十多年后，给出的答案不是给 C++ 删特性，而是**写指南约束用法、然后整语言往前用**。历史在这里绕了一个大圈，回到了 ISO 那份性能报告的路线上。换而言之，特别要强调的是——现代的C++的确正在流行起来，虽然的确身上背着一屁股历史债务。

## 2026年呢？

笔者不是专业的，图一乐的说辞，大伙看个响就好。

C，直到今天，仍然统治嵌入式，这没什么好争的，Facts are facts。这也是笔者当时想尝试一下C++写嵌入式下的CFBox的时候（哦，是一个busybox的C++23平替），我的公众号那简直是一片声讨啊。“开源你不用C”的论点震撼我了好几个月。当时还跟朋友打趣说我辞职研究这句话了哈哈。

C是个出色的语言，就像您跟我说“喂帮我看看这段代码的性能问题”的时候，如果是C我是最乐意的，一眼望下去汇编就能猜个七七八八，到底离硬件更近。

但是，嵌入式的蛋糕，以Rust, C++为代表的新生代语言的确正在迅速的瓜分嵌入式这块巨大的蛋糕。Stack Overflow 的开发者调查 2024 年第一次加了嵌入式专节，2025 年又继续扩充了嵌入式相关的新问题<RefLink :id="12" preview="Stack Overflow Developer Survey 2024/2025, Embedded technologies" />。嵌入式语言的多元化是进行时，而不是将来时。

发动疑问之那我们呢？

第一，坏印象要连本带利还清也得还准账：C++ 在九十年代确实"又大又慢"，但那是 Cfront 和 64 KB 都算大内存的年代的账，今天的编译器已经换了三代了：g++, clang++, msvc...这些事实上的编译器巨头们，编译出来的二进制丝毫不会让您和您的CPU失望（大部分情况下，嗯）

第二，EC++ 的教训不是"C++ 不适合嵌入式"，而是 **"砍语言子集"这条路本身走不通**。所以我们才会选择配置编译选项，理解编译特性而不是跑路另造轮子。

第三，生态已经备齐了：零堆容器有 ETL（Embedded Template Library）和 Google 的 Pigweed 这类专门为裸机做的库，C++26 还会把栈上定长的 `inplace_vector` 收进标准库。您在上篇看到的 libestdx，就是这门手艺在咱们这条线上的实践。

> PS一下，笔者也会尝试分享一下自己使用对应的binutils分析二进制的思路。
>
> 嵌入式 C++ 最独特的工程动作，是"不信任任何抽象声明，直接看产物"：`arm-none-eabi-size` 看体积、`objdump -d` 看指令，上一篇咱们已经全用上了；后面还会加上链接器 map 文件和 bloaty 这类体积归因工具，把"哪个函数吃了我的 Flash"问到人头上去。让我们在的确困惑他娘的发生什么的时候，终于可以说哈哈小子就是你干的。
>
> 这套手法在 C++ 这里的分量格外重：**它是"零开销抽象"这个承诺的重要的验收手段**。

## 这条线站在哪

收个尾。五十年下来，C++ 从贝尔实验室的系统语言野心出发，背着九十年代编译器的旧账和 EC++ 的制度记忆走到今天，安全行业用 MISRA C++:2023 给它发了关键系统的通行证。它依然不完美：**语言复杂、历史包袱重、学曲线陡，这些都是真的。而且也同样可以成为您不应该使用C++来进行嵌入式编程的一个重要的理由！**

但在 64 KB Flash 的板子上，用类型拦住事故、用编译期算好配置、并且每一条开销都能拿 objdump 验收，这条路径在 2026 年是真实可走的。我想，咱们没有理由不站起来，尝试一个更加现代化的体验开发。这也是笔者做嵌入式C++的理由，也是笔者开设TAMCPP的重要的出发点。

下一篇，咱们回到工具本身：把手头的环境搭起来，把 Renode 这位老大哥上来。现在恭喜你，你真的可以不需要板子就准备来试试嵌入式咯！

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Amar Mahmutbegovic"
    title="C++ in Embedded Systems: A practical transition from C to modern C++"
    :year="2025"
    url="https://www.packtpub.com/en-us/product/c-in-embedded-systems-9781835881149"
    chapter="Packt Publishing, Ch.1 对 C with Classes 起源的叙述"
  />
  <ReferenceItem
    :id="2"
    author="Bjarne Stroustrup"
    title="Celebrating the 30th Anniversary of the first C++ compiler"
    :year="2015"
    url="https://isocpp.org/blog/2015/10/cpp-30"
    chapter="Cfront 1.0 与 TC++PL 第一版同日发布的一手回忆"
  />
  <ReferenceItem
    :id="3"
    author="Bjarne Stroustrup"
    title="Keynote: What can C++ do for embedded systems developers?"
    url="https://www.youtube.com/watch?v=VoHOLDdfDhk"
    chapter="NDC Conferences 主题演讲"
  />
  <ReferenceItem
    :id="4"
    author="Wikipedia"
    title="Cfront"
    :year="2026"
    url="https://en.wikipedia.org/wiki/Cfront"
    chapter="1993 年 Cfront 4.0 加异常失败后被弃"
  />
  <ReferenceItem
    :id="5"
    author="Perforce"
    title="A Brief History of MISRA C++"
    url="https://www.perforce.com/blog/qac/misra-cpp-history"
    chapter="EC++ 委员会、草案时间、Extended EC++ 与 Performance TR"
  />
  <ReferenceItem
    :id="6"
    author="Wikipedia"
    title="Embedded C++"
    :year="2026"
    url="https://en.wikipedia.org/wiki/Embedded_C%2B%2B"
    chapter="被砍特性清单与委员会构成"
  />
  <ReferenceItem
    :id="7"
    author="EE Times"
    title="Green Hills Unveils Compiler for Embedded C++"
    :year="1997"
    url="https://www.eetimes.com/green-hills-unveils-compiler-for-embedded-c/"
    chapter="EC++ 的编译器厂商跟进记录"
  />
  <ReferenceItem
    :id="8"
    author="cplusplus.com"
    title="History of C++"
    :year="2026"
    url="https://cplusplus.com/info/history/"
    chapter="1979-2020 年代标准时间线"
  />
  <ReferenceItem
    :id="9"
    author="Parasoft"
    title="Breaking Down the AUTOSAR C++14 Coding Guidelines"
    :year="2023"
    url="https://www.parasoft.com/blog/breaking-down-the-autosar-c14-coding-guidelines-for-adaptive-autosar/"
    chapter="AUTOSAR C++14 与 MISRA C++ 2023 的基线对比"
  />
  <ReferenceItem
    :id="10"
    author="Perforce"
    title="What You Need to Know About the Next MISRA Standard"
    :year="2023"
    url="https://www.perforce.com/blog/qac/misra-cpp-2023-intro"
    chapter="MISRA C++:2023 吸收 AUTOSAR 指南"
  />
  <ReferenceItem
    :id="11"
    author="Jacob Beningo"
    title="The Best Embedded Programming Languages for Engineers Now"
    :year="2024"
    url="https://www.beningo.com/the-best-embedded-programming-languages-for-engineers-now/"
    chapter="行业调查口径:C 驱动全球超过 60% 的嵌入式项目"
  />
  <ReferenceItem
    :id="12"
    author="Stack Overflow"
    title="Developer Survey 2024/2025 — Embedded technologies"
    :year="2025"
    url="https://survey.stackoverflow.co/2025/technology"
    chapter="2024 首设嵌入式小节,2025 扩为完整板块"
  />
</ReferenceCard>
