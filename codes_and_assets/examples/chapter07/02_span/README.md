# Chapter 7.2 - std::span 示例

本目录包含《std::span——轻量、非拥有的数组视图》章节的所有代码示例。

## 说明

`std::span<T>` 是**非拥有**（non-owning）的视图：不负责内存释放。它通常是一个指针 + 长度（非常轻量，拷贝成本低）。

## 为什么使用 std::span

- 函数参数用 `std::span<const T>` 可以优雅地接受 `T[]`、`std::array`、`std::vector`、裸指针+长度 等多种来源
- 不拷贝数据，零开销视图
- 类型和长度信息绑定在一起，比分离的指针+长度更安全
- 支持切片操作（subspan）

## 示例文件

| 文件 | 说明 | 关键点 |
|------|------|--------|
| `basic_usage.cpp` | 基本用法演示 | 接受多种容器来源，统一接口 |
| `static_extent.cpp` | 静态 vs 动态 extent | 编译期大小 vs 运行时大小 |
| `subspan_example.cpp` | 切片操作 | first, last, subspan |
| `packet_parsing.cpp` | 协议包解析 | 实际嵌入式场景应用 |
| `bytes_view.cpp` | 字节视图 | as_bytes, as_writable_bytes |
| `function_parameter.cpp` | 函数参数最佳实践 | const 正确性，API 设计 |

## 编译方法

```bash
mkdir build && cd build
cmake ..
make
```

## 运行示例

```bash
./basic_usage
./static_extent
./subspan_example
./packet_parsing
./bytes_view
./function_parameter
```

## 环境要求

- **C++20 或更高**（std::span 是 C++20 引入的）
- GCC 10+ 或 Clang 10+
- 启用 -O2 优化

## 嵌入式注意事项

1. **不要让 span 的生存期超过底层数据的生存期** —— 悬垂指针依旧会把你咬一口
2. `operator[]` 不检查边界，必要时做显式检查或使用调试断言
3. span 不持有内存，不会析构或释放
4. 不是以 null 结尾的字符串，处理字符串请用 `std::string_view`
