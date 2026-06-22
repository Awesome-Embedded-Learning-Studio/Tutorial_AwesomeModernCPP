---
title: "设计模式"
description: "GoF 设计模式的现代 C++ 实现:演进式讲清每个模式解决什么问题,21 篇覆盖创造型、结构型、行为型三大类,配套可编译工程"
---

# 设计模式

设计模式是前人沉淀下来的、针对反复出现的设计问题的经典解法。这一卷覆盖 GoF 的 21 个核心模式,用**现代 C++** 把它们重新实现一遍。

和「定义 + UML + 例子」式的传统讲法不同,我们的路子是**从最直觉、最原始的写法起步,一步步逼出每个模式**——讲清楚它到底在解决什么问题、每一步演进为什么还不够、以及到了 C++17/20 有了 `std::variant` / concepts / CRTP / 模板之后,同一个模式能写出怎样更安全、更零开销的版本。

贯穿全卷的几个写法特点:每个模式都带「这里先验证一下」的真实终端输出(不是纸上谈兵);该踩的坑写成 `::: warning` 预警块;凡是能用现代 C++ 替代的旧写法,都把「经典 vs 现代」摆在一起比。

配套可编译工程在仓库的 [code/volumn_codes/vol4/design-patterns/](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns) 下,每个模式一个独立 CMake 子工程,`cmake -S . -B build && cmake --build build` 即可跑。

## 创造型(Creational)

<ChapterNav variant="sub">
  <ChapterLink href="01-singleton">单例模式:从注释约束到 Meyer's Singleton</ChapterLink>
  <ChapterLink href="02-builder">构建器模式</ChapterLink>
  <ChapterLink href="03-factory-method-abstract-factory">工厂方法与抽象工厂</ChapterLink>
  <ChapterLink href="04-prototype">原型模式</ChapterLink>
</ChapterNav>

## 结构型(Structural)

<ChapterNav variant="sub">
  <ChapterLink href="05-adapter">适配器模式</ChapterLink>
  <ChapterLink href="06-bridge">桥接模式(pImpl)</ChapterLink>
  <ChapterLink href="07-decorator">装饰器模式</ChapterLink>
  <ChapterLink href="08-composite">组合模式</ChapterLink>
  <ChapterLink href="09-facade">外观模式</ChapterLink>
  <ChapterLink href="10-flyweight">享元模式</ChapterLink>
  <ChapterLink href="11-proxy">代理模式</ChapterLink>
</ChapterNav>

## 行为型(Behavioral)

<ChapterNav variant="sub">
  <ChapterLink href="12-strategy">策略模式</ChapterLink>
  <ChapterLink href="13-command">命令模式</ChapterLink>
  <ChapterLink href="14-state">状态机模式</ChapterLink>
  <ChapterLink href="15-memento">备忘录模式</ChapterLink>
  <ChapterLink href="16-visitor">访问者模式(variant + visit)</ChapterLink>
  <ChapterLink href="17-observer">观察者模式</ChapterLink>
  <ChapterLink href="18-iterator">迭代器模式</ChapterLink>
  <ChapterLink href="19-chain-of-responsibility">责任链模式</ChapterLink>
  <ChapterLink href="20-interpreter">解释器模式</ChapterLink>
  <ChapterLink href="21-mediator">中介者模式</ChapterLink>
</ChapterNav>

## 进阶:泛型与模板模式(规划中)

经典 GoF 之外,C++ 还有自己的一套「模板级」设计手段,它们是这一卷的延伸方向,后续补齐:

- Policy-Based Design
- 类型擦除(`std::function` 的底层机制)
- CRTP(静态多态)
- Mixin 与组合式设计
- Tag Dispatching 与 `if constexpr` 分派
- NVI(Non-Virtual Interface)
- 模板与 DSL 构建
