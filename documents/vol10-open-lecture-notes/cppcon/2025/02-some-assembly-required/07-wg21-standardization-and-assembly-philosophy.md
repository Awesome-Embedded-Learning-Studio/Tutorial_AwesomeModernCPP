---
title: "WG21 标准化与 x86/RISC-V 汇编哲学"
description: "CppCon 2025 演讲笔记 —— C++: Some Assembly Required by Matt Godbolt"
conference: cppcon
conference_year: 2025
talk_title: "C++: Some Assembly Required"
speaker: "Matt Godbolt"
video_bilibili: "https://www.bilibili.com/video/BV1ptCCBKEwW?p=2"
video_youtube: "https://www.youtube.com/watch?v=zoYT7R94S3c"
tags:
  - cpp-modern
  - host
  - intermediate
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
chapter: 2
order: 7
---

# WG21 与 C++ 标准的组织链路

在各种技术文章和视频里，我们经常看到"WG21"这个缩写，但很少有人把这条完整的组织链路从头到尾捋清楚。实际上层级虽然多，结构本身并不复杂——我们先把这条链路理一遍，后面再看提案和标准文档的时候，至少知道这些东西是从哪来的、谁在管。

## 先从一个反直觉的事实说起

ISO 的全称是 **International Organization for Standardization**（注意美式拼写 "Organization"，且最后一个词是 "Standardization" 而非 "Standards"）<RefLink :id="10" preview="ISO, About Us" />。缩写 ISO 并不来自英文名称——英语缩写应该是 IOS，法语是 OIN（Organisation Internationale de Normalisation）。创始人觉得 IOS 和 OIN 都不够好，选了希腊语 isos（平等）作为统一的缩写，这样不管什么语言都叫 ISO。这个冷知识跟 C++ 本身没什么直接关系，但它解释了为什么缩写和英文全称对不上。

::: details 参考资料原文
ISO 官网 "About us" 页面<RefLink :id="10" preview="ISO, About Us" />原文：

> "ISO, the **International Organization for Standardization**, brings global experts together to agree on the best ways of doing things."
>
> "Because 'International Organization for Standardization' would have different acronyms in different languages ('IOS' in English, 'OIN' in French for Organisation internationale de normalisation), our founders decided to give it the short form 'ISO'. ISO is derived from the Greek word isos (meaning 'equal')."

读者可访问 iso.org/about-us.html 自行验证。
:::

## 从 ISO 到 C++，中间到底隔了几层

ISO 下面并不是直接管 C++ 的。它先跟另一个组织 IEC（International Electrotechnical Commission，国际电工委员会）搞了一个联合体，叫 JTC1，全称 Joint Technical Committee 1，一号联合技术委员会，主管信息技术标准。

然后 JTC1 下面又有子委员会，叫 SC22（Subcommittee 22），全称是"编程语言及其环境和系统软件接口"。注意这个范围——不单单是编程语言，还包括"环境"和"系统软件接口"，所以 SC22 下面挂了一堆东西。

SC22 下面才是各个工作组（Working Group），也就是 WG。很多 WG 已经灰掉了——它们完成了历史使命，对应的语言标准定完了。但还活跃着的那些，一看名单：COBOL、Fortran、Ada、C、Prolog、Linux 相关的、编程语言漏洞研究，以及我们最关心的 C++。

C++ 在这里面是 WG21。为什么是 21 号？这个编号是历史分配的，没什么特别的含义，就是轮到它的时候恰好是这个号。

## 一个值得注意的事实

单从标准制定参与人数来看，C++ 的 WG21 在整个 SC22 里是体量最大的（据演讲者观察，如果按参与人数画比例图的话，其他语言的工作组可能就几个点，C++ 会把整张图撑满）。当然这不是说其他语言不重要，Fortran、Ada 这些在各自的领域（科学计算、航空航天）依然是不可替代的。但参与人数多也直接解释了为什么 C++ 标准化的速度和复杂度是这样的——提案多，讨论多，争议也多。

## 整条链路总结

从顶到底：ISO 和 IEC 联合成立 JTC1（一号联合技术委员会，管信息技术），JTC1 下设 SC22（第 22 子委员会，管编程语言及相关的东西），SC22 下设 WG21（第 21 工作组，专门管 C++）<RefLink :id="2" preview="ISO/IEC JTC1/SC22/WG21, Official Page" />。

完整的正式称呼是 ISO/IEC JTC1/SC22/WG21。

## 为什么搞清楚这条链路有意义

搞清楚这条链路之后，再看到提案文档上的 WG21 标识，我们就知道这是经过 ISO 框架下正式标准制定流程的东西，不是谁拍脑袋定的。"C++ 标准"这件事从一个模糊的概念变成了一个有具体组织架构支撑的实体。回头看看其实就是几层嵌套的委员会，没什么神秘的，但不知道的时候就是觉得云里雾里。

---

# 一个提案从想法到 C++ 标准的完整旅程

对"C++ 标准是怎么制定出来的"这件事，很多人的理解可能停留在"一群大佬开会然后拍板"的阶段。实际上整个流程是一套非常严谨的漏斗机制，层级不少，但每一步都有明确的职责边界。

## 先搞清楚 WG21 下面到底有什么

我们平时说"C++ 标准委员会"，指的就是 WG21。WG21 并不是一个扁平的大群，它下面还挂了一堆子组织，有管行政的，有管核心规范的，有管演进方向的，还有一堆我们在提案文档里经常看到缩写但可能不太清楚具体职责的 SG（Study Group，研究组）。这些研究组的状态不是一成不变的，有些是活跃且开放接纳新成员的，有些则是已经完成了历史使命、彻底结项的。不过要注意一个认知陷阱——看到"结项"就以为这个方向以后再也不会有人提了。结项只是说这个研究组本身不需要再存在了，它产出的结论可能已经被其他组接手，也可能暂时搁置了。最典型的例子就是 UB（Undefined Behaviour），相关的研究组虽然结项了，但关于 UB 的提案在各个组里依然大量存在，毕竟这是写 C++ 的人绕不开的痛。

## 一个想法从脑子里到标准里，到底要走多远

这部分是整个流程中最有意思的。一个关于 C++ 应该怎么改的想法，从脑子里到标准里，要走完一套完整的漏斗机制。

第一步，把想法写成一份正式的提案文档，然后发到一个叫 reflector 的邮件列表里。reflector 听起来很高深，实际上就是一个邮件列表，名字比较古老而已。提案发出去之后，会被路由到对应的研究组（SG）。在 SG 里面，这个领域的专家们会审阅、提反馈，然后作者回去改，改完再发，再讨论，反反复复地打磨。这个阶段本质上是在小范围里验证这个想法到底靠不靠谱。

当 SG 里的讨论基本成熟了，提案就需要"升级"，进入更广阔的视野去看它怎么融入整个 C++ 生态。这时候就分叉了——如果提的是一个库层面的特性（比如新增一个头文件里的工具），它会去 LEWG，也就是库演进工作组；如果提的是语言层面的特性（比如新的语法规则），它会去 EWG，也就是语言演进工作组。LEWG 和 LWG 的区别在于：LEWG 管"演进"，讨论这个特性值不值得做、怎么做更合理；而 LWG 是后面才轮到的"核心"组，负责具体的标准措辞。

在演进组里又会经历一轮打磨，当大家都觉得这个特性方向对了、细节也基本到位了，它才会从演进组流入核心组。库特性进 LWG，语言特性进 CWG。核心组做的事情非常硬核——他们要直接修改 C++ 标准文档，把提案翻译成精确到标点符号的规范性文字。

最后，假设所有环节的人都对这份修改满意了，提案就会进入全体投票环节。WG21 的全体成员一起投票，通过了之后，这个特性就会出现在下一个版本的 C++ 标准里。从想法到落地，可能要经历好几年的迭代。

## 整套流程的内核

搞清楚这套流程之后，提案文档上那些 SGxx、EWG、LWG 的缩写就不那么令人头疼了<RefLink :id="3" preview="ISO C++ Foundation, The Committee: WG21" />。再翻开一份提案，我们可以有意识去看它当前处于哪个阶段——如果还在 SG，说明还在早期探索，设计变数很大；如果已经到了 LWG/CWG，那基本意味着大方向定了，只剩措辞层面的精修了。

还有一个容易忽略的细节：提案从演进组（EWG/LEWG）流向核心组（CWG/LWG）这个动作，在委员会术语里叫"forward"。如果去读会议纪要，会经常看到"LEWG decided to forward Pxxxx to LWG"这样的句子，这里的 forward 是在说提案在流程里往下走了一步。

整个流程本质上是一套分层的同行评审机制——先在小圈子里验证可行性，再放到大圈子里看生态影响，最后由最严谨的人来定稿措辞。每一步都有明确的职责边界。虽然慢，但确实稳。

---

# C++ 标准化到底有多慢——跟其他语言横向对比

聊到 C++ 标准化的时间线，很多人的直觉是 C++23 应该是 2023 年出来的，C++26 就是 2026 年，但实际上 C++23 的技术工作在 2023 年初完成，ISO 正式出版则拖到了 **2024 年 10 月**（标准号 ISO/IEC 14882:2024）<RefLink :id="11" preview="ISO, ISO/IEC 14882:2024" />，C++26 的草案到目前还有一堆东西在讨论中，最终定稿大概率还会往后延。各个版本从启动到发布的时间跨度，比多数人想象中要长得多——这也是 C++ 标准化工程体量庞大的一个侧面。

::: details 参考资料原文
ISO 官方标准页面<RefLink :id="11" preview="ISO, ISO/IEC 14882:2024" />（iso.org/standard/83626.html）：

> Status: Published
> Publication date: **2024-10**
> Edition: 7
> Number of pages: 2104

isocpp.org/std/the-Standard<RefLink :id="3" preview="ISO C++ Foundation, The Committee: WG21" />：

> "The current ISO C++ standard is C++23, formally known as ISO International Standard **ISO/IEC 14882:2024(E)** – Programming Language C++."

读者可访问 iso.org/standard/83626.html 自行验证出版日期。
:::

那其他语言是怎么做的呢？每家走的路子差别很大，横向对比之后反而更能理解 C++ 为什么这么慢了。

先说 Rust。Rust 的理念跟 C++ 完全不一样。C++ 的模式是有一份 ISO 标准文档，写得极其详尽，然后 GCC、Clang、MSVC 这些编译器团队各自去实现这份文档，大家努力向标准看齐。Rust 基本上只有一个实现，就是 rustc。更准确地说，Rust 源代码仓库里的测试用例本身就是规范——"标准"是可执行的。写了一段代码，如果能通过 Rust 源码里那套测试，那它就是合法的 Rust 代码。C++ 领域里我们经常遇到"标准怎么说"和"编译器实际怎么做"不一致的情况，而 Rust 直接用测试用例把这个问题消解了。

这种模式带来的直接好处就是速度快。Rust 团队想加一个特性，改编译器代码，写好测试，提 PR，有人 review 通过，合并进去，下一个版本发布的时候所有人就能用上了。没有"三家编译器分别实现、进度不一样、有的支持有的不支持"这种问题。他们也有领导委员会和类似 RFC 的提案流程，但整体上比 C++ 轻量得多。"单实现 + 测试即规范"的模式，确实是 Rust 能保持六周一个发布周期的关键原因。

再看看 Python。在很长一段时间里，Guido van Rossum（Python 之父）扮演的是"仁慈的终身独裁者"的角色——语言往哪个方向走、哪些特性加、哪些不加，最终是他拍板的。比如争议很大的海象运算符 `:=`，就是在他任期内推进的。但到了 2018 年，Guido 自己退了，这背后反映的是一个现实问题：当语言社区大到一定程度，一个人来做最终决策会变得越来越吃力，社区内部的分歧也会越来越大。现在 Python 走的是社区治理模式，有一个五人指导委员会，提案机制叫 PEP（Python Enhancement Proposal），跟 C++ 的提案流程有几分相似，但明显没那么正式。他们努力做到每年发一个版本，也基本做到了。对比一下，Python 的流程比 C++ 轻量不少，但比 Rust 又重一些，处在中间位置。

最后说说 JavaScript。JavaScript 名义上背后有一个标准化组织叫 ECMA，JavaScript 在某些场合会被叫做 ECMAScript——这才是它技术上正式的名字。但实际体验上，JavaScript 的演进主要是被 V8 引擎（Chrome 背后的引擎）和 Node.js 生态推着走的，ECMA 标准更多是在事后追认"大家已经在用的东西"。这跟 C++ "先写标准、再实现"的路径几乎是反过来的。

把这些放在一起看，会形成一个很有意思的光谱。一端是 Rust 这种"实现即标准"、迭代极快的模式；中间是 Python 和 JavaScript，有标准化流程但相对轻量，实际推进力往往来自实现侧；另一端就是 C++，先写一份极其详尽的规范文档，然后多家编译器分别去实现，标准委员会本身不做任何实现。每种模式都有代价——Rust 的代价是基本没有编译器选择的自由；C++ 的代价就是一个特性从提案到能实际用上，可能要等上好几年。

C++ 标准化之所以慢，不是委员会的人不努力，而是"先定规范、多方实现"的框架本身就决定了它快不起来。至于这个代价值不值得，那就是另一个话题了。

---

# C++ 标准委员会的运作方式与社区参与

关于 C++ 标准化流程，坊间流传着不少说法——"C++ 标准被大厂把持了""提案都是供应商在暗中操纵"之类的观点时有出现。但实际了解下来，虽然确实有供应商的参与（毕竟实现编译器需要大量工程资源，投入了人自然就有话语权），背后是有正式的委员会流程的，提案要经过草案、投票、评审等多个阶段，并不是谁嗓门大谁说了算。流程相对轻量级，不像某些语言那样有极其严格的治理架构，但轻量不代表没有规矩。

我们也很容易觉得"隔壁的草更绿"，看到 Rust 那套 RFC 流程觉得特别规范特别透明，就吐槽 C++ 怎么不学学。但回头看，C++ 这种模式换来的是长久的生命力——这门语言从上世纪八十年代走到今天，经历了无数次技术浪潮的冲刷还活着，而且活得很好。每种治理模式都有它的取舍。

## 那些在幕后投入的人

标准委员会成员的投入程度常常被低估。很多参与提案的人把自己大量的个人时间——不是工作时间，是个人时间——都投入到了让这门语言变得更好这件事上。写提案、回应评审意见、在邮件列表里反复讨论细节、飞到世界各地参加面对面会议，这些大部分都没有额外报酬。CppCon 在夏威夷办过，有人回来之后说整个时间都待在酒店房间里过提案。还有那些赞助工程师参与标准化的公司，以及支持家人去参会的家庭——这些支撑体系是看不见的，但没有它们整个生态就转不起来。

## 线下会议的价值

据演讲者介绍，2025 年有 11 个大型的 C++ 国际会议，是历史上最多的一次。COVID 期间有个明显的低谷，但恢复得相当快，而且还在继续上升——这说明社区是活的。线上看演讲有它的价值，但线下和一群人坐在一起，茶歇的时候聊"你那个项目里 range-v3 用得怎么样""你踩过那个 MSVC 的坑吗"，这种信息密度和连接感是屏幕给不了的。

如果还在犹豫要不要参加一次线下的 C++ 会议或者 meetup，建议去试试，哪怕只是本地的半小时分享会。

---

## 线下聚会的实际形态

全球 C++ meetup 的数量，据 isocpp.org 登记的就已经超过了一百三十个，这意味着不管你住在哪里，方圆一百英里内大概率能找到一个。国内一线城市基本都有，二线城市也在慢慢出现。如果实在找不到，自己发起一个也完全没问题——不需要什么正式流程。有人就是在群里发个消息说"我周五晚上带着笔记本在某处坐着，聊 C++，有人来就来"，第一次去了四个人，后来稳定在十来个人，每个月一次，互相看代码、讨论问题。

更正式的形式也有：大公司赞助场地、请外部讲师做技术分享，有幻灯片和 Q&A；闪电演讲形式，每个人五到十分钟讲一个踩坑经历或小技巧，节奏很快，信息密度很高。甚至有些公司内部就有定期的技术交流时间。

线下聊天的一个实际好处是，讨论的技术方案和踩坑经验，很多是网上搜不到的——那些东西不够"成体系"，不值得写一篇博客，但恰恰是这种碎片化的、来自真实项目一线的经验，往往最管用。

---

# 线上社区与资源

很多人学 C++ 的前期都是一个人闷头折腾。遇到编译报错就自己搜，搜不到就换种写法绕过去。这种状态持续一段时间之后，往往会发现瓶颈不在努力程度，而在有没有找到正确的圈子。

## 线上社区

线上社区的氛围往往比想象中好得多。C++ Slack（由 C++ Alliance 运营）频道分得很细，可以按自己感兴趣的方向加入不同的频道。Discord 那边选择更多，Compiler Explorer 的 Discord 以及专门聊 C++ 标准提案的服务器都是活跃的讨论场所。新手和专家在同一个空间里交流，这在 C++ 社区里是真实存在的——刚学两个月的人在 Slack 的 `#beginners` 频道问指针问题，下面好几个人耐心解释；ISO 委员会成员在 Discord 里跟人讨论提案细节。

实际建议是：别一进去就问问题，先花几天时间潜水，看看别人怎么提问、怎么回答，熟悉社区的节奏。潜水过程中会学到很多东西。

## cppreference——社区驱动的参考文档

cppreference<RefLink :id="4" preview="cppreference.com, C++ Reference" /> 是社区驱动、社区运营的参考网站，上面每一个页面、每一个示例代码都是有人实际维护的。它不是某个大公司赞助的官方文档，而是一群志愿者在搞。正常情况下它可以由社区成员修改和补充，这也是它能保持高质量的原因——不是某一个人在写，是无数人在共同维护。每次查一个标准库组件的时候，顺手看一下页面底部的注释和讨论，经常能发现一些很有价值的信息，比如某个函数在特定编译器上的已知问题。

## 代码分享平台

除了实时聊天的社区，Compiler Explorer<RefLink :id="7" preview="Compiler Explorer, godbolt.org" /> 这样的代码分享平台在技术交流中极其重要。把代码放进去，生成一个链接，丢到任何地方——Discord、Slack、论坛、甚至直接发给同事。比起直接贴一大段代码文本，一个 Compiler Explorer 链接让别人点开就能直接看、直接改、直接跑，效率完全不一样。

调试问题时，先把最小复现代码放到 Compiler Explorer 上，确认能在多个编译器上复现，然后再去社区问——这样做的好处是别人帮你排查问题时不需要搭环境，直接点链接就能看到你看到的东西。

## 社区是 C++ 生态的核心

C++ 之所以让人着迷，不只是因为语言本身强大，更因为背后这群人。那些在开源项目里默默提交 patch 的人，花自己的时间维护 cppreference 的人，自费去组织线下聚会的人，在 Discord 里凌晨三点还在帮新手调试代码的人——是这些人构成了 C++ 的生态。去社区里泡着，看到的不仅是问题的答案，还有别人思考问题的方式、解决问题的思路，甚至对待技术的态度。

---

# 参与 C++ 社区——贡献不只有一种形式

关于"参与开源社区"，很多人有一个狭隘的理解——觉得那是有资格的人才能做的事，是在委员会里挂着名字的大佬、是写了知名库的作者才配谈的事。但实际上，参与的方式远比想象中多元。

## "贡献"比我们想象的更广泛

对 C++ 社区做贡献，不一定要写一个被广泛使用的库，不一定要向标准委员会提交一份提案然后被采纳。演讲里提到的参与方式，很多是现在就能做的：所在城市没有 C++ 聚会，自己发起一个就行——不需要是专家，只需要是一个愿意把人凑在一起聊 C++ 的人；参加一场会议，哪怕只是去听、去认识几个同样在用 C++ 的人，这本身就已经是在参与社区了；把自己踩过的坑写成一篇文章发出来，让后面的人少走弯路，这也是一种贡献。

## 关于站上舞台

演讲里有一个描述很真实——站上演讲舞台，回头看着无数张注视着你的脸，心里想着"我为什么又要做这种事"。做技术分享不需要讲得多完美，只需要讲你真正搞懂了的东西，讲你踩过的坑，这就足够有价值了。如果有机会做分享，哪怕心里紧张，也值得去试一次。

## 关于参与 C++ 委员会

C++ 委员会正在招人。委员会的工作需要各种层面的人参与——不只是语言设计层面的专家，也需要实际使用者的反馈，需要有人去测试提案、写用例、报告问题。不需要是 Bjarne Stroustrup 才能进去，有热情并且愿意投入时间就行。

## 最后一个小插曲

Q&A 环节里有个细节很真实：演讲者把 Barry Revzin 说成是负责 Ranges 的人，结果被现场纠正——Barry Revzin 近期在 C++26 Reflection 的应用层面做了很多工作（他在 CppCon 做过 "Practical Reflection With C++26" 的演讲），而 Ranges 的主要作者是 Eric Niebler（演讲者口误说成了 Eric Kneedler）。不过严格来说，Reflection 提案的主要推动者是 Daveed Vandevoorde 和 Herb Sutter 等人，Revzin 更多是在应用和教学层面。这种"把人名和负责领域搞混"的事情很常见，C++ 标准委员会涉及的人和子工作组太多了，就连经常参与的人也未必能全搞清楚。演讲者自嘲"我真是太糟糕了"，这种真实感反而让人觉得这个社区是很接地气的。

## 参与社区的门槛

C++ 社区不是某个封闭的圈子，它是由每一个正在用 C++ 的人组成的。最简单的贡献，可能就是把你今天学到的东西跟身边的同事分享一下，或者在社区里回答一个新手的问题。不必等到"足够强"才去参与——因为到那时候你可能已经忘了新手阶段的困惑，而恰恰是那些困惑，才是最有价值的分享内容。

---

# ARM32 条件码里的"永不执行"指令——正交性设计与它的消亡

这段 Q&A 问答环节涉及到一个有趣的架构设计问题。ARM32 指令集中每条指令前面都有四个 bit 的条件码字段，可以写 `ADDNE` 表示"如果不相等则加"，`MOVEQ` 表示"如果相等则移动"，不用单独写分支指令，代码密度很高。条件码里有一个 `AL`（Always，总是执行），对应 0b1110；但还有一个条件码，四个 bit 全是 1，也就是 0b1111，叫 `NV`，意思是"Never"。一个"从不执行"的指令——写进去不就是白占空间吗？

::: warning 重要更正
NV 条件码只在 **ARMv4 及更早版本** 中存在。从 ARMv5 开始，NV 被正式废弃，`0b1111` 编码被重新分配用于无条件指令扩展。在 ARMv7-A 上，使用条件码 `0b1111` 的行为是 **UNPREDICTABLE**，不再保证"永不执行"。本文后续的验证实验需要以 ARMv4 为目标架构才能得到预期结果。ARM 官方文档原文：

> "Every conditional instruction contains a 4-bit condition code field, the cond field, in bits 31 to 28. This field contains one of the values **0b0000 – 0b1110**."
>
> — ARM Architecture Reference Manual ARMv7-A/R, Section "The condition code field"<RefLink :id="5" preview="Arm Developer, Condition Codes: Conditional Execution" />

实际验证结果（arm-none-linux-gnueabihf-gcc 15.2 + qemu-arm-static）：

```bash
# ARMv4：NV 正常工作
$ arm-none-linux-gnueabihf-gcc -static -march=armv4 test.c && qemu-arm-static ./a.out
AL (always): result = 42
NV (never):  result = 0         # ← 符合预期，NV 跳过了 MOV

# ARMv7：直接触发 SIGILL（非法指令异常）
$ arm-none-linux-gnueabihf-gcc -static -march=armv7-a test.c && qemu-arm-static ./a.out
qemu: uncaught target signal 4 (Illegal instruction) - core dumped
```

验证代码见仓库：`code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-01-arm32-nv-condition.c`。
:::

## 正交性——ARM32 的设计哲学

关键在于 ARM32 的设计哲学：**极致的正交性**<RefLink :id="5" preview="Arm Developer, Condition Codes: Conditional Execution" />。所谓正交性，简单说就是"每个维度的选择都是独立的，可以自由组合"。在 ARM32 里，条件码这个维度被设计得非常彻底——每一种条件都有它的逻辑反面。等于（EQ）的反面是不等于（NE），大于等于（GE）的反面是小于（LT），无符号大于（HI）的反面是无符号小于等于（LS）……以此类推。

那"总是执行"（AL）的逻辑反面是什么？当然是"从不执行"（NV）。

因为四个 bit 能表示 16 种状态，条件码的设计者把所有 16 种状态都填满了，每一种都有对应的语义。这不是"故意留个没用的"，而是正交性推到极致之后的必然结果——不可能只保留 15 种而空着一种不管，那样就不正交了。代价就是：在 ARM32 的全部指令编码空间里，整整十六分之一的编码，对应的全是"什么都不做"的指令。这是设计取舍——用一点空间浪费换取了指令集在概念上的完美对称。

这个设计在最初的 ARM（ARMv1 到 ARMv4）中确实如此。但 ARM 的后续版本证明了"正交到极致"本身也有代价。

## 动手验证：写一条"永不执行"的指令（ARMv4）

我们可以亲手验证一下这个东西<RefLink :id="6" preview="Arm Developer, Condition Codes: Condition Flags and Codes" />。因为 NV 条件码只在 ARMv4 及更早版本中有效，我们需要明确指定架构版本。

::: details 为什么不能用 ARMv7？
ARMv7-A 的有效条件码范围仅为 `0b0000`–`0b1110`。编码 `0b1111` 在 ARMv5+ 中被重新分配——它要么被解释为完全不同的指令（利用条件码位来扩展操作码空间），要么产生 UNPREDICTABLE 行为。在 ARMv7 上用 `.word 0xf3a0002a`，**不能保证**结果是"永不执行"。验证代码已放在仓库中（`code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-01-arm32-nv-condition.c`），读者可以自行在 ARMv4 和 ARMv7 目标上对比测试。
:::

环境是 Arch Linux WSL，交叉编译工具链用的 `arm-none-linux-gnueabihf-gcc`（Arm GNU Toolchain 15.2）。注意编译时需要用 `-march=armv4` 来确保 NV 条件码的语义：

先写一个最简单的 C 文件：

```c
// test_nv.c
void foo(void) {
    __asm__ volatile("mov r0, #42");
}
```

编译成汇编看看正常的 `MOV` 长什么样（注意这里我们用 `-march=armv4`）：

```bash
$ arm-none-linux-gnueabihf-gcc -S -O0 -march=armv4 test_nv.c -o test_nv.s
$ cat test_nv.s
    .arch armv4
    .file   "test_nv.c"
    .text
    .align  2
    .global foo
    .arch armv4
    .type   foo, %function
foo:
    push    {r7}
    sub     r7, sp, #0
    mov     r0, #42
    nop
    pop     {r7}
    bx      lr
    .size   foo, .-foo
    .ident  "GCC: (Ubuntu 12.3.0-1ubuntu1~22.04) 12.3.0"
```

现在我们手动构造一条"永不执行"的 `MOV`。ARM32 的 `MOV` 指令编码格式里，高四位就是条件码。正常的 `MOV R0, #42` 的机器码，可以用 `objdump` 看一下：

```bash
$ arm-none-linux-gnueabihf-gcc -c -march=armv4 test_nv.c -o test_nv.o
$ arm-none-linux-gnueabihf-objdump -d test_nv.o

test_nv.o:     file format elf32-littlearm

Disassembly of section .text:

00000000 <foo>:
   0:   e52db004        push    {r7}
   4:   e24db000        sub     r7, sp, #0
   8:   e3a0002a        mov     r0, #42     ; 注意这里：0xe3a0002a
   c:   e320f000        nop
  10:   e49db004        pop     {r7}
  14:   e12fff1e        bx      lr
```

看到 `0xe3a0002a` 了吗？高四位是 `0xe`，也就是二进制的 `1110`，对应条件码 `AL`（Always）。现在把高四位从 `1110` 改成 `1111`，也就是从 `0xe3a0002a` 变成 `0xf3a0002a`。在 ARMv4 上，这就是一条"永不执行"的 `MOV R0, #42`——它被解码了，CPU 认识它是一条 MOV 指令，但因为条件码是 NV，所以永远不会真正执行。

::: warning 再次提醒
这条指令只在 ARMv4 及更早版本上表现为"永不执行"。如果在 ARMv5+（包括 ARMv7-A）上执行 `0xf3a0002a`，行为是 UNPREDICTABLE 的。
:::

用 `.word` 直接塞机器码进去验证：

```c
// test_nv2.c
#include <stdio.h>

void foo(void) {
    int result = 0;
    // 正常的 MOV R0, #42，条件码 AL (0xe)
    __asm__ volatile("mov r0, #42" : "=r"(result));
    printf("AL (always): result = %d\n", result);

    result = 0;
    // 手动塞入条件码 NV (0xf) 的同一条指令
    // 0xf3a0002a = MOVNV R0, #42  (ARMv4 only!)
    __asm__ volatile(".word 0xf3a0002a" : "=r"(result));
    printf("NV (never):  result = %d\n", result);
}

int main(void) {
    foo();
    return 0;
}
```

编译运行（注意 `-march=armv4`）：

```bash
$ arm-none-linux-gnueabihf-gcc -march=armv4 test_nv2.c -o test_nv2 -static
$ qemu-arm-static ./test_nv2
AL (always): result = 42
NV (never):  result = 0
```

`result` 还是 0——那条 `MOV R0, #42` 被完整地解码了，但 CPU 看了一眼条件码是 `NV`，直接跳过，什么都不做。`result` 保持了之前的值 0。

这里有个容易踩的坑：如果没加 `=r`(result) 的输出约束，编译器可能直接把 `result` 优化掉，怎么跑都是 0，很容易误以为是机器码写错了。

## 顺便一提：TEQ 指令

问答里还提到了一条叫 `TEQP` 的指令。`TEQ` 本身是"Test Equivalence"的缩写，做的是异或操作并设置标志位，用来比较两个值是否相等（不改变寄存器的值，只改标志位）。带 `P` 后缀的 `TEQP` 是旧版 ARM（ARMv4 之前）用于直接操作处理器状态寄存器（PSR）的指令——在现代 ARM 中已被 `MSR`/`MRS` 指令替代。

## 小结

ARM32（ARMv4 及更早）里那十六分之一的"空操作"指令编码，不是 bug，不是遗留问题，而是一个极致正交设计带来的必然副产品。设计者选择了概念上的完美对称，代价就是浪费了一些编码空间。

但 ARM 自己的后续演进也说明了一切：ARMv5 废弃了 NV 条件码，回收了 `0b1111` 的编码空间；ARM64（AArch64）则彻底砍掉了条件码字段。"正交到极致"在概念上很美，但 ARM 的实践证明，在实际演进中，编码空间和指令集简洁性最终战胜了概念上的完美对称。了解这段设计历史之后，再看汇编手册的体验会完全不同。

---

# 学汇编到底该看 x86 还是 RISC-V

在 Compiler Explorer 上折腾的时候，经常会纠结一个问题：x86 汇编看起来像天书——`mov rax, qword ptr [rdi + 8]`，寄存器名字又长又没规律；换成 RISC-V 看起来好懂不少，寄存器就是 `x0` 到 `x31`，指令格式也规整得多。但看 RISC-V 汇编跟实际工作里跑的 x86 代码到底有多大差距？会不会看了半天白看？

## 结论：看什么架构，取决于优化级别

这个事情没有一刀切的答案，关键在于 Compiler Explorer 里选的优化级别。如果开的是 `-O0`（无优化），那看 x86 还是 RISC-V 区别不大。编译器在 `-O0` 下做的事情非常"通用"——老老实实把 C++ 语句一条条翻译成机器指令，该压栈压栈，该存内存存内存，不管什么架构都是这个套路。在这个级别下学到的"编译器把代码变成了什么样"，在不同架构之间确实是可互换的知识。

用一段简单的函数验证一下：

```cpp
int add_and_double(int a, int b) {
    int sum = a + b;
    return sum * 2;
}
```

在 `-O0` 下，x86 和 RISC-V 的输出虽然指令不同，但"味道"是一模一样的——都是先把参数存到栈上，再从栈上加载回来做加法，结果再存回栈上，最后再加载出来做乘法。编译器在无优化时很老实，不会做任何聪明的事情，这个认知跟架构无关。

## 到了 -O2 以上，事情就不一样了

当优化级别拉到 `-O2` 甚至 `-O3` 之后，不同架构之间的差异开始系统性地显现。你看到的汇编已经不纯粹是"编译器的通用优化策略"了，里面混入了大量"针对这个架构特定指令集的专门优化"。

举一个典型的例子——统计一个整数里有多少个 1 的 popcount：

```cpp
int count_ones(unsigned int x) {
    int count = 0;
    while (x) {
        count += x & 1u;
        x >>= 1;
    }
    return count;
}
```

这段代码在 `-O2` 下丢到 x86 的 Compiler Explorer 里，编译器直接把它替换成了一条 `popcnt` 指令。整条循环没了，函数体就一条指令。但切到 RISC-V——循环还在。RISC-V 的基础指令集里没有 `popcnt` 这条指令（虽然某些扩展里有），所以编译器没法做这种替换，只能老老实实地用循环或者查表法来优化。同样的 C++ 代码，同样的 `-O2`，两个架构给出的汇编完全是两回事。

如果在 RISC-V 上学汇编，可能会得出结论"编译器不会自动识别 popcount 模式"；在 x86 上学会得出完全相反的结论。到底谁对？都对，也都不对——因为这不是编译器能力的差异，而是目标架构指令集的差异。

## 实际策略

总结一下策略：如果学汇编的目的是理解"编译器的高层优化决策"——内联怎么做的、常量传播怎么做的、死代码消除怎么做的——那看哪个架构都行，因为这些确实是跨架构通用的。编译器决定"要不要内联这个函数"的时候，考虑的是函数大小、调用频率、有没有副作用这些高层面的东西，跟底下跑在什么 CPU 上关系不大。

但如果目的是理解"编译器最终生成的指令到底长什么样"，那最好看实际工作里用的那个架构。到了 `-O2` 以上，看到的每一条指令都可能是一条"架构专属捷径"，换到另一个架构上可能根本不存在对应的指令。

## Compiler Explorer 的 AI 功能

Compiler Explorer 上线了 AI 辅助解释汇编的功能，体验参差不齐。对于简单的指令序列——基本的函数调用约定、栈帧布局这些——AI 解释得还挺清楚的。但遇到架构特定的优化手段，比如 x86 上用 `cmov` 做条件移动来避免分支预测失败，AI 有时候会给出比较泛泛的解释，没有点出"这到底是在针对什么架构特性做优化"。可以把它当入门拐杖，但别当权威答案。

## 小结

之前常有人说"学汇编就得选一个最干净的架构来入门"，但干净到跟实际工作脱节，反而会形成错误认知。不如一开始就面对实际的 x86 汇编，虽然学习曲线陡一点，但学到的东西每一条都能直接用上。RISC-V 很适合用来"验证通用优化逻辑"——同一个代码在两个架构上都跑一遍，如果某个优化在两边都出现了，那它大概率是编译器的通用策略；如果只在一个架构上出现，那它大概率是架构特定的指令替换。这个对比方法比单独看一个架构的输出清晰多了。

---

# 重新理解"手写汇编"——什么时候该碰，什么时候不该碰

关于汇编，存在两种常见的极端态度：一种觉得"编译器帮你处理了，不用管汇编"，另一种觉得"关键路径上不手写内联汇编就不算真正懂 C++"。这两种态度都不对。演讲者说了一句话很到位：他现在写汇编，主要是给喜欢的老电脑写代码，因为那些架构的汇编还比较容易处理，你可以把所有东西都装进脑子里。汇编的价值不在于"比编译器更聪明"，而在于"完全理解这台机器在做什么"。

## 现代 x86-64 汇编为什么难"装进脑子里"

对比一下不同时代的指令集就明白了。现在 x86-64 光是 `mov` 指令的编码变体就有几十种——`mov rax, imm32` 符号扩展到 64 位、`mov r/m64, imm32` 也是符号扩展、`movzx`、`movsx`、`cmovcc` 条件移动……再加上 AVX-512 那一套 EVEX 编码前缀、掩码寄存器、广播机制。一个正常人要把 x86-64 的完整指令集"装进脑子里"基本是不可能的任务。

演讲者提到了 Hitachi SH4 的指令集，说那可能已经是一个正常人能做到的极限了。SH4 是 1990 年代末的 RISC 处理器，16 位定长指令编码，寻址模式很简洁。对比一下就能理解为什么老硬件上的汇编体验完全不一样——那是一种"人脑可以完整掌握"的指令集，而 x86-64 经过四十多年的向后兼容累加，已经变成了一头没有人能完全看懂的巨兽。不是"汇编"本身难，而是 x86-64 这个特定平台的汇编难。

## 现代 C++ 开发者什么时候该碰汇编

演讲者讲了一个真实的案例：他去过的某家公司遇到编译器在一个绝对的热点循环里不停地溢出寄存器（register spilling），怎么调优化选项都搞不定，最后团队直接手写了整个循环的汇编版本，然后维护一个 C++ 版本和一个汇编版本做交叉验证。这听起来很累，但确实是一种非常务实的工程决策。内联汇编在 GCC 和 Clang 之间的语法不统一，而且很难精确控制编译器周围的寄存器分配状态——有时候你要的就是"这段代码的寄存器使用完全由我说了算"，那独立的汇编文件反而是最干净的做法。

不过手写汇编的维护成本很高。在敏捷开发环境下，需求一变，手写好的汇编可能需要完全重写。这种痛苦在手写 SIMD intrinsics 时也会遇到——精心设计了一个用 4 个 `__m256i` 寄存器的循环，结果需求变了，数据结构多了一个字段，寄存器分配直接崩盘。

所以判断标准比较清晰：除非遇到了编译器确实搞不定的极端热点，而且这个热点的性能瓶颈已经被 profiling 确认是寄存器溢出或者指令序列问题，并且这个热点足够稳定、不太会频繁变动——只有这三个条件同时满足，手写汇编才是值得的。否则，老老实实写 C++，让编译器干活。

## 学汇编的真正价值——看懂编译器输出

学汇编最大的价值不是让你去写它，而是让你能看懂编译器输出了什么。举个具体的例子：

```cpp
// 统计 buf 中字符 ch 出现的次数（已知长度）
size_t count_char(const char* buf, size_t len, char ch) {
    size_t count = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == ch) count++;
    }
    return count;
}
```

这个函数简单到不能再简单。但把它丢到 godbolt 上用 `-O3 -march=x86-64-v2` 编译之后，编译器（GCC 16）把它自动向量化了，用了 SSE 指令，一次比较 16 个字节。如果不懂汇编，根本不知道编译器做了这件事，可能还会自己去手写 SIMD 优化。

::: warning 原文错误更正
原版示例使用的是 `while (*str)` 以空字符结尾的循环。实际上，**GCC 和 Clang 在 `-O2` 和 `-O3` 下都不会对这种模式自动向量化**——因为空字符的位置是运行时才确定的，编译器无法安全地一次读取 16 个字节（可能越过空字符读到未映射内存）。只有已知长度的版本（`for (i < len)`）才会被向量化。

读者可以用以下命令自行验证（环境：GCC 16.1.1, x86-64）：

```bash
# while(*str) 版本 — 不会被向量化
cat > /tmp/test.cpp << 'EOF'
#include <cstddef>
__attribute__((noinline))
size_t f(const char* s, char c) {
    size_t n = 0; while (*s) { if (*s == c) ++n; ++s; } return n;
}
EOF
g++ -O3 -march=x86-64-v2 -S /tmp/test.cpp -o /tmp/test.s
grep pcmpeqb /tmp/test.s   # 无输出 = 没有向量化
```

验证代码见仓库：`code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-04-count-char-vec.cpp`。
:::

真实的 GCC 输出（简化后的核心循环）：

```asm
# GCC 16 -O3 -march=x86-64-v2 输出的核心循环（简化）
count_char:
    movd    %r8d, %xmm4            # ch 放入 XMM4 最低字节
    pxor    %xmm2, %xmm2           # XMM2 = 全零（用于 pshufb 广播掩码）
    pshufb  %xmm2, %xmm4           # 广播 ch 到 16 个字节
                                    # pshufb 零掩码：每个字节取 src[0]，即广播最低字节
    pxor    %xmm2, %xmm2           # count 累加器清零
.L4:
    movdqu  (%rax), %xmm0          # 加载 16 字节
    pcmpeqb %xmm4, %xmm0           # 逐字节比较，匹配的位置 0xFF，不匹配 0x00
    pmovsxbw %xmm0, %xmm6          # 符号扩展低 8 字节 → 8 个 word
    pmovsxwd %xmm6, %xmm5          # 符号扩展 → 8 个 dword
    pmovsxdq %xmm5, %xmm5          # 符号扩展 → 4 个 qword（每个值为 0 或 -1）
    # ... 将 -1 转为 +1 并累加到 xmm2 ...
    paddq   %xmm1, %xmm2           # 累加到计数器
    cmpq    %rax, %rcx              # 循环是否结束
    jne     .L4
```

::: details 原文汇编为什么是错的？
原文声称 GCC 使用 `punpcklbw` 来广播字节——这是错的。`punpcklbw` 的功能是**交错合并**两个寄存器的低位字节（byte → word），不是广播。GCC 实际使用 `pshufb`（带零掩码的 PSHUFB）来广播：当掩码全零时，每个位置都取 `src[0]`，效果就是将最低字节复制到所有 16 个位置。

另外，原文声称用 `pmovmskb` + `popcnt` 来计数——GCC 实际用的是符号扩展链（`pmovsxbw` → `pmovsxwd` → `pmovsxdq`），把匹配结果的 0xFF/-1 通过符号扩展变成 qword 值，然后累加。这种策略在某些场景下比 `pmovmskb`+`popcnt` 更优（特别是当需要进一步 SIMD 处理计数结果时）。
:::

看到 `pcmpeqb` 和符号扩展链就能判断：编译器在这个 case 下已经做得很好了，不需要手动优化；但如果在另一个更复杂的场景下发现编译器没有自动向量化，也能通过看汇编输出定位到"它卡在哪里了"。学汇编的真正价值——让你拥有了"审计编译器"的能力。不是要替代编译器，而是要能看懂它的输出。

## 实践方式：看汇编比写汇编多得多

每次写完一个可能有性能问题的函数，先把编译器的输出看一遍——丢到 godbolt 上，开 `-O2` 或者 `-O3`，然后看几个关键指标：有没有不必要的内存访问（比如某个变量预期应该在寄存器里但编译器却反复从栈上加载，可能是因为用了 `volatile` 或者有别名问题）；循环有没有被向量化（如果循环体很简单但编译器没向量化，看看是不是有数据依赖或者分支）；函数调用有没有被内联（如果没有，是不是因为函数太大或者用了什么阻止内联的东西）。

这些判断全部建立在"能看懂汇编"的前提下。不需要能从零手写一段完美的汇编，只需要能看懂 `mov`、`load`、`store`、`cmp`、`jmp`、`call`、`ret` 这些基本指令，能看出数据流向就够了。

## 在简单架构上写汇编的价值

演讲者说他不怀念手写汇编的辛苦，但确实怀念那种智力上的挑战。在某个简单的架构上真正从零写过汇编，能帮你建立一种"机器思维"——写 C++ 的时候脑子里会不自觉地有一个模型：这行代码大概会生成什么样的指令？这个对象在内存里是怎么布局的？这个虚函数调用会走几次间接寻址？这种直觉在做性能优化时会发挥巨大作用。

## 小结

学汇编不是用来替代编译器的工具，也不是高不可攀的黑魔法。它是一种让人能看懂机器在做什么的能力，而获得这种能力最好的方式，可能不是在 x86-64 上死磕，而是找一个简单的、人脑能装下的架构，真正动手写一写。日常工作里的原则很简单：看汇编，多看；写汇编，慎写。除非真的遇到了编译器搞不定的场景，而且确信手写能带来显著提升，并且这段代码足够稳定不会频繁改动。否则，让编译器干活，你来审计它。

---

# 如何吸引新人进入 C++

CppCon 上也在认真思考一个问题：当一个 CS 毕业生从来没写过 C++，该怎么把人拉进来？演讲者提到一个观察：他小时候家里那些设备，打开之后唯一能做的事情就是输入点什么，然后靠耳濡目染去搞明白怎么回事。没有太多选择的时候，反而会深入地去折腾。而现在选择太多了，一个大学生完全可以念完四年计算机科学，用 Python 把作业交了、用 React 把毕设做了，从头到尾不需要知道什么是栈、什么是堆、什么是未定义行为。

但演讲者后面说的话更值得注意：他在 Google 遇到一些新毕业生，这些人明明在"高层"语言环境里长大，却自己在钻研底层硬件。这说明对底层的好奇心不是某个年代特有的，它一直都在，只是触发的方式变了。

把新人引入 C++，可能不应该从"你应该学 C++ 因为它很重要"这种说教开始，而是要找到每个人心里的那个触发点——某天突然遇到了一个 Python 解决不了的性能问题，或者突然想搞明白"程序到底是怎么跑到硬件上去的"，那就是最好的时机。我们这些"后来者"其实有一个优势：我们知道从高层语言跌落到 C++ 时哪些地方最痛，这种"从疼到通"的体验恰恰是可以分享给下一个新人的。

---

# 预处理器的逐步退场——C++ 的渐进替代之路

Matt Godbolt 在 Q&A 中被问到"如果可以移除一个特性，你想干掉什么"，他的答案是预处理器。这并不是一时兴起的想法——从 C++11 开始，这个语言就一直在做同一件事：把"预处理器时代"的东西用"真正的 C++"重新实现一遍。

## 预处理器的典型问题

早期的 C++ 项目里，满屏的 `#define`、`#ifdef` 和条件编译嵌套是很常见的。以日志宏为例：

```cpp
// 我 2022 年的写法，现在看着想打自己
#define LOG(level, msg) \
    do { \
        if (level >= g_log_level) { \
            printf("[%s:%d] %s\n", __FILE__, __LINE__, msg); \
        } \
    } while(0)

#define LOG_DEBUG(msg) LOG(0, msg)
#define LOG_INFO(msg)  LOG(1, msg)
#define LOG_ERROR(msg) LOG(2, msg)
```

这种写法的问题在于：宏是文本替换，根本不理解 C++ 的类型系统。传一个带逗号的表达式进去，比如 `LOG_DEBUG(func(a, b))`，预处理器会把它当成两个参数，直接编译报错。而且错误信息还特别离谱，因为报错的位置是展开后的代码，跟写的宏完全对不上。

## 现代替代方案：用 C++ 替代文本替换

用 `constexpr`、`inline` 函数和模板来替代宏，效果完全不同：

```cpp
// log.hpp
#pragma once
#include <iostream>
#include <source_location>

enum class LogLevel { Debug = 0, Info = 1, Error = 2 };

inline LogLevel g_log_level = LogLevel::Info;

// 用 constexpr 函数替代宏，类型安全，支持任意参数
template <typename... Args>
void log(LogLevel level, const std::format_string<Args...> fmt, Args&&... args,
         const std::source_location& loc = std::source_location::current())
{
    if (static_cast<int>(level) >= static_cast<int>(g_log_level)) {
        std::cout << std::format("[{}:{}] {}\n",
                                 loc.file_name(), loc.line(),
                                 std::format(fmt, std::forward<Args>(args)...));
    }
}

// 用 inline constexpr 变量替代宏常量
inline constexpr LogLevel log_debug = LogLevel::Debug;
inline constexpr LogLevel log_info  = LogLevel::Info;
inline constexpr LogLevel log_error = LogLevel::Error;
```

```cpp
// main.cpp
#include "log.hpp"

int main() {
    g_log_level = LogLevel::Debug;
    
    // 这样调用，带逗号的表达式完全没问题
    log(log_debug, "value is {}", std::max(1, 2));
    log(log_info, "program started");
    log(log_error, "something went wrong: code={}", 404);
}
```

你可能会问，`__FILE__` 和 `__LINE__` 怎么办？这正是 C++20 的 `std::source_location` 要解决的事情——它是一个"真正的 C++ 特性"，不是预处理器黑魔法，编译器能正确理解它，调试的时候也能拿到准确的信息。

## `#include` 的替代：Modules

预处理器最根深蒂固的存在感来自 `#include`。C++20 引入了 modules<RefLink :id="8" preview="cppreference.com, Modules (since C++20)" />，从根基上动摇预处理器的地位。看一个最简单的例子：

```cpp
// math_utils.cppm —— 这是一个模块接口文件
export module math_utils;

export int square(int x) {
    return x * x;
}

export double pi() {
    return 3.14159265358979;
}
```

```cpp
// main.cpp
import math_utils;
#include <iostream>

int main() {
    std::cout << "5^2 = " << square(5) << "\n";
    std::cout << "pi = " << pi() << "\n";
}
```

编译的时候需要注意，modules 的支持在不同编译器上进度不一样。我用的是 GCC 14，编译命令大概是这样：

```bash
g++-14 -std=c++20 -fmodules-ts math_utils.cppm main.cpp -o demo
```

跑一下看看：

```text
5^2 = 25
pi = 3.14159
```

关键区别在于：`math_utils` 这个模块只会被编译一次，不管 `import` 多少次。传统的 `#include` 是把头文件内容在每个翻译单元里都复制粘贴一遍，这就是为什么大项目编译慢——同一个 `<vector>` 被处理了几百次。

不过 modules 现在踩坑的地方还不少，主要是模块和传统头文件的互操作问题。如果 `import` 一个模块，但这个模块内部 `#include` 了传统头文件，然后另一个地方又 `#include` 了同一个头文件，某些编译器会报很奇怪的错误。所以建议是：要么全用 modules，要么别用，别混着来，至少在工具链成熟之前是这样。

## 条件编译怎么办

`#ifdef` 的替代目前没有完美方案。C++20 的 `consteval` 和 `if constexpr` 能解决一部分问题，但前提是条件在编译期能确定。

::: warning 原文错误更正
原版示例使用了 `reinterpret_cast` 来判断字节序，但 `reinterpret_cast` 在 C++ 标准中**不允许出现在常量表达式求值中**（[expr.const]<RefLink :id="12" preview="cppreference.com, Constant expressions" />），因此 `consteval` 函数中不能使用它。GCC 16.1.1 的实际报错信息如下：

```text
/tmp/test.cpp:4:12: warning: 'reinterpret_cast' is not a constant expression [-Winvalid-constexpr]
    4 |     return reinterpret_cast<const char*>(&test)[0] == 1;
      |            ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/tmp/test.cpp:7:34: error: call to consteval function 'is_little_endian()' is not a constant expression
```

验证代码见仓库：`code/volumn_codes/vol10/cppcon/2025/02-some-assembly-required/05-02-consteval-endian-broken.cpp`（编译失败）和 `05-03-consteval-endian-fixed.cpp`（修正版，编译通过）。读者可以用 `g++ -std=c++20 05-02-consteval-endian-broken.cpp` 自行验证编译失败。
:::

修正后有两种编译期判断字节序的方法：

```cpp
// 方法 1：编译器内置宏（推荐，简洁可靠）
consteval bool is_little_endian() {
    return __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
}

// 方法 2：使用 std::bit_cast（C++20，可在 constexpr/consteval 中使用）
#include <array>
#include <bit>
consteval bool is_little_endian_bitcast() {
    // std::bit_cast 可以在常量表达式中使用，而 reinterpret_cast 不行
    constexpr auto bytes = std::bit_cast<std::array<unsigned char, sizeof(int)>>(1);
    return bytes[0] == 1;
}

void write_bytes(int value) {
    if constexpr (is_little_endian()) {
        // 小端序的处理逻辑
        std::cout << "little endian path\n";
    } else {
        // 大端序的处理逻辑
        std::cout << "big endian path\n";
    }
}
```

但如果是真正的平台检测（Windows vs Linux），目前还是得靠预处理器定义的宏。这也是为什么 Matt 说的是"让预处理器变得越来越不重要"而不是"明天就删掉它"——这是一个渐进的过程。

## 整体趋势

从 C++11 开始，这个语言一直在做同一件事——把"预处理器时代"的东西用"真正的 C++"重新实现一遍。`constexpr` 替代宏常量，`inline` 函数替代宏函数，`template` 替代类型无关的宏，`source_location` 替代 `__FILE__`/`__LINE__`，`modules` 替代 `#include`，`if constexpr` 替代部分 `#ifdef`……每一步都不起眼，但加在一起就是一个清晰的方向。预处理器不懂 C++，它只会剪贴板，所以才会出那么多离谱的报错。而模板、`constexpr`、`if constexpr` 这些东西是 C++ 的一部分，编译器能真正理解你在干什么。

预处理器不会明天就消失，但作为写 C++ 的人，我们可以主动减少对它的依赖。每次想写 `#define` 的时候停下来想一想：这个东西有没有类型安全的 C++ 替代方案？大部分时候，答案是有的。

---

# 怎么判断离奇的汇编输出到底是优化还是 UB

在 Compiler Explorer 上看自己写的 C++ 代码对应的汇编时，经常看到一些完全看不懂的指令序列——这到底是编译器聪明到你看不懂了，还是你写了什么 UB 导致编译器在合法地"发疯"？很长时间里这是一个很难回答的问题。

## 最直接的信号：陷阱指令

有一个特别明显的红旗，就是看到 `UD2` 这条指令。它的全称是 Undefined Instruction，执行结果只有一个：CPU 直接抛出非法指令异常，程序当场崩溃。编译器放这条指令的意思是："正常情况下不可能执行到这里，如果真的到了，就让程序死掉吧。"

最典型的场景就是 switch 语句：

```cpp
#include <cstdint>

int32_t classify(int32_t value) {
    switch (value) {
        case 0:  return 1;
        case 1:  return 2;
        case 10: return 3;
        case 11: return 4;
    }
    // 我当时觉得：如果不是上面这四个值，就返回 0 吧
    return 0;
}
```

这段代码逻辑上看起来很完整，每个分支都有 return，最后还有一个兜底的 return 0。但是把优化开到 `-O2`，去看 GCC 或者 Clang 生成的汇编，可能会在 switch 的跳转表之后看到一条 `UD2`。编译器在做值范围分析的时候，如果它发现调用方传入的值已经被约束在了一个有限的集合里，它就可能推断出最后那个 return 0 永远不会被执行。这时候它就不会为 return 0 生成正常的返回代码，而是直接放一条 `UD2` 当作"此路不通"的标记。所以如果在汇编里看到 `UD2`，而且确信代码逻辑上存在一条合理的路径能走到那里，那基本可以断定：你和编译器对程序行为的理解出现了分歧，而这往往意味着 UB。

## 更多时候没那么明显

不是所有 UB 都会以 `UD2` 的形式出现。很多时候编译器遇到 UB 之后，直接基于"这个情况不会发生"的假设去做激进的优化，结果就是生成一段看起来完全匪夷所思的指令序列。

```cpp
#include <cstddef>

int sum_array(const int* arr, size_t n) {
    int sum = 0;
    for (size_t i = 0; i <= n; ++i) {  // 注意这里是 <=
        sum += arr[i];
    }
    return sum;
}
```

这个循环里 `i <= n` 意味着会访问 `arr[n]`，已经越界了一个元素。在没开优化的情况下，这段代码可能"看起来正常工作"，因为越界读到的内存位置恰好有某个值，程序不会立刻崩溃。但一旦开了 `-O2`，编译器可能基于"数组越界是 UB"的假设把整个循环的逻辑改得面目全非。这时候去看汇编，看到的不是 `UD2`，而是一段"看起来在干活但结果肯定不对"的指令。根本无法从汇编本身判断出"这是因为 UB"，只能靠经验去怀疑。

## 排查思路

第一步，先看有没有陷阱指令。如果看到了 `UD2` 或者目标架构上等价的陷阱指令（比如 ARM 上的 `UDF`），那直接锁定：编译器在说这里有不可达路径，去查为什么编译器认为不可达。

第二步，如果没有陷阱指令，但汇编看起来不对劲——比如循环次数明显少了、某些变量完全消失了、或者出现了完全没写过的计算逻辑——那就开始怀疑 UB。用 `-fsanitize=undefined` 重新编译一次<RefLink :id="9" preview="GCC Documentation, Program Instrumentation Options" />，看看运行时会不会报错。这个工具在揪出有符号整数溢出、空指针解引用、数组越界这些 UB 方面非常有效。

第三步，如果 sanitizer 也没报错，那可能真的是编译器做了一个没预料到的合法优化。去查编译器的控制流图，Compiler Explorer 里可以打开这个视图，看看基本块之间的跳转关系是否和预期一致。

最后，如果以上都搞不定，那就把那条看不懂的指令丢进搜索引擎，看看别人有没有遇到过类似的情况。

## 没有银弹

没有什么一招鲜的方法能让你看一眼汇编就知道"这是优化还是 UB"。它更像是一个积累经验的过程，见过的 UB 模式越多，看汇编时的直觉就越准。`-fsanitize=undefined` 和陷阱指令是最可靠的两个锚点，其他的就是靠查、靠看控制流图、靠反复对比不同优化级别下的输出来推理。看汇编更像是一种调试手段，不需要每条指令都认识，但需要有识别出"这里不对劲"的能力，然后有系统的方法去缩小范围。

---

# 编译器"聪明"与 UB 的模糊边界

UB 和非 UB 之间并不总是有一条清晰的线。当你把优化等级拉到 -O2 甚至 -O3 之后，很多时候分不清编译器到底是在"聪明地帮你优化"，还是在"合法地搞坏你的代码"。

## "跑得对就是没 UB"——一个常见的误解

对 UB 有一个常见的朴素理解：只要程序跑出了正确结果就没问题。道理上"UB 就是 UB，不管现在跑不跑得对，编译器都有权做任何事"——但这真正让人"开窍"的往往不是别人讲道理，而是自己被坑了一次。

典型的场景：先用 `new` 分配一块内存，用一个 `unsigned char*` 指针遍历做初始化，然后用一个 `float*` 指针去读写。Debug 模式下跑得好好的，一到 Release 模式输出直接花屏。这是严格别名规则的问题——编译器在 -O2 下看到 `float*` 去读那块内存，就认为"这个指针和之前的 `unsigned char*` 没有关系"，把之前写入的值优化掉了。编译器做错了吗？没有，它完全合法。这就是模糊边界的来源。

## 为什么这条线越来越难画

现代编译器的优化不是简单的"删掉没用的变量"这种级别，而是基于对程序语义的深度分析——死存储消除、基于严格别名假设的指针分析、基于有符号整数溢出是 UB 的循环优化……这些优化每一个都建立在"程序没有 UB"的前提假设之上。一旦代码触发了 UB，编译器的这些前提假设就崩塌了，但它不会告诉你，只会继续按照自己的逻辑往下推，推出来的结果可能恰好是对的，也可能是完全荒谬的。更折磨人的是，换一个编译器版本、换一个优化等级、甚至换一种编译顺序，结果可能都不一样。

C++ 标准把很多东西定义为 UB，本质上就是在给编译器腾出优化的空间。享受了优化的红利，就得承担 UB 的风险——这不是编译器在跟你作对，而是选择 C++ 时签的"契约"。

## 应对策略：不猜，用工具

既然靠肉眼看代码很难判断是不是 UB，那就不要猜，用工具。

```cmake
# 我的项目里标配的警告选项，GCC/Clang 通用
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Werror          # 警告当错误，强迫自己处理
    -Wconversion     # 隐式类型转换警告，这个抓过我好几次坑
    -Wsign-conversion # 有符号无符号混用警告
)
```

第一个习惯是开编译器警告到最严格。`-Wconversion` 强烈推荐，它能在循环里用 `int` 做索引去访问 `std::vector` 时容器大小超过 `INT_MAX` 的情况下提前发出截断警告。

第二个是用 Sanitizer。开发阶段跑测试时开 UBSan 和 ASan：

```cmake
# 开发模式下的选项
add_compile_options(
    -fsanitize=undefined,address
    -fno-sanitize-recover=all  # 遇到 UB 直接 abort，别继续跑
    -g -O1                     # 注意：Sanitizer 在 -O0 下效果最好，
                               # 但 -O1 更接近真实场景，我选 -O1 做折中
)
add_link_options(-fsanitize=undefined,address)
```

UBSan 能检测的东西包括有符号整数溢出、空指针解引用、未对齐的内存访问、无效的类型转换（包括严格别名违规）、shift 量超出范围等等。这些东西光靠看代码很难全部覆盖到。

第三个习惯比较"笨"但很有效：多编译器交叉验证。本地用 GCC，CI 里面跑一遍 Clang，偶尔丢到 MSVC 上编译一下。不同编译器对 UB 的"利用方式"不同，同一个 UB 在 GCC 下可能恰好跑得对，在 Clang 下就炸了。如果三个编译器跑出来的结果不一致，几乎可以肯定有 UB。

## Compiler Explorer 上的 LLM 功能

Q&A 里还提到了 Compiler Explorer 上的 LLM 功能，体验参差不齐。拿它来"解释"已有的汇编代码效果不错——丢一段 -O2 下生成的汇编进去，问"这个循环被展开成了什么样"，基本能给出八九不离十的回答。但如果让它"从头生成"一段汇编，风险就大很多了，因为汇编指令集的细节太多了。

用法比较保守的话：只让 LLM 帮忙"读"汇编，不让它"写"汇编。而且每次看完它的解释，对照指令集手册或者 Compiler Explorer 上实际跑的结果验证一遍。演讲者提到的策略也很有意思——在系统提示词里强调"不确定就不要说"，这确实能减少过度自信的错误输出，但代价是它可能会变得更"沉默"。

## 接受模糊，但不放弃精确

在 C++ 的世界里，"编译器做对了"和"代码有 UB 但恰好没炸"这两件事，在表象上可能完全一样，没法通过观察输出来区分。与其纠结"这到底算不算 UB"，不如把精力放在预防上——开严格的警告、跑 Sanitizer、多编译器验证。这三板斧能挡住绝大多数 UB 问题。至于剩下那些真的处于灰色地带的情况，如果不确定它是不是 UB，就换一种写法，写成确定不是 UB 的样子。多写几行代码比半夜排查莫名其妙的优化问题强多了。

---

# 手写汇编的价值——指令集并没有抛弃人类

## 指令集并没有抛弃人类

有一个常见的误解：早期的 x86 指令集是给人写的，指令格式规整、语义清晰；现在的指令集，AVX-512、各种掩码操作、各种前缀组合，完全就是给编译器生成的机器码准备的，人根本读不动。但仔细翻 Intel 的指令手册之后会发现这个认知有问题。

演讲里举了一个精准的例子：有一条指令叫 `PMAXUB`，单看名字和描述——"并行比较无符号字节最大值"，把 16 个字节和另外 16 个字节逐一比较取较大值。第一反应可能是"这什么鬼指令"。但翻 motion JPEG 的规范，发现运动补偿里恰好就需要这个操作，一条指令搞定，编译器根本不知道在什么上下文里该发这条指令。

新指令的诞生逻辑其实没变过——"某个特定领域需要一个高频操作，于是加一条专用指令"。它不是"为了编译器好生成而设计"，而是"为了这个领域的程序员好写而设计"。只不过这个"程序员"可能是写视频编解码的、写密码学的、写数值计算的。不是指令集排斥人类了，而是它服务的"人类"越来越细分了。

## 动手验证：手写汇编 vs 编译器输出

我们来写一段实际的手写汇编，感受一下它和编译器输出的区别。环境是 Arch Linux WSL，GCC 16.1.1，x86-64 架构。注意 GCC 内联汇编的语法和独立汇编器的语法是两回事，后面会说。

先看一个最简单的场景：把一个数组里所有元素取绝对值。用纯 C++ 写，再用手写 SIMD 汇编写，对比一下。

```cpp
// abs_array.cpp
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>

constexpr int N = 1024 * 1024;  // 1M 个 int32

void abs_c(int32_t* dst, const int32_t* src, int n) {
    for (int i = 0; i < n; i++) {
        dst[i] = std::abs(src[i]);
    }
}

// 手写 SSE 汇编版本，每次处理 4 个 int32
void abs_asm(int32_t* dst, const int32_t* src, int n) {
    // 这里用 GCC 扩展内联汇编
    // 核心思路：用 PSIGND 指令，它可以根据符号掩码取反
    // 但更简单的方式是用 PXOR + PSUBD 的技巧：
    // abs(x) = (x ^ mask) - mask，其中 mask = x >> 31（符号位扩展）
    __asm__ volatile (
        "xor %%eax, %%eax\n\t"         // i = 0
        "1:\n\t"
        "cmp %2, %%eax\n\t"            // 比较 i 和 n
        "jge 2f\n\t"                   // 如果 i >= n，跳到结束
        "movdqu (%1, %%eax, 4), %%xmm0\n\t"  // 加载 4 个 int32
        "movdqa %%xmm0, %%xmm1\n\t"   // 复制一份
        "psrad $31, %%xmm1\n\t"       // 算术右移 31 位，得到符号掩码
        "pxor %%xmm1, %%xmm0\n\t"     // x ^ mask
        "psubd %%xmm1, %%xmm0\n\t"    // (x ^ mask) - mask = abs(x)
        "movdqu %%xmm0, (%0, %%eax, 4)\n\t"  // 存储
        "add $4, %%eax\n\t"           // i += 4（一次处理 4 个）
        "jmp 1b\n\t"                  // 继续循环
        "2:\n\t"
        : // 输出操作数，这里不需要
        : "r"(dst), "r"(src), "r"(n)   // 输入操作数
        : "eax", "xmm0", "xmm1", "memory", "cc"  // clobber 列表
    );
}

int main() {
    // 分配对齐的内存
    int32_t* src = (int32_t*)std::aligned_alloc(16, N * sizeof(int32_t));
    int32_t* dst_c = (int32_t*)std::aligned_alloc(16, N * sizeof(int32_t));
    int32_t* dst_asm = (int32_t*)std::aligned_alloc(16, N * sizeof(int32_t));

    // 填充随机数据（包含负数）
    srand(42);
    for (int i = 0; i < N; i++) {
        src[i] = (int32_t)(rand() - RAND_MAX / 2);
    }

    // 预热
    abs_c(dst_c, src, N);
    abs_asm(dst_asm, src, N);

    // 正确性验证——这一步千万别省，我之前就因为没验证白高兴半天
    bool correct = true;
    for (int i = 0; i < N; i++) {
        if (dst_c[i] != dst_asm[i]) {
            printf("MISMATCH at %d: c=%d, asm=%d\n", i, dst_c[i], dst_asm[i]);
            correct = false;
            break;
        }
    }
    printf("Correctness: %s\n", correct ? "PASS" : "FAIL");

    // 性能测试
    constexpr int ITER = 1000;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITER; i++) abs_c(dst_c, src, N);
    auto t1 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < ITER; i++) abs_asm(dst_asm, src, N);
    auto t2 = std::chrono::high_resolution_clock::now();

    double ms_c = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double ms_asm = std::chrono::duration<double, std::milli>(t2 - t1).count();
    printf("C version:   %.2f ms\n", ms_c);
    printf("ASM version: %.2f ms\n", ms_asm);
    printf("Speedup:     %.2fx\n", ms_c / ms_asm);

    std::free(src);
    std::free(dst_c);
    std::free(dst_asm);
    return 0;
}
```

编译运行：

```bash
g++ -O2 -march=native abs_array.cpp -o abs_array && ./abs_array
```

跑出来的结果大概是 ASM 版本快 3 到 4 倍。但先别急着下结论——如果把 `-O2` 换成 `-O3`，GCC 其实会自动向量化这个循环，速度差距会缩小很多。手写汇编的意义在于：当编译器的自动向量化"没猜到你的意图"的时候，你可以精确控制——数据有特殊的对齐方式、循环有特殊的展开需求、要在循环里插入编译器不知道的特定指令——这些场景下，手写汇编就是最后的手段。

## 汇编器的大坑：AT&T vs Intel 语法

GCC 附带的 `as`（GNU Assembler）用的是 AT&T 语法——操作数顺序是反的（源操作数在前，目的操作数在后），寄存器要加 `%` 前缀，立即数要加 `$` 前缀。比如"把 eax 的值存到 [rbx + 8] 这个地址"，AT&T 语法写出来是 `movl %eax, 8(%rbx)`，而 NASM 写法是 `mov [rbx + 8], eax`——后者更符合直觉。如果真的打算手写汇编，建议用 NASM 或者 YASM，Intel 语法可读性好得多：

```asm
; abs_asm.nasm
section .text
global abs_asm_nasm

; void abs_asm_nasm(int32_t* dst, const int32_t* src, int n)
; rdi = dst, rsi = src, rdx = n
abs_asm_nasm:
    xor eax, eax          ; i = 0
.loop:
    cmp eax, edx          ; i < n?
    jge .done
    movdqu xmm0, [rsi + rax*4]   ; 加载 4 个 int32
    movdqa xmm1, xmm0            ; 复制
    psrad xmm1, 31               ; 符号掩码
    pxor xmm0, xmm1              ; x ^ mask
    psubd xmm0, xmm1             ; abs(x)
    movdqu [rdi + rax*4], xmm0   ; 存储
    add eax, 4                   ; i += 4
    jmp .loop
.done:
    ret
```

同样的逻辑，NASM 版本读起来清晰多了。编译的时候注意 NASM 生成的是目标文件，需要把它和 C++ 的目标文件链接在一起。调用约定要自己保证——Linux x86-64 下是 System V AMD64 ABI，前六个整数参数分别放在 rdi、rsi、rdx、rcx、r8、r9 里，返回值在 rax。

## 指令集的发展方向

指令集的发展方向不是"从为人设计变成为编译器设计"，而是"从通用设计变成领域专用设计"。每一条看起来很奇怪的指令，背后都有一个具体的应用场景。不在那个领域里觉得它蠢，在那个领域里觉得它是救命稻草。

这对 C++ 程序员意味着两点：第一，遇到性能瓶颈、编译器优化已经到头的时候，知道可以打开编译器输出的汇编（`-S` 参数或者 Compiler Explorer），看看到底在发生什么；第二，发现某个领域有专用指令可以用的时候，有能力通过内联汇编或者独立汇编文件去调用它，而不是干等着编译器"哪天学会了"。

内联汇编确实有学习成本，但不是不可逾越的——不需要背指令手册，只需要知道"去哪里查"和"怎么写一个最小的可运行例子"，剩下的就是查文档和试错的过程。

---

# 面向人类的汇编器与 LLM 生成汇编

## "面向人类的汇编器"这个概念

演讲者提到现有的一些汇编器很多已经不再积极维护了，然后问是否还有空间做一个"面向人类"的汇编器。这个问题的核心在于：现有工具的设计哲学停留在"汇编器就是汇编指令的翻译器"这个时代，没有往"让写汇编的人更舒服"这个方向走。

比如在 NASM 里想表达"把这个结构体的第二个字段加载到 rax 里"，得自己算偏移量，写 `mov rax, [rcx + 8]`，这个 8 是心算出来的。如果结构体改了一个字段类型，得把所有硬编码的偏移量全找出来改一遍。FASM（Flat Assembler）有一个特性很实用——支持在汇编里直接定义"虚拟结构体"，然后用字段名来引用偏移：`mov rax, [rcx + MyStruct.second_field]`，虽然本质上还是算偏移量，但至少是汇编器帮你算的。

但 FASM 的宏系统调试过程很痛苦，报错信息经常指向宏展开后的某一行，根本不知道原始宏的哪里出了问题。现代的 C++ 编译器都在拼命改善错误信息、改善调试体验，汇编器这边却好像时间停滞了。

理想中的汇编器应该能给出漂亮的错误提示，能内置结构体和联合体的支持（不是通过宏 hack），能支持某种形式的模块化（而不是靠 include 递归包含）。"小众"不等于"没有价值"。

## LLM 生成汇编——千万别无脑信

Q&A 里有一位观众指出 LLM 生成的汇编代码把 RSI 当成了长度，而实际上可能不是。演讲者的回应是"持保留态度"和"非确定性"。作为实际用过 LLM 生成汇编然后被坑过的经验，**LLM 生成的汇编代码，在你不完全理解的情况下，绝对不要直接用。**

举一个实际的例子。让 LLM 写一个"接收三个整数参数、返回它们之和"的汇编函数：

```asm
; LLM 生成的代码——看起来很合理，但有隐患
section .text
global add_three

add_three:
    ; 第一参数在 rdi，第二在 rsi，第三在 rdx
    lea rax, [rdi + rsi + rdx]
    ret
```

乍一看没问题——System V AMD64 ABI 下前六个整数参数确实是 rdi、rsi、rdx、rcx、r8、r9，三个参数相加用 lea 比 add 链式写法优雅。编译链接跑一下，结果居然是对的。但问题出在后面——让它生成一个"接收六个参数、返回它们之和"的版本：

```asm
; LLM 生成的代码——这次有错了
section .text
global add_six

add_six:
    ; 参数：rdi, rsi, rdx, rcx, r8, r9
    lea rax, [rdi + rsi]        ; 先加前两个
    add rax, rdx
    add rax, rcx
    add rax, r8
    add rax, r9
    ret
```

这个代码在大多数情况下能跑对，但有一个微妙的问题：`lea rax, [rdi + rsi]` 做的是无符号加法，如果 rdi 和 rsi 的值很大，加起来超过 64 位无符号整数的范围，会静默溢出。而用 `add rax, rdi` 再 `add rax, rsi`，虽然也会溢出，但溢出标志位（OF）的设置是符合算术加法语义的。如果调用者依赖 OF 标志位来判断是否溢出，这个 lea 就会悄悄地挖一个坑。

更离谱的是，让 LLM 再生成一遍同样的功能，第二次的结果可能把第六个参数的寄存器写成 r10——这完全是错的，r10 不是参数传递寄存器。这就是"非确定性"：问两遍得到两个不同答案，一个可能对，另一个可能错。

## 实际工作流

经过踩坑之后，用 LLM 辅助写汇编的方式应该完全转变：不再让它"帮我写一个做 XXX 的函数"，而是把它当成一个"会背指令手册的聊天对象"。问它"x86-64 有没有一条指令可以同时做加法和乘法"，它会告诉你 `imul` 可以做带加法的变体（比如三操作数形式 `imul rax, rbx, 42`），然后自己去 Intel 手册里确认这条指令的具体行为，再自己写代码。LLM 的价值从"代码生成器"退化为"索引工具"——一个不太可靠但比自己翻 PDF 手册快的索引工具。

## 两个问题的联系

把这两个点放在一起看，它们指向同一个方向：**汇编编程的体验还有很大的改善空间**。面向人类的汇编器是在工具层面改善体验，而可靠的 LLM 辅助（如果有一天能做到的话）是在学习曲线层面改善体验。但这两者的前提都是——你得理解底层在发生什么。工具再好不能代替思考，LLM 再强不能代替验证。

---

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Matt Godbolt"
    title="C++: Some Assembly Required"
    publisher="CppCon 2025"
    :year="2025"
    url="https://www.youtube.com/watch?v=zoYT7R94S3c"
  />
  <ReferenceItem
    :id="2"
    author="ISO/IEC JTC1/SC22/WG21"
    title="The C++ Standards Committee — Official Page"
    publisher="Open Standards"
    url="https://www.open-std.org/jtc1/sc22/wg21/"
  />
  <ReferenceItem
    :id="3"
    author="ISO C++ Foundation"
    title="The Committee: WG21"
    publisher="isocpp.org"
    url="https://isocpp.org/std/the-committee"
  />
  <ReferenceItem
    :id="4"
    author="cppreference.com"
    title="C++ Reference"
    url="https://en.cppreference.com/"
  />
  <ReferenceItem
    :id="5"
    author="Arm Developer"
    title="Condition Codes 2: Conditional Execution"
    publisher="Arm Community Blogs"
    url="https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/condition-codes-2-conditional-execution"
  />
  <ReferenceItem
    :id="6"
    author="Arm Developer"
    title="Condition Codes 1: Condition Flags and Codes"
    publisher="Arm Community Blogs"
    url="https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/condition-codes-1-condition-flags-and-codes"
  />
  <ReferenceItem
    :id="7"
    author="Matt Godbolt"
    title="Compiler Explorer"
    url="https://godbolt.org/"
  />
  <ReferenceItem
    :id="8"
    author="cppreference.com"
    title="Modules (since C++20)"
    url="https://en.cppreference.com/cpp/language/modules"
  />
  <ReferenceItem
    :id="9"
    author="Free Software Foundation"
    title="GCC Manual: Program Instrumentation Options"
    publisher="GCC Online Documentation"
    url="https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html"
  />
  <ReferenceItem
    :id="10"
    author="ISO"
    title="About Us — International Organization for Standardization"
    publisher="iso.org"
    url="https://www.iso.org/about-us.html"
  />
  <ReferenceItem
    :id="11"
    author="ISO"
    title="ISO/IEC 14882:2024 — Programming languages — C++"
    publisher="iso.org"
    :year="2024"
    url="https://www.iso.org/standard/83626.html"
  />
  <ReferenceItem
    :id="12"
    author="cppreference.com"
    title="Constant expressions (C++20/23 [expr.const])"
    url="https://en.cppreference.com/w/cpp/language/constant_expression"
  />
</ReferenceCard>
