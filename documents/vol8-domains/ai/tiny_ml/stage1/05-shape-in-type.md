---
title: "形状塞进类型——维度为什么是模板参数"
description: "讲维度为什么进模板参数 Tensor<Rows,Cols,StorageType> 而非构造函数传入:编译期固定大小顺带不堆分配、类型系统当免费 shape checker 把形状错挪到编译期、维度编译期可见;代价是每种维度组合都是独立类型"
chapter: 8
order: 11
platform: host
difficulty: intermediate
cpp_standard: [23]
reading_time_minutes: 6
prerequisites:
  - "行主序——二维坐标怎么落进一维内存"
related:
  - "固定维度 Tensor——推理器的数据底座"
  - "为什么不用现成的——三个候选过堂"
tags:
  - host
  - cpp-modern
  - intermediate
  - 模板
  - 类型安全
---

# 形状塞进类型——维度为什么是模板参数

[上一篇](./04-row-major.md)讲了“数怎么摆”(行主序),这篇讲最后一块:**这两个数 Rows 和 Cols,凭什么要写进模板参数 `Tensor<Rows, Cols, StorageType>`,而不是像普通对象那样,构造的时候传进去?**

你可能会想,维度嘛,构造的时候传个 rows、cols 不就完了,干嘛非得塞进模板参数搞出一堆类型。这一步是 Tensor 设计里最容易被新手跳过、却又最值钱的一步——它把一整类“形状错”的 bug,从运行期挪到了编译期。

## 两种写法的对照

先看如果维度不进类型,会怎么写。大概是这种对新手友好的写法:

```cpp
class Tensor {
    float* data_;
    int rows_, cols_;
public:
    Tensor(float* data, int rows, int cols)
        : data_(data), rows_(rows), cols_(cols) {}
};
```

大小是运行期的 `int` 成员,构造时传进来。这是绝大多数“动态数组”类的写法,PyTorch 的 `torch::Tensor` 就是这样,形状运行期才知道。

咱们的写法是:

```cpp
template <std::size_t Rows, std::size_t Cols, typename StorageType = float>
class Tensor {
    std::array<StorageType, Rows * Cols> internals_{};
    // rows_/cols_ 不存在,它们就是模板参数 Rows、Cols 本身
};
```

大小不是成员,是类型的一部分。`Tensor<4, 3>` 和 `Tensor<2, 2>` 是两个完全不同的类型,编译期就定下来,运行期改不动。

## 好处一:编译期固定大小,顺带不堆分配

既然 Rows、Cols 是编译期常量,`Rows * Cols` 也是编译期常量,`std::array<StorageType, Rows*Cols>` 的大小编译期就定了。编译器知道这个对象多大,直接在栈上或静态区开出来,不需要 new,不需要 malloc。硬约束里的“编译期固定大小”和“不堆分配”,这一步同时满足。

反过来,运行期版本那个 `float* data_`,data 指向哪里?要么指向一块 new 出来的堆内存(撞“不堆分配”),要么指向外部传入的缓冲区(生命周期得自己管,容易出悬挂引用)。两条路都有坑。

## 好处二:类型系统帮你抓形状错(最值钱的地方)

这才是维度进类型真正的威力。神经网络里大量错误本质上是形状错:权重矩阵的列数对不上输入向量的长度、两个矩阵相乘维度不匹配,等等。这些错如果形状是运行期的,只能运行期检查,崩了或返回个错误码你才知道。但如果形状是类型的一部分,**编译器在编译期就能替你挡住一大片**。

举个例子。Dense 层要求权重的列数等于输入的长度:权重 W 是 `Tensor<4, 3>`,输入 x 必须是 `Tensor<1, 3>`(那个 3 要对上)。如果哪天你手滑传了个 `Tensor<1, 5>` 的输入进去,编译器在编译阶段就拒掉,根本轮不到程序跑起来。

这种能力是动态形状的框架给不了的。PyTorch 那种运行期形状,维度不匹配要等跑到那一行代码、抛个 RuntimeError 才知道。咱们把维度塞进类型,等于让类型系统当免费的形状检查器。

具体怎么“塞”,Stage 2 写 Dense 的时候会用 `static_assert` 和模板约束把维度关系卡住,到时候你会看到它怎么在编译期挡错。这篇你只要先认下这个结论:**维度进类型,一整类形状错误就从运行期被挪到了编译期**。

## 好处三:维度编译期可见

顺带一个不那么起眼但很实在的好处。既然 Rows、Cols 是编译期常量,`row()`、`col()`、`size()` 这些查询函数就可以在编译期求值——咱们标的是 `constexpr`,够用了,写 `static_assert(tensor.size() == 12)` 直接能过。真想限定成“只能编译期求”再上 `consteval`,Stage 1 没这个刚需。

这对 Stage 5 也有用:权重要存成 `inline constexpr std::array`,它的形状得是编译期已知的常量,正好对得上 Tensor 这套“维度进类型”的设计。两边天然咬合。

## 代价:维度组合就是类型组合

诚实交代一下代价。维度进类型意味着,**每一种维度组合都是一个独立的类型**。`Tensor<4, 3>`、`Tensor<3, 4>`、`Tensor<2, 2>` 是三个互不相同的类型,写函数模板时它们是不同的实例化。

这对咱们 Lab 不是问题:MLP 的形状是固定的那几种(输入 1×3、权重 4×3 和 3×4、偏置 1×4 和 1×3),数得过来,类型膨胀可控。但如果你要写一个通用框架,接受任意形状、还要动态 reshape,这套“维度进类型”就不够用了,那得回到运行期形状的老路,PyTorch 就是这么选的。咱们是用“固定形状”换“编译期形状安全”,这笔交易对教学 Lab 划算,对通用框架不划算。各取所需。

## 五篇引入,到这就齐了

回头看这五篇:[01](./01-what-is-tensor.md) 把 Tensor 祛魅成一张二维数表,[02](./02-tensor-in-neural-network.md) 看它装输入/权重/偏置/输出四种数据,[03](./03-why-not-built-in.md) 否掉三个现成候选逼出方案,[04](./04-row-major.md) 把行主序布局定下来,这篇把维度塞进类型。Tensor 是什么、装什么、为什么这么造,五块拼图到这就齐了。

下一步是 [06-tensor.md](./06-tensor.md):把完整接口摆出来,讲三个剩下的小决策(at 返回值不返回引用、固定 2D 不做可变模板、CMake 怎么加库),然后照着接口草图动手写。前面这五篇的铺垫,就是为了让 06-tensor.md 里那些设计取舍读起来不再悬空。
