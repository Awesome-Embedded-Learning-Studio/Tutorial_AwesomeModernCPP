# Chapter 5.4 - Placement New 的使用

本目录包含《放置new（Placement New）的使用》章节的代码示例。

## 说明

在没有堆或需要精确控制内存布局的嵌入式系统中，placement new 是一个重要工具：它允许在预分配的内存上构造对象。

## 示例文件

| 文件 | 说明 | 关键点 |
|------|------|--------|
| `basic_placement_new.cpp` | Placement new基础 | 对齐方法、显式析构、异常安全 |
| `bump_allocator.cpp` | Bump分配器 | 线性分配、Arena模式、分层设计 |
| `inplace_wrapper.cpp` | RAII包装器 | InPlace类、自动析构、延迟初始化 |

## 编译方法

```bash
mkdir build && cd build
cmake ..
make
```

## 运行示例

```bash
./basic_placement_new
./bump_allocator
./inplace_wrapper
```

## 核心概念

### Placement New 基本语法

```cpp
alignas(MyType) unsigned char buffer[sizeof(MyType)];
MyType* p = new (buffer) MyType(args...);
// ... 使用 p
p->~MyType();  // 显式析构，不要用 delete
```

### 对齐方法

- **C++11/14**: `std::aligned_storage<sizeof(T), alignof(T)>::type`
- **C++17**: `std::aligned_storage_t<sizeof(T), alignof(T)>`
- **推荐**: `alignas(T) unsigned char buffer[sizeof(T)]`

### Bump/Arena分配器

- 极快分配（仅指针移动）
- 零碎片
- 不可单独释放
- 适合初始化或帧临时对象

### 使用建议

1. **必须显式析构**: placement new 对象不能使用 delete
2. **注意对齐**: 使用 alignas 确保正确对齐
3. **异常安全**: 在构造失败时正确回滚
4. **使用RAII**: 用 InPlace 包装器自动化管理
5. **Bump分配器**: 适合生命周期一致的场景

## 环境要求

- C++17 或更高
- GCC 7+ 或 Clang 5+
