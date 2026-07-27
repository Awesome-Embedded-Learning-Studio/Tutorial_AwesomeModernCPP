---
title: "权重为什么是 [Out, In]——行主序下的 cache 账"
description: "拆 Dense 权重为什么是 [Out,In] 不是 [In,Out]:算第 o 个输出要读 W 的第 o 行,行主序下一行连续 cache 友好,反过来读一列得跳着取;附 1024x1024 在线可运行的 cache 实测(列序比行序慢约 7-9 倍);同时跟 PyTorch nn.Linear 的 weight (out,in) 对齐,Stage 5 对拍零摩擦"
chapter: 8
order: 14
platform: host
difficulty: intermediate
cpp_standard: [23]
reading_time_minutes: 7
prerequisites:
  - "行主序——二维坐标怎么落进一维内存"
  - "Dense 在算什么——一次乘加,拆到每个输出"
related:
  - "Dense 层——span 视图与权重布局"
  - "固定维度 Tensor——推理器的数据底座"
tags:
  - host
  - cpp-modern
  - intermediate
  - 内存管理
---

# 权重为什么是 [Out, In]——行主序下的 cache 账

[01 篇](./01-what-is-dense.md)拆 Dense 公式的时候,反复说"算第 o 个输出,用权重的第 o 行"。这就引出个问题:权重那张表,凭什么是 Out 行 In 列(`[Out, In]`),不是反过来 In 行 Out 列(`[In, Out]`)?

你可能会想,不就是个排法嘛,数学上转置一下的事儿,选哪个不一样。数学结果确实一样(转置能补回来),但在内存里怎么摆,直接决定算的时候 cache 配不配合你、以及 Stage 5 跟 NumPy 对拍能不能对上。这条线笔者在这里钉死,后面不许再改主意——跟 [Stage 1 的 04 篇](../stage1/04-row-major.md)钉行主序是一个意思。

## 先复习:行主序怎么摆

[Stage 1 的 04 篇](../stage1/04-row-major.md)定过,咱们的二维 Tensor 走行主序:先存满第 0 行,再存第 1 行,一行接一行。一张 `Tensor<Out, In>` 的权重 W,在内存里是这样一条线:

```text
下标:    0       1       ...  In-1     In      In+1   ...
      [ W[0,0]  W[0,1]  ... W[0,In-1] W[1,0]  W[1,1] ... ]
        └─── 第0行(算第0个输出的配方)───┘ └── 第1行 ──┘
```

W[o, i] 落在下标 `o * In + i`。第 o 行那一组(算第 o 个输出用的配方),在下标 `o*In` 到 `o*In + In - 1` 之间,**内存里挨在一起,连续**。

## 算一个输出,正好顺读一行

[01 篇](./01-what-is-dense.md)的公式再贴一遍:

```text
y[o] = Σ W[o,i]·x[i]   (i 从 0 到 In-1)
```

算第 o 个输出,要把 `W[o,0]`、`W[o,1]`、...、`W[o,In-1]` 这一行全读一遍,跟输入逐个乘再加。而这一行,咱们刚说,在内存里是连续挨着的。

CPU 读内存不是一个个数读的,它按 cache line(本机 64 字节,16 个 float)一坨一坨往缓存里搬。你读 `W[o,0]` 的时候,`W[o,1]`、`W[o,2]`... 早被顺手搬进缓存了,后面取它们直接命中缓存,飞快。这是行主序 + `[Out, In]` 布局送上门的红利:算每个输出的内层循环,顺着一行往下读,cache 全程配合。

## 反过来 [In, Out] 会怎样

假设当初选了 `[In, Out]`(In 行 Out 列)。算第 o 个输出,你要的配方现在散在**第 o 列**:`W[0,o]`、`W[1,o]`、...、`W[In-1,o]`。在行主序下读一列,相邻两个元素之间隔着整整一行(stride = Out 个 float)。也就是说,取完 `W[0,o]`,下一个要取的 `W[1,o]` 跟它隔了 Out 个数,大概率不在同一个 cache line 里——每次取都得重新从内存搬一坨,却只用其中那一个数,缓存利用率惨不忍睹。

数学上这两种摆法算出来的结果能一样(转置一下的事),但跑起来速度能差几倍。矩阵一大,这个差距实打实。

## 真的吗?跑跑看

口说无凭,咱们拿一个 1024×1024 的矩阵,分别按行序遍历(`[r,c]` 内层走 c)和按列序遍历(`[r,c]` 内层走 r,跨行跳)各累加一遍,计个时(`-O2`,5 次取中位数)。代码不长,点开下面这个 demo 自己跑一遍:

<OnlineCompilerDemo
  title="行主序 vs 列主序的 cache 差异"
  source-path="code/examples/vol8/cache_layout.cpp"
  description="1024x1024 矩阵行序遍历 vs 列序遍历的耗时对比,点运行在 Compiler Explorer 云端执行"
  allow-run
  run-compiler="g152"
  run-options="-O2 -std=c++23"
/>

点"动手试一试"再点"运行",等几秒就出结果。这里贴的是本机 WSL2 的数字,Compiler Explorer 云端服务器实测能跑到 9 倍多,你的机器又是另一个数,但"行序远快于列序"这个趋势一定成立:

```text
N=1024, -O2, 5 次取中位数:
  行序(row-major): 0.39 ms
  列序(col-major): 2.83 ms
  列序 / 行序 = 7.2 倍
```

差了 7 倍多。这个数字里既有 cache 的功劳(行序命中、列序 miss),也有编译器自动向量化的功劳(行序循环好向量化,列序难向量化),但大头是 cache。咱们 Lab 测试用的 2×2、2×3 小矩阵感觉不到,可布局这玩意儿一旦定下来,后面所有 stage 都照着走,现在不选对,Stage 7 嵌入式审查、Stage 8 上 MCU 的时候会回头咬你(问就是嗷奥嗷嗷奥！痛痛痛痛痛)。

## 还有一条:跟 PyTorch 对齐

光 cache 友好还不够,权重布局还背着另一个任务:Stage 5 要从 NumPy/PyTorch 把训练好的权重导进 C++。要是咱们这边的摆法跟人家不一样,导出的时候就得转置、重排,容易出错。

巧的是,业界事实上的标准就是 `[Out, In]`。PyTorch 的 `torch.nn.Linear(in_features, out_features)`,它存的权重 `weight` 形状正是 `(out_features, in_features)`,前向计算官方文档原话写作 `y = xWᵀ + b`(见 [PyTorch nn.Linear 文档](https://docs.pytorch.org/docs/stable/generated/torch.nn.Linear.html))。展开看单样本:`y[o] = Σ_i x[i]·Wᵀ[i,o] = Σ_i x[i]·W[o,i]`,跟咱们 [01 篇](./01-what-is-dense.md)的公式一模一样。也就是说,PyTorch 的 `weight[o, i]` 和咱们的 `W[o, i]` 指向同一个数、躺在内存的同一个位置。Stage 5 导出时,Python 那边 `W.flatten()` 出来的顺序,跟咱们 `weight_[o*In + i]` 一位对一位地对上,不用任何转置。

这不是巧合。咱们选 `[Out, In]` 时就冲着两件事一起办:cache 友好 + 跟业界对齐。两条理由指向同一个选法,这笔买卖划算。

## 带走什么

权重选 `[Out, In]`,两条腿走路:一是算每个输出正好顺读一行,行主序下连续,cache 配合(实测 1024×1024 矩阵,行序比列序快 7 倍);二是跟 PyTorch `nn.Linear` 的 `(out, in)` 惯例对齐,Stage 5 对拍零摩擦。反过来 `[In, Out]` 这两条都吃亏。

布局定了,接下来就能动手写 Dense 了。但写之前还有个存储策略的取舍要先拍板:权重这一堆数,`Dense` 是自己存一份,还是只拿个视图指过去?这事看着小,实则牵扯到 v0.1 的无堆分配硬约束和 Stage 5 的权重导出,甚至决定 Stage 5 要不要回头改接口。[03 篇](./03-dense.md)就拆这个,顺带把 Dense 的完整接口摆出来。
