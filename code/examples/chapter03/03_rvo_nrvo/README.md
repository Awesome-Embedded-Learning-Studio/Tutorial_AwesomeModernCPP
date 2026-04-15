# Chapter 3.3 - RVO/NRVO 示例

本目录包含《RVO, NRVO》章节的代码示例。

## 说明

RVO (Return Value Optimization) 和 NRVO (Named Return Value Optimization) 是编译器优化技术，可以消除函数返回对象时的拷贝/移动开销。

## 编译方法

```bash
mkdir build && cd build
cmake ..
make
./rvo_nrvo_example
```

## 关键点

1. **RVO**：返回临时对象时，直接在调用点构造
2. **NRVO**：返回命名对象时，直接在调用点构造
3. **C++17 保证**：复制省略（guaranteed elision）
4. **不要 return std::move**：会阻止 RVO/NRVO！

## 编译器优化

启用优化后（-O2 或更高），编译器会自动应用 RVO/NRVO：
```bash
g++ -O2 -std=c++17 rvo_nrvo_example.cpp
```

## 环境要求

- C++17 或更高（C++17 保证复制省略）
- GCC 7+ 或 Clang 5+
