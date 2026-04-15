# Chapter 7.1 - std::array 示例

本目录包含《std::array：编译期固定大小数组》章节的所有代码示例。

## 说明

`std::array<T, N>` 是一个封装了 C 风格数组的轻量类模板：大小 N 在编译期就确定，提供 STL 风格的接口，且通常不会比原始数组多多少运行开销。

## 为什么在嵌入式喜欢它

- **零动态分配**：没有 `new` / `malloc`，适合无堆或受限内存环境
- **可预测内存布局**：编译期大小、连续存储，方便用于 DMA、裸指针接口
- **STL 友好**：可以直接传给算法和容器适配器
- **constexpr 支持**：可以用作编译期查表或常量数据
- **类型安全与自文档化**：`std::array<uint8_t, 128>` 明确表达意图

## 示例文件

| 文件 | 说明 | 关键点 |
|------|------|--------|
| `basic_usage.cpp` | 基本用法演示 | 初始化、访问、STL 算法兼容 |
| `static_storage.cpp` | 静态存储 vs 栈存储 | 大数组应放静态区，避免栈溢出 |
| `dma_usage.cpp` | DMA/外设使用场景 | 连续内存保证，`.data()` 传给 HAL |
| `constexpr_table.cpp` | 编译期查表 | constexpr 函数生成常量表 |
| `constexpr_crc_table.cpp` | 编译期 CRC 表 | 复杂编译期计算示例 |
| `structured_binding.cpp` | 结构化绑定 | C++17 特性，`std::get` 和解构 |
| `c_api_compatible.cpp` | C API 兼容 | 使用 `.data()` 和 `.size()` |

## 编译方法

```bash
mkdir build && cd build
cmake ..
make
```

## 运行示例

```bash
./basic_usage
./static_storage
./dma_usage
./constexpr_table
./constexpr_crc_table
./structured_binding
./c_api_compatible
```

## 环境要求

- C++17 或更高
- GCC 7+ 或 Clang 5+

## 嵌入式注意事项

1. `.at()` 在异常被禁用或不可用的裸机环境下不适合；用 `operator[]` 并保持索引正确
2. 小数组可放在栈上，较大数组应放为 `static` 或放在 `.bss`
3. 用于 DMA 时，元素类型应该是 trivially copyable
4. `constexpr` 数据可放进 flash，节省 RAM
