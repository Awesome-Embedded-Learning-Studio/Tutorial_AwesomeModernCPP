# auto与decltype 示例代码

本目录包含了Chapter 11.1 "自动类型推导：auto与decltype"的所有示例代码。

## 示例列表

### 1. basic_auto.cpp
**基础auto用法演示**
- 基本类型推导
- 范围for循环中的auto
- 复杂类型简化
- 函数返回值

**编译运行**:
```bash
g++ -std=c++17 basic_auto.cpp -o basic_auto
./basic_auto
```

### 2. auto_with_references.cpp
**auto与引用、const的推导规则**
- 引用推导规则
- const auto& 避免拷贝
- 常见陷阱演示

**编译运行**:
```bash
g++ -std=c++17 auto_with_references.cpp -o auto_with_references
./auto_with_references
```

### 3. decltype_basics.cpp
**decltype关键字基础**
- decltype保留类型精确信息
- 双括号规则 `decltype((x))`
- decltype用于表达式
- 类型别名

**编译运行**:
```bash
g++ -std=c++17 decltype_basics.cpp -o decltype_basics
./decltype_basics
```

### 4. decltype_auto_trailing_return.cpp
**decltype(auto)和尾返回类型**
- C++11尾返回类型语法
- C++14 decltype(auto)
- 完美转发返回值
- 实际应用：配置解析器

**编译运行**:
```bash
g++ -std=c++17 decltype_auto_trailing_return.cpp -o decltype_auto_trailing_return
./decltype_auto_trailing_return
```

### 5. embedded_hal_types.cpp
**嵌入式HAL库类型中的auto应用**
- 简化HAL类型声明
- 寄存器访问辅助类
- DMA配置
- 中断处理程序

**编译运行**:
```bash
g++ -std=c++17 embedded_hal_types.cpp -o embedded_hal_types
./embedded_hal_types
```

### 6. auto_pitfalls.cpp
**auto使用的常见陷阱**
- 意外的拷贝
- 代理类型（vector<bool>）
- 初始化列表推导
- decltype(auto)悬空引用
- std::function vs auto

**编译运行**:
```bash
g++ -std=c++17 auto_pitfalls.cpp -o auto_pitfalls
./auto_pitfalls
```

## 使用CMake构建

```bash
mkdir build
cd build
cmake ..
make
```

## 编译器要求

- C++17或更高版本
- 支持的编译器：GCC 7+, Clang 5+, MSVC 2017+

## 嵌入式编译

对于嵌入式平台，可以添加以下选项：

```bash
g++ -std=c++17 -fno-exceptions -fno-rtti basic_auto.cpp -o basic_auto_embedded
```

或在CMakeLists.txt中取消注释：
```cmake
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")
```
