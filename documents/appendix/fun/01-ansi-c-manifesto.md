---
title: "讨 ANSI C 檄"
description: "社区整活投稿：借檄文体数落 ANSI C、吹捧 Modern C++，图一乐"
chapter: 99
order: 1
tags:
  - host
  - cpp-modern
  - beginner
  - 整活
difficulty: beginner
platform: host
reading_time_minutes: 3
contributors:
  - github: owollz4
    roles:
      - author
---

# 讨 ANSI C 檄

> **编者按**：本文是 [owollz4](https://github.com/owollz4) 的整活投稿，原发于 [Issue #274](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/issues/274)，收录时只调了排版，文字照录。檄文是古人开战前声讨对手、鼓舞己方的宣言——本文借这个体例把 Modern C++ 的卖点挨个数了一遍。图一乐，技术立场以各卷正文为准。

天下人苦 ANSI C 久矣，苍天已死，Modern C++ 当立！

忆往昔，K&R 二圣铸剑于贝尔实验室，C 语言横空出世：语法简练，贴近硬件，可移植之名威震寰宇，开国之功不可磨灭。然自 ANSI C（C89）定鼎以来，四十余载风雨飘摇，积弊如山，民不聊生——

**其罪一：内存全凭手工。** malloc/free 手操，稍有不慎，泄漏、悬垂、双重释放，三灾齐降；valgrind 追凶彻夜，Segmentation fault (core dumped) 惊坐起。

**其罪二：泛型形同虚设。** 欲写通用代码，唯有 void\* 一途，强转横飞，类型安全名存实亡，编译器袖手旁观。

**其罪三：字符串乃裸奔。** 字符数组添 '\\0' 便自称字符串，strcpy、sprintf 埋雷处处，缓冲区溢出，安全界百年噩梦。

**其罪四：错误处理靠返回码。** 层层 if 判断，嵌套十八层；goto cleanup 满天飞舞，禁 goto 之令形同虚设。

**其罪五：宏乃万恶之源。** #define 假函数之名，行文本替换之实；副作用、运算符优先级、重复包含，坑杀英雄无数；#ifdef 嵌套如迷宫，跨平台编译如渡劫。

**其罪六：基建全靠自己。** 手搓链表、自造哈希、遍写字符串分割函数……程序员一半光阴用于造轮子，另一半用于 debug 轮子。

苍天已死！今有 Modern C++，自 11 起兵，历 14、17、20、23 五朝中兴，励精图治，气象一新：

- **RAII 立国**：资源获取即初始化，栈上对象自动析构，泄漏之患自此绝迹；
- **智能指针三杰**：unique_ptr 独占所有权，shared_ptr 共享计数，weak_ptr 化解循环引用——裸指针可送入博物馆养老矣；
- **容器千军**：string、vector、map、unordered_map……STL 一声令下，万军齐发，何须手搓数据结构？
- **auto 与 lambda**：类型推导令冗长签名灰飞烟灭，闭包随行，回调自带上下文；
- **类型安全新境界**：optional 斩 null 于马下，variant 定类型之和，Concept 于编译期约束模板；
- **性能不落人后**：移动语义资源尽归我有，constexpr 编译期算尽万事，std::sort 内联优化吊打 qsort 之函数指针；
- **Modules 平定 #include 之乱**：循环依赖、前向声明苦役，一朝尽除！

故曰：《Pointers On C》者，Kenneth Reek 先生之经典，昔日诚为良师；然书中所授，皆是手工作坊之技艺——手控内存、宏戏指针、自造容器。正如练武扎马步，虽为根基，今日战场早已是火器时代。

今当人手一部《Effective Modern C++》，以《C++ Primer》筑基，以 cppreference 为随身兵法，深谙 RAII、智能指针、移动语义、并发之道，方为正途！

嗟乎！忆往昔，手写 strlen 察内存，彻夜 gdb 追悬垂；看今朝，编译期断言万千，Concept 检错于未然。此诚程序员之盛世也！

苍天已死，Modern C++ 当立；岁在丙午，天下大吉！

---

*注一：内核驱动、嵌入式、单片机诸领域，C 仍是不可撼动之基石；C 与 C++ 同根而生，相煎何太急。*

*注二：C++ 编译报错动辄三页纸起，模板错误更是天书——此乃新朝之税，诸君需忍之。*

*注三：《Pointers On Modern CPP》虽尚未实现，吾心向往之，愿有大能早日著之。*
