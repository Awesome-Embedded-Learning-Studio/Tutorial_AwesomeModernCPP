# 001 · `mkdir -p dir/{a,b,c}` 拆解

> 学习日志。每条记录一个已达成共识的知识点。
> 出处:`tinyinfercpp-handbook.md` M0 第一步(第 244 行)。

## 原命令

```bash
mkdir -p tinyinfercpp-lab/{include/tinyinfer,src,tests,tools,examples/hello_inference}
```

## 结论:三个独立机制拼接

| 部分 | 机制 | 作用 |
|------|------|------|
| `mkdir` | make directory | 建目录 |
| `-p` | `--parents` | ① 递归建出缺失的父目录;② 幂等(目录已存在不报错,退出码 0) |
| `{a,b,c}` | bash **花括号展开(brace expansion)** | 纯 shell 文本替换,把逗号分隔项各自拼上前缀,展开成多个独立参数 |

## 实证(2026-06-30 跑通)

- **花括号展开**:`echo tinyinfercpp-lab/{include/tinyinfer,src,...}` 输出 5 个独立路径,证明是先展开成多参数再交给命令。
- **不带 `-p`**:父目录缺失时 `mkdir: cannot create directory ...: No such file or directory`,退出码 1。
- **带 `-p`**:`a/b/c` 一路补齐建成;重复执行不报错(退出码 0);不带 `-p` 重复执行报 `File exists`,退出码 1。

## 等价形式

```bash
mkdir -p tinyinfercpp-lab/include/tinyinfer
mkdir -p tinyinfercpp-lab/src
mkdir -p tinyinfercpp-lab/tests
mkdir -p tinyinfercpp-lab/tools
mkdir -p tinyinfercpp-lab/examples/hello_inference
```

## 坑

- 花括号展开是 **bash/zsh** 特性。`#!/bin/sh`(dash 等 POSIX shell)**不保证支持**,会把 `{...}` 当字面目录名建出来。本机 zsh/bash 无此问题。
- 花括号里**逗号前后不能有空格**,否则不触发展开(变成字面量)。
- 本命令必须 `-p`:要建的 `include/tinyinfer`、`examples/hello_inference` 是两层深,且父目录 `tinyinfercpp-lab/` 也还不存在。
