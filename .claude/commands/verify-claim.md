---
description: 验证一条 C++ 断言 —— 编译最小例子 + 查 cppreference,给出成立/不成立的实证(金科玉律固化版)
argument-hint: "[必填: 要验证的 C++ 断言]"
---

# /verify-claim — 验证一条 C++ 断言

C++ 语义随标准版本和实现变化,**禁止凭记忆断言**。本命令把这条金科玉律变成可执行流程:对一条断言,**用真实编译输出说话**。

> 命名说明:叫 `/verify-claim` 而非 `/verify`,是为了避开 Claude Code 内置的 `/verify`(验证代码改动是否生效)。本命令专做"C++ 断言的编译实证"。

## 输入

`$ARGUMENTS` —— 要验证的断言(如"`std::move` 作用于 `const` 对象会退化为拷贝")。为空则停下问。

## 流程

1. **写最小复现**:用 Write 把最小 `.cpp` 写到 `/tmp/`(`verify_<主题>.cpp`),只覆盖断言涉及的行为,别塞无关代码。
2. **编译**(单独一次 Bash):`g++ -std=c++20 -O2 -o /tmp/verify_xxx /tmp/verify_xxx.cpp`;要看生成的指令再加一次 `g++ -std=c++20 -O2 -S -o /tmp/verify_xxx.s /tmp/verify_xxx.cpp`。
3. **运行**(单独一次 Bash):`/tmp/verify_xxx`。**不要和编译用 `&&` 串。**
4. **查标准**(必要时):web search cppreference,标标准版本。
5. **记录编译器**:`g++ --version`。

## 输出

```
断言：<原文>
结论：成立 / 不成立 / 部分成立（说清条件）
标准：C++XX  |  编译器：GCC XX
最小例子：<code>
真实输出：<粘贴>
依据：cppreference 链接 / 汇编片段（如适用）
```

## 硬约束

- 编译产物**只到 `/tmp/`**;**禁止**在项目目录跑 cmake / make / build。
- 编译和运行**分两次 Bash**,不串 `&&`。
- 不确定就如实说"无法确定",**别编输出**。
