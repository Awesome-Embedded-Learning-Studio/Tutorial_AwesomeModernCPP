---
title: "卷八 · 领域应用 课后练习（Homework）"
description: "卷八（嵌入式、网络编程、AI/TinyML、C++ 深度专题等七个子领域）的课后练习：每个子领域按其主线章节出基础+进阶题，另有 2 道跨领域综合与 1 道 L5 无锁队列挑战。难度覆盖 L1~L5，题目全部做了变式处理，参考答案独立成文件、逐步解答附知识点链接，所有输出在 WSL Arch（g++ 16.1.1 / clang++ 22.1.8）真实运行得到。"
chapter: 8
order: 1
tags:
  - host
  - intermediate
  - cpp-modern
  - 嵌入式
  - 网络编程
  - 状态机
  - 循环缓冲区
  - 智能指针
  - 模板
difficulty: intermediate
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 25
prerequisites: []
related: []
---

# 卷八 · 领域应用 课后练习（Homework）

## 引言

这里的题按子领域分节，每个子领域按其主线章节出基础 + 进阶题，最后是两道跨领域综合和一道 L5 挑战。每题标注难度档位（L1~L5，见[练习总览](../exercises/index.md)）和涉及章节，题面里的章节链接点过去就是教材原文。题目都是「变式」——换场景、换数据、换推理方向，照抄教材例题抄不出答案；每道题都要真编译真跑，把输出贴下来才算完。

答案在独立的[参考答案](./02-homework-solutions.md)文件里，按题号对应，每步解答带知识点链接。建议一个子领域做完再看答案。所有代码用 `-std=c++20 -Wall -Wextra` 起步（涉及 `std::expected`/`std::span` 的题目题面会写明用 C++23），需要多线程的加 `-pthread`，需要抓内存问题的上 ASan/UBSan。

一条本卷特有的纪律先说在前面：嵌入式子领域的题**全部在主机上模拟验证**——寄存器用普通变量、时钟用虚拟毫秒、串口用函数调用。需要真实 STM32 板子的场景（比如点灯、EXTI 中断）本套练习一律不出，那是教程正文里配硬件的活。所有答案代码都在 WSL Arch（g++ 16.1.1 / clang++ 22.1.8）下真编译真跑过，验证输出全部真实。

## 8.1 嵌入式

### 8.1-A {#hw-8-1-a}

难度 **L1** · 涉及[第 11 篇：HAL_GPIO_WritePin 与 TogglePin](../embedded/01-led/06-hal-gpio-output.md)、[类型安全的寄存器访问](../embedded/02-type-safe-register-access.md)

**先手算，再真跑**。给定一个模拟的 32 位 GPIO 输出寄存器快照 `uint32_t odr = 0x1C00u`（对应引脚 10/11/12 为高、PC13 对应 bit13 为低）。按顺序执行三件套：①置位 bit13（`|= (1u << 13)`）；②清位 bit10（`&= ~(1u << 10)`）；③翻转 bit11 两次（`^=` 两遍）。每一步打印十六进制值，手算结果写下来再对照输出，最后用移位+掩码读出 bit13 的当前状态。

进阶自测：`int n = 31; int a = 1 << n;` 和 `int b = 1 << 32;` 都是未定义行为吗？（先按本套默认 `-std=c++20` 的移位规则回答，再去查 C++11 的老规则——两个口径不一样。）用 `-fsanitize=undefined` 在 `-std=c++20` 与 `-std=c++11` 两种标准下各真跑一遍（g++ 和 clang++ 都试），贴出 sanitizer 的完整报告——**注意它到底只报了哪一个**，并解释：哪个口径下「没报」是正确行为、哪个口径下「没报」才是工具盲区（提示：CWG 1457）。

[参考答案 →](./02-homework-solutions.md#hw-8-1-a)

### 8.1-B {#hw-8-1-b}

难度 **L2** · 涉及[第 24 篇：非阻塞消抖](../embedded/02-button/06-non-blocking-debounce.md)、[第 25 篇：7 状态消抖状态机](../embedded/02-button/07-debounce-state-machine.md)

实现一个非阻塞消抖器（主机版：虚拟时钟，`feed(sample, now_ms)`），维护三个状态量：`last_raw_`（最近原始采样）、`last_change_`（原始值最后一次变化的时间戳）、`stable_`（确认后的稳定电平），消抖窗口 20ms。喂入这条信号（time, level）序列：`(0,0) (1,1) (3,0) (5,1) (9,0) (11,1) (31,1) (60,0) (61,1) (63,0) (85,0)`——一次带 4 次反弹的按下，持续按住，再来一次带 2 次反弹的释放。打印每一次**稳定状态确认**发生的时间与方向，并解释为什么两个事件分别落在 t=31 和 t=85、而不是在抖动发生的时刻。

再做一个溢出验证：把 `last_change_` 设为 `0xFFFFFFF8`，在 `now = 0x10` 时喂入采样，验证无符号减法的差值计算仍然正确（不用管溢出）。

[参考答案 →](./02-homework-solutions.md#hw-8-1-b)

### 8.1-C {#hw-8-1-c}

难度 **L4** · 涉及[第 37 篇：无锁环形缓冲区](../embedded/03-uart/07-circular-buffer-lock-free-spsc.md)、[循环缓冲区](../embedded/03-circular-buffer.md)

这道题是「教材代码审查」。第 37 篇的 `CircularBuffer` 用两个技巧：`mask(v) = v & (N-1)` 做数组下标环绕，`next(v) = (v+1) & (2N-1)` 让 `head_`/`tail_` 在 `0..2N-1` 之间环绕，并且白纸黑字写着「留一个位置不写来区分空和满：如果 N 个位置的缓冲区，最多存 N-1 个字节」。

①**照抄**教材的实现（N=8），push 10 个字节不 pop，打印每次 push 的返回值、`size()`，再全部 drain 出来——教材承诺的「最多存 7 个」守住了吗？贴出真实输出。②定位缺陷根源：结合 `next()` 的定义域（0..2N-1）和 `mask()` 的值域（0..N-1），说明 `full()` 判定为什么失效、失效后写到了哪里。③用「单调递增计数器 + 只在数组下标处 `& (N-1)`」重写，验证：容量恰好 N-1、满时拒绝并计数、pop 后 push 走环绕路径、drain 顺序正确。④一句话说清重写方案为什么不怕计数器回绕。

[参考答案 →](./02-homework-solutions.md#hw-8-1-c)

### 8.1-D {#hw-8-1-d}

难度 **L4** · 涉及[第 32 篇：UART 协议详解](../embedded/03-uart/02-uart-protocol-basics.md)

主机上实现一个 8N1 UART 帧编解码器。①编码：把 `0x41`（`'A'`）编成 10 bit 帧——起始位 0、数据位 LSB 在前、停止位 1——打印帧并与手算对照。②解码：以 16x 过采样模拟接收端——把帧展开成 160 个采样点（每个 bit 重复 16 次），并且模拟时钟偏差：采样起始边沿**迟到 2 个 tick**；解码器在每个 bit 中心取 3 个采样点做多数表决。③错误检测：解码器要校验起始位必须为 0、停止位必须为 1；把停止位附近的采样点改成 0，验证解码器报 framing error。要求全程只用 `std::array` 和位运算，`-Wall -Wextra` 零警告。

[参考答案 →](./02-homework-solutions.md#hw-8-1-d)

## 8.2 网络编程

### 8.2-A {#hw-8-2-a}

难度 **L1** · 涉及[00 · 传统 socket 编程：服务器五步与 TCP 建链](../networking/00-traditional-socket-basics.md)

字节序三道。①**先预测**：端口 `13013`（十六进制是多少？）经 `htons` 后，在**小端主机**内存里两个字节的先后顺序是什么？`0x7F000001`（127.0.0.1）经 `htonl` 后内存里四个字节呢？写程序用 `unsigned char*` 逐字节打印验证，并验证 `ntohs(htons(x)) == x` 往返不变。②判断题：「网络传输都是大端，所以二进制日志文件在不同机器间直接拷贝没问题」——对还是错？结合 `htonl`/`htons` 的作用说清协议字段的正确姿势。③一句话：为什么教材里说写上 `htons`/`htonl`「无论什么 CPU 都对」？

[参考答案 →](./02-homework-solutions.md#hw-8-2-a)

### 8.2-B {#hw-8-2-b}

难度 **L2** · 涉及[01 · 现代 socket 封装：RAII 与 std::expected](../networking/01-modern-socket-wrapping.md)

实现一个 RAII 资源包装类 `Fd`（变式：不碰真实 fd，用一个全局计数器 `active` 模拟内核的 fd 表——构造 +1、真正 close 时 -1）。要求：①禁拷贝（贴出 `Fd b = a;` 的真实编译报错）；②移动语义转移所有权，moved-from 对象置空（`get()` 返回 -1），其析构必须安全地什么都不做；③验证三段事实：作用域退出自动释放（计数器归零）、`reset()` 手动释放恰好一次、moved-from 对象析构不再 double close。打印构造/释放轨迹，结合教材 01 篇解释「漏 close 从靠人记变成不可能」这句话的含义。

[参考答案 →](./02-homework-solutions.md#hw-8-2-b)

### 8.2-C {#hw-8-2-c}

难度 **L4** · 涉及[02 · epoll：Linux I/O 多路复用](../networking/02-epoll-io-multiplexing.md)

用 `socketpair` + epoll 在主机上复现教材的「ET-read-once 丢数据」陷阱（变式：教材用 TCP，这里用 socketpair，机制一样、免开服务器）。一次写 8192 字节进对端，读端每次最多读 1024。跑三种模式并统计「总字节数 / epoll 事件数 / read 调用次数」：①LT 模式 + 循环读到 EAGAIN；②ET 模式但**每个事件只 read 一次**（`epoll_wait` 带 200ms 超时）——贴出丢了多少字节；③ET 模式 + 循环读到 EAGAIN（读端 fd 必须先设 `O_NONBLOCK`，为什么？）。回答：为什么教材说「拿小消息做单元测试全绿、一上线大 burst 才丢数据」是最阴险的 bug？你的输出里哪一行对应这个说法？

[参考答案 →](./02-homework-solutions.md#hw-8-2-c)

## 8.3 AI 与 TinyML

### 8.3-A {#hw-8-3-a}

难度 **L1** · 涉及[行主序——二维坐标怎么落进一维内存](../ai/tiny_ml/stage1/04-row-major.md)

行主序换算三题。①**手算**：`Tensor<4, 3>` 的权重矩阵，$W(2, 1)$ 对应扁平数组下标几？下标换算公式是什么？②真跑：往 `std::array<float, 12>` 里依次填 0..11，用 $i \times Cols + j$ 打印成 4 行 3 列表格，对照手算结果。③**先预测再验证**：同一行相邻元素 `&W[0][1] - &W[0][0]` 是几？跨行的 `&W[1][0] - &W[0][2]` 是几？很多人预测第二个不是 1——解释为什么它也是 1（提示：底层是一段连续数组，二维只是外面那层「壳」的换算），并说明这套行主序约定为什么是后面 NumPy 对拍的基础。

[参考答案 →](./02-homework-solutions.md#hw-8-3-a)

### 8.3-B {#hw-8-3-b}

难度 **L3** · 涉及[固定维度 Tensor——推理器的数据底座](../ai/tiny_ml/stage1/06-tensor.md)

实现最小 `Tensor<Rows, Cols, StorageType=float>`（C++23，题面用到 `std::expected`）：`std::array` 存储、`operator()(i, j)` 热路径返回引用不检查、`at(i, j)` 返回 `std::expected<StorageType, Error>`（越界走错误路径、`noexcept`）、`view()` 返回扁平 span、`storage()` 返回底层数组。验证四条：①`at(99, 0)` 返回 `kOutOfRange` 且进程正常退出码 0（无异常）；②默认构造零初始化（`t(0,0) == 0.f`）——成员声明里的 `{}` 起什么作用？③逐元素断言 `&t(i,j) == &t.storage()[i*Cols+j]`（行主序地址恒等）；④**编译失败题**：把 `at` 的返回类型改成 `std::expected<float&, Error>`，贴出真实编译报错的关键行，说清标准里哪条规则堵死了「返回引用」这条路。

[参考答案 →](./02-homework-solutions.md#hw-8-3-b)

### 8.3-C {#hw-8-3-c}

难度 **L3** · 涉及[形状塞进类型——维度为什么是模板参数](../ai/tiny_ml/stage1/05-shape-in-type.md)、[固定维度 Tensor——推理器的数据底座](../ai/tiny_ml/stage1/06-tensor.md)

把 Tensor 全套 `constexpr` 化，让形状检查发生在编译期。①写 `constexpr Tensor<3, 2>`，用 `static_assert` 在编译期求整个 Tensor 的元素和（`total() == 21.f`）与逐元素 `add()` 的结果——整个计算发生在编译期，程序运行只打印一行确认。②形状进类型：`add(Tensor<2,3>, Tensor<2,3>)` 编译通过、`add(Tensor<2,3>, Tensor<3,2>)` 编译失败——贴出真实报错，指出关键行（「deduced conflicting values for non-type parameter」）。③回答：如果形状是运行时的（`std::vector` 方案），这个错误要到什么时候、以什么形式暴露？两种时机差在哪里？

[参考答案 →](./02-homework-solutions.md#hw-8-3-c)

## 8.4 C++ 深度专题

### 8.4-A {#hw-8-4-a}

难度 **L2** · 涉及[非拥有指针全景：从 T* 到 Borrowed 到 ObserverPtr](../cpp-deep-dives/pointer-semantics/01-non-owning-pointer-overview.md)

语义辨析 + 编译验证。①填空表：`T&`、`T*`、`Borrowed<T>`、`ObserverPtr<T>`、`WeakPtr<T>`、`std::weak_ptr<T>` 六种，各回答三问——可空吗？能延长对象生命周期吗？对象销毁后能安全判空吗？②实现教学版 `Borrowed<T>`（`explicit Borrowed(T&)`、`Borrowed(T&&) = delete`、`Borrowed(std::nullptr_t) = delete`、`borrow()` 辅助函数），写一个统计字符串里某字符出现次数的函数走通正常用法。③两处**编译失败**都要真跑贴报错：从临时对象构造（`Borrowed<std::string> b(std::string("temp"));`）——报的是什么？从左值拷贝初始化（`Borrowed<std::string> b = s;`）——为什么连这条也被拒了？

[参考答案 →](./02-homework-solutions.md#hw-8-4-a)

### 8.4-B {#hw-8-4-b}

难度 **L4** · 涉及[WeakPtr 反模式：T* + raw Flag* 的致命陷阱](../cpp-deep-dives/pointer-semantics/02-unsafe-weakptr-ub.md)、[SimpleWeakPtr：T* + shared_ptr\<Flag\> 的安全改进](../cpp-deep-dives/pointer-semantics/03-simple-weakptr.md)

UB 实证对抗赛。①写 `UnsafeWeakPtr`（`T* + raw Flag*`，Flag 是 Factory 的成员），在 owner 销毁后调用 `is_valid()`：普通构建输出什么？ASan 构建（`-fsanitize=address -g`）报告什么——贴出 report 的 ERROR 行与 `freed by` 栈帧。②改成 `SimpleWeakPtr`（`T* + std::shared_ptr<Flag>`），同样场景 ASan 干净、安全返回 false。③关键问答：普通构建下 Unsafe 版「看起来能工作」（本机输出就是它），为什么这恰恰是 UB 最危险的表现形式？`shared_ptr<Flag>` 解决的到底是「Flag 内存没了」还是「对象 T 的并发访问安全」？

[参考答案 →](./02-homework-solutions.md#hw-8-4-b)

## 8.5 GUI 与图形（规划中）

> 本子领域正文仍在规划中（见[子领域导航](../gui-graphics/index.md)）。这两道题基于其规划主题「图形基础、最小 GUI 框架」出成通识 + 主机模拟，全部知识自包含、不依赖教材正文。

### 8.5-A {#hw-8-5-a}

难度 **L1** · 涉及[GUI 与图形（规划中）](../gui-graphics/index.md)

概念判断 + 主机模拟。①判断题四则：a) RGB 颜色用三个字节分别表示红绿蓝强度；b) 帧缓冲（framebuffer）是内存里一块对应屏幕像素的数组，每个元素决定一个像素的颜色；c) 双缓冲（double buffering）用两块帧缓冲：一块正在显示、一块在后台绘制，绘制完成后交换——避免画面「撕裂」；d) GUI 事件循环是「等事件 → 取事件 → 分发事件」的循环，程序的主线程大部分时间都泡在里面。②主机模拟验证双缓冲语义：写一个 8×8 的 `Display`（front/back 两块缓冲 + `draw()` 打印 front + `flip()` 交换），先向 back 写两个像素，`flip()` 前后各 `draw()` 一次——贴输出，指出「向 back 写了但屏幕不变」发生在哪一步、为什么这是「未完成画面不可见」的证据。

[参考答案 →](./02-homework-solutions.md#hw-8-5-a)

### 8.5-B {#hw-8-5-b}

难度 **L2** · 涉及[GUI 与图形（规划中）](../gui-graphics/index.md)、[第 27 篇：std::variant 事件 + std::visit 分发](../embedded/02-button/09-cpp-variant-and-visit.md)

最小 GUI 事件循环（主机模拟）。事件用 `std::variant<ClickEvent, ResizeEvent>` 表达（这正是按钮教程第 27 篇的类型安全事件套路，换了个 GUI 场景），写一个 `MiniLoop`：`post()` 把事件入队（`std::queue`），`on_click`/`on_resize` 注册回调（`std::function`），`run()` 循环 `std::visit` 分发直到队列空。按「click → resize → click」的顺序 post 三个事件，贴出分发输出。回答：如果把 `ClickEvent` 换成 `enum class` + switch，再给 `ClickEvent` 加一个时间戳字段，两种方案各要改哪些地方？（对比第 27 篇「为什么用空结构体而不是 enum class」的论述。）

[参考答案 →](./02-homework-solutions.md#hw-8-5-b)

## 8.6 数据存储（规划中）

> 本子领域正文仍在规划中（见[子领域导航](../data-storage/index.md)）。这两道题基于其规划主题「序列化、文件格式、键值存储」出成主机可验证的实现题，知识自包含。

### 8.6-A {#hw-8-6-a}

难度 **L2** · 涉及[数据存储（规划中）](../data-storage/index.md)

二进制记录的布局稳定序列化。逻辑记录 `Record { uint32_t id; uint16_t temp; char tag[4]; }`（temp 单位 0.1℃，245 = 24.5℃）。①打印 `sizeof(Record)`——为什么它比字段之和多 2 字节？这 2 字节在结构体的什么位置、为什么在那里？（先预测，再用 `offsetof` 实测）②**不直接 fwrite 结构体**，而是逐字段手工打包成 `std::array<unsigned char, 10>`（大端字节序，即网络序惯例；`tag` 用 `memcpy`），打印线上字节，再 `unpack` 回来验证字段一致。③回答：为什么 `fwrite(&record, sizeof(record))` 跨平台/跨编译器危险？手工打包解决的是哪两个具体问题？

[参考答案 →](./02-homework-solutions.md#hw-8-6-a)

### 8.6-B {#hw-8-6-b}

难度 **L2** · 涉及[数据存储（规划中）](../data-storage/index.md)、[静态存储与栈上分配策略](../embedded/02-static-and-stack-allocation.md)

固定容量的键值表（无堆分配版——顺手呼应嵌入式的静态分配精神）。实现 `FixedMap`：8 个槽位（`std::array<Entry, 8>`），开放寻址 + 线性探测，`put(key, value)` 支持覆盖、表满返回 false，`get(key)` 返回 `std::optional<int>`（探测到第一个空槽即返回 `nullopt`，为什么可以这么做？）。玩具哈希用「字符串长度」即可。验证序列：`put("led", 1)`、`put("uart", 2)`、`put("led", 0)`（覆盖）、`put("button", 3)`、`put("tensor", 4)`，然后查询 led/uart/button/tensor/不存在的键，贴输出。

[参考答案 →](./02-homework-solutions.md#hw-8-6-b)

## 8.7 算法与数据结构（规划中）

> 本子领域正文仍在规划中（见[子领域导航](../algorithms/index.md)）。这两道题基于其规划主题「复杂度分析、经典算法、手写 STL 组件」出成通识 + 手写实现，知识自包含。

### 8.7-A {#hw-8-7-a}

难度 **L1** · 涉及[算法与数据结构（规划中）](../algorithms/index.md)

复杂度辨析 + 实测。①填空表：冒泡 / 插入 / 选择 / 快速 / 归并 / 堆排序各自的最坏时间复杂度和平均时间复杂度，并标出哪些是稳定排序。②选择题：`std::sort` 通常是什么算法？它的最坏情况时间复杂度是多少、为什么标准库敢承诺它？③实测：n=1000 随机序列，手写冒泡排序与 `std::sort` 各自调用多少次元素比较（给比较器装计数器）——贴出真实数字，用 $\frac{n(n-1)}{2}$ 验证冒泡的理论值，解释实测相差约 43 倍（约 1.6 个数量级）的原因，并说明理论比值 n/log₂n（n=1000 时约 100 倍）才接近两个数量级。

[参考答案 →](./02-homework-solutions.md#hw-8-7-a)

### 8.7-B {#hw-8-7-b}

难度 **L3** · 涉及[算法与数据结构（规划中）](../algorithms/index.md)

手写 STL 组件——二叉堆优先队列。实现 `MaxHeap`：`push`（尾插 + 上滤）、`pop`（首尾交换 + 下滤）、`empty`，底层用 `std::vector`，父节点下标 $\frac{i-1}{2}$、左右孩子 $2i+1$/$2i+2$。验证：10 个随机数依次 push、全部 pop 出来，断言输出严格降序；再用 `std::priority_queue<int>` 对同一组输入做对照，逐元素比对两者 pop 序列一致。回答：为什么 push/pop 都是 O(log n)？上滤/下滤各在维护什么不变量？

[参考答案 →](./02-homework-solutions.md#hw-8-7-b)

## 8.C 跨领域综合与挑战

### 8.C-1 {#hw-8-c-1}

难度 **L3** · 涉及[第 42 篇：命令处理器与完整代码走读](../embedded/03-uart/12-command-processor-and-main-walkthrough.md)、[00 · 传统 socket 编程：服务器五步与 TCP 建链](../networking/00-traditional-socket-basics.md)

分片流式行解析器（嵌入式命令解析 × 网络流语义的综合）。TCP 的 `read` 一次返回多少字节没有保证——一行 `"LED ON\n"` 可能被切成任意几段到达。实现 `LineParser::feed(data, n)`（内部固定 `std::array<char, 16>` 行缓冲、无堆分配），要求：容忍 `\r\n`；空行不产生输出；超长行静默丢弃超出部分；缓冲区永不越界。测试：把整段会话流 `"LED ON\r\nHELP\nLED OFF\n" + 一条 54 字节的超长行 + "TAIL\n"` 按不规则片大小 `{2,1,4,3,7,2,5,9,1,1,1,1}` 循环切分喂入，打印每条被分发出的完整行与总行数；再用 ASan 构建跑一遍贴「零报告」。回答：教材第 42 篇主循环里的行拼装代码，把 `line_len < line_buf.size() - 1` 这个条件去掉会怎样？

[参考答案 →](./02-homework-solutions.md#hw-8-c-1)

### 8.C-2 {#hw-8-c-2}

难度 **L4** · 涉及[非拥有指针全景：从 T* 到 Borrowed 到 ObserverPtr](../cpp-deep-dives/pointer-semantics/01-non-owning-pointer-overview.md)、[固定维度 Tensor——推理器的数据底座](../ai/tiny_ml/stage1/06-tensor.md)、[行主序——二维坐标怎么落进一维内存](../ai/tiny_ml/stage1/04-row-major.md)

非拥有视图的推理数据通路（深度专题 × AI 综合）。给定 `Tensor<4, 3>` 权重 W 与 `Tensor<1, 4>` 偏置 b，实现 `LayerView`：**只持有** `std::span<const float>`（权重、偏置各一个）+ 维度，提供 `forward(x, y)` 计算 $y[i] = Σ_j W[i\times in+j]·x[j] + b[i]$（行主序，公式已在题面给出；Dense 层是 Stage 2 的内容，这里只练「非拥有视图」这个机制）。验证：①`w.view().data() == w.storage().data()`（view 零拷贝）；②把 $w(0,0)$ 改成 100 后再 forward，视图立刻看到新值（借用不是拷贝）；③问答：这套「span 视图不拥有数据」的约定，推理器为什么敢用？什么情况下它会变成悬垂引用（对照 01 篇四层语义模型回答）？

[参考答案 →](./02-homework-solutions.md#hw-8-c-2)

### 8.C-3 {#hw-8-c-3}

难度 **L5** · 涉及[第 37 篇：无锁环形缓冲区](../embedded/03-uart/07-circular-buffer-lock-free-spsc.md)、[中断安全的代码编写](../embedded/05-interrupt-safe-coding.md)

挑战题（受 Dmitry Vyukov 的 intrusive MPSC 队列（1024cores.net）启发、按竞赛挑战级强化改编；L5＝「用本卷知识可解的最难问题」，档位口径见[练习总览](../exercises/index.md)）。实现跨线程的无锁 SPSC 环形缓冲区：`std::atomic<size_t>` 的 head/tail（各自独占 cache line）、容量 N-1（留一槽区分空满）、`push` 用 `memory_order_relaxed` 读自己 + `memory_order_acquire` 读对方 + `memory_order_release` 写自己。双线程：生产者把 0..9999999 共 1000 万个 `uint32_t` 依次入队（满时自旋），消费者全部出队并**逐项校验严格递增**——断言无丢失、无重复、无乱序。要求：①普通构建 `-O2` 跑通；②TSan 构建（`-fsanitize=thread`）同样跑通且**零报告**——TSan 是通过此题的必要条件。回答：为什么 `push` 读 `tail` 要 acquire、写 `head` 要 release？换成全 relaxed 会怎么样？

[参考答案 →](./02-homework-solutions.md#hw-8-c-3)
