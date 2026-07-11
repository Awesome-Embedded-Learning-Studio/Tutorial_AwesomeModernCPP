---
chapter: 99
cpp_standard:
- 23
description: 多维数组的非拥有视图，span 的多维推广，零拷贝索引二维及以上连续数据
difficulty: intermediate
order: 11
reading_time_minutes: 3
tags:
- host
- cpp-modern
- intermediate
- span
- 容器
title: std::mdspan
---
<!--
参考卡：std::mdspan（C++23）。span 的多维推广，本机 GCC 16.1.1 -std=c++23 实测。
索引坑：GCC 16 的 mdspan 用 C++23 多维 operator[] (m[i,j])，旧 operator() 写法已不工作。
-->

# std::mdspan（C++23）

## 一句话

`std::span` 的多维推广：一段连续内存上的多维视图，不拥有数据，编译期或运行期维度均可，零开销索引 `m[i, j]`。

## 头文件

`#include <mdspan>`

## 核心 API 速查

| 操作 | 签名 | 说明 |
|------|------|------|
| 固定维度 | `mdspan<T, extents<size_t, R, C>>` | 行列编译期已知 |
| 动态维度 | `mdspan<T, dextents<size_t, Rank>>` | 维度运行期给定 |
| 索引 | `m[i, j]` | 多维下标（C++23 P2128），注意是 `[]` 不是 `()` |
| 行数 | `m.extent(0)` | 第 0 维大小 |
| 列数 | `m.extent(1)` | 第 1 维大小 |
| 元素总数 | `m.size()` | 所有维度乘积 |
| 秩（维数） | `m.rank()` | 几维 |
| 底层指针 | `m.data_handle()` | 拿到原始指针 |
| 布局 | `layout_right`（默认）/ `layout_left` / `layout_stride` | 行主序/列主序/自定义步长 |

## 最小示例

```cpp
// Standard: C++23
#include <mdspan>
#include <cstdio>

int main() {
    int data[2 * 3] = {1, 2, 3, 4, 5, 6};

    // 固定维度：行列编译期已知
    std::mdspan<int, std::extents<std::size_t, 2, 3>> m(data);
    std::printf("rows=%zu cols=%zu  m[0,0]=%d m[1,2]=%d\n",
                m.extent(0), m.extent(1), m[0, 0], m[1, 2]);

    // 动态维度：行列运行期给定
    std::mdspan<int, std::dextents<std::size_t, 2>> dm(data, 2, 3);
    std::printf("dm[1,1]=%d size=%zu\n", dm[1, 1], dm.size());
}
```

实测输出（GCC 16.1.1，`-std=c++23`）：

```text
rows=2 cols=3  m[0,0]=1 m[1,2]=6
dm[1,1]=5 size=6
```

## 嵌入式适用性：中

- 零拥有视图，不分配，大小只是一个指针加 extents（几十字节），适合内存受限场景
- 多维传感器数据（图像、矩阵、ADC 采样）零拷贝传递，替代手算 `row*cols+col` 下标
- 编译期固定维度（`extents`）版本可被优化到完全寄存器化，无运行期开销
- 注意 `operator[]` 多参是 C++23 才有；老资料用 `operator()` 的写法在 GCC 16 上已编不过

## 编译器支持

| GCC | Clang | MSVC |
|-----|-------|------|
| 14 | 17 | 19.36 |

## 另见

- [cppreference: std::mdspan](https://en.cppreference.com/w/cpp/header/mdspan)
- [std::span 一维视图](01-span.md)

---

*部分内容参考自 [cppreference.com](https://en.cppreference.com/)，采用 [CC-BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/) 许可*
