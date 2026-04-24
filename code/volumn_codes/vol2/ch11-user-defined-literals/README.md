# 用户自定义字面量示例代码

本目录包含《用户自定义字面量》章节的验证代码。

## 文件说明

- `01-udl-basics.cpp` - 基础 UDL 示例：整型、浮点型、字符串哈希、标准库字面量
- `02-udl-practice.cpp` - 实战示例：类型安全单位系统、温度转换、嵌入式应用

## 编译方法

### 方法 1: 使用 CMake

```bash
mkdir build && cd build
cmake ..
make
./01_udl_basics
./02_udl_practice
```

### 方法 2: 直接使用 g++

```bash
g++ -std=c++14 -Wall -Wextra -O2 01-udl-basics.cpp -o 01_udl_basics
g++ -std=c++14 -Wall -Wextra -O2 02-udl-practice.cpp -o 02_udl_practice
./01_udl_basics
./02_udl_practice
```

## 验证要点

### 01-udl-basics.cpp

- 整型和浮点型 UDL 重载
- 字符串哈希字面量的编译期计算
- 标准 chrono 和 string 字面量的使用
- 整数溢出的未定义行为演示

### 02-udl-practice.cpp

- 类型安全的单位系统（长度、时间、速度）
- 标量乘法的类型推导问题及解决方案
- 温度单位转换（摄氏度、华氏度、开尔文）
- 嵌入式应用：频率计算、波特率、内存布局
- 字符串哈希用于 switch-case

## 技术要点

### 类型推导问题

原始的模板实现：

```cpp
template <typename T, typename UnitTag>
constexpr Quantity<T, UnitTag> operator*(T scalar, Quantity<T, UnitTag> q);
```

问题：`2 * 100.0_m` 会失败，因为 `T` 被推导为 `int` 和 `long double`，类型不匹配。

解决方案：添加额外的重载：

```cpp
template <typename UnitTag>
constexpr Quantity<long double, UnitTag> operator*(
    int scalar, Quantity<long double, UnitTag> q);
```

### 整数溢出

UDL 中的乘法可能导致未定义行为：

```cpp
constexpr Bytes operator""_KiB(unsigned long long v) {
    return Bytes{v * 1024};  // 大 v 会溢出！
}
```

解决方案：在文档中注明范围限制，或使用运行时检查。

### 浮点字面量要求

如果只定义了 `long double` 重载：

```cpp
constexpr Frequency operator""_MHz(long double v);
```

则必须使用浮点字面量：

```cpp
auto f = 72.0_MHz;  // 正确
auto f = 72_MHz;    // 编译错误！
```

## 相关文章

- `/documents/vol2-modern-features/ch11-user-defined-literals/01-udl-basics.md`
- `/documents/vol2-modern-features/ch11-user-defined-literals/02-udl-practice.md`
