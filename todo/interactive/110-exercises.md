---
id: "110"
title: "练习题和编程作业系统"
category: interactive
priority: P3
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: ["002"]
blocks: []
estimated_effort: medium
---

# 练习题和编程作业系统

## 目标

为每篇教程添加文末练习题系统，帮助读者巩固所学内容。练习题分为两类：

1. **思考题**：概念性问题，检验对理论的理解
2. **编程作业**：动手实践任务，基于教程中的代码进行扩展

同时设计配套的参考答案体系（使用折叠块隐藏），方便读者自测。

## 验收标准

- [ ] 定义练习题的 Markdown 标准格式和模板
- [ ] 为至少 10 篇核心教程添加文末练习题（每篇 3-5 道思考题 + 1-2 道编程作业）
- [ ] 所有练习题都有对应的参考答案（折叠块隐藏）
- [ ] 练习题难度与文章 difficulty 标签匹配
- [ ] 编程作业有明确的输入/输出要求或验收标准
- [ ] 练习题区域使用统一的 admonition 样式，视觉上与正文区分
- [ ] 评估在线评测（OJ）集成的可行性并记录结论

## 实施说明

### 练习题格式标准

```markdown
---

## 练习题

### 思考题

**1.** RAII 的核心原则是什么？为什么它在嵌入式开发中特别重要？

??? success "参考答案"
    RAII (Resource Acquisition Is Initialization) 的核心原则是：资源的获取在对象构造时完成，资源的释放由对象析构函数自动完成。
    
    在嵌入式开发中特别重要，因为：
    - 嵌入式系统资源有限，必须确保资源（内存、外设、中断）及时释放
    - 异常或提前 return 容易导致资源泄漏
    - RAII 提供了确定性的资源管理，不依赖 GC

**2.** 为什么嵌入式 C++ 通常避免使用动态内存分配？有哪些替代方案？

??? success "参考答案"
    ...

### 编程作业

**作业 1：扩展 GPIO 类**

为 `GpioPin` 类添加以下功能：
- `toggle()` 方法：翻转引脚电平
- `read()` 方法：读取引脚当前电平（输入模式）

要求：
- 使用寄存器直接操作
- 编写测试代码验证功能
- 注意：使用 `^` 运算符实现翻转

??? success "参考答案"
    ```cpp
    void GpioPin::toggle() {
        register_->ODR ^= (1U << pin_);
    }
    
    bool GpioPin::read() const {
        return (register_->IDR & (1U << pin_)) != 0;
    }
    ```
```

### 练习题模板

```markdown
## 练习题

### 思考题

<!-- 3-5 道概念性问题 -->

**1.** [问题]
??? success "参考答案"
    [答案]

**2.** [问题]
??? success "参考答案"
    [答案]

**3.** [问题]
??? success "参考答案"
    [答案]

### 编程作业

<!-- 1-2 道动手任务 -->

**作业 1：[标题]**

**目标**：描述

**要求**：
- 要点 1
- 要点 2

**提示**：
> 提示内容

??? success "参考答案"
    [代码 + 解释]
```

### 难度分级策略

| 文章难度 | 思考题 | 编程作业 |
|----------|--------|----------|
| beginner | 概念回忆、定义解释 | 修改示例代码、简单扩展 |
| intermediate | 原理分析、对比讨论 | 实现新功能、组合多个概念 |
| advanced | 设计权衡、性能分析 | 完整模块设计、优化改进 |

### Admonition 样式

```css
/* 练习题区域样式 */
.admonition.exercise {
  border-left-color: #2196F3;
  margin-top: 2em;
}

.admonition.exercise > .admonition-title {
  background-color: rgba(33, 150, 243, 0.1);
}

/* 参考答案样式 */
.admonition.success {
  border-left-color: #4CAF50;
}

.admonition.success > .admonition-title {
  background-color: rgba(76, 175, 80, 0.05);
}
```

### 优先添加练习题的文章

1. C++ 基础系列（理论性强，需要练习巩固）
2. GPIO 输入输出（入门级，适合动手）
3. UART 串口通信（通信协议理解）
4. 中断和 EXTI（概念理解）
5. Timer/PWM（时序概念）
6. RTOS 任务管理（调度概念）
7. RTOS 同步机制（并发思维）
8. 内存管理（RAII/智能指针）
9. 模板元编程（编译期计算）
10. 设计模式在嵌入式中的应用

### 在线评测评估

**方案评估**：

| 方案 | 优点 | 缺点 | 可行性 |
|------|------|------|--------|
| 嵌入 Judge0 | 在线运行代码 | 需要服务器，安全风险 | 低 |
| GitHub Actions 自动检查 | 免费额度，安全 | 提交流程长 | 中 |
| 本地测试框架 | 简单，离线可用 | 需要读者配置环境 | 高 |
| 纯文本答案对照 | 零成本，简单 | 只适合选择题 | 高 |

**建议**：初期使用纯文本答案对照 + 本地测试框架，后续考虑 GitHub Actions 自动检查编程作业。

## 涉及文件

- 所有教程 `.md` 文件 — 添加练习题章节
- `documents/stylesheets/extra.css` — 练习题样式
- `.templates/exercise-template.md` — 练习题模板

## 参考资料

- [MkDocs Material Admonitions](https://squidfunk.github.io/mkdocs-material/reference/admonitions/)
- [Judge0 在线评测](https://judge0.com/)
- [Exercism 练习模式](https://exercism.org/)
