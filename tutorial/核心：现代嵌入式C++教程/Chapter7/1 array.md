# 嵌入式现代C++教程——std::array：编译期固定大小数组

你写嵌入式代码时，堆（heap）常常像个不可靠的室友：随时可能把屋子掀翻。`std::array` 就像那位稳重但不多话的朋友——编译期确定大小、占栈或静态存储、没有动态分配，性能可预测，语义清楚。我们的一个重点就是——他跟传统的C风格数组比起来，怎么样的事情。

------

## 什么是 `std::array`

`std::array<T, N>` 是一个封装了 C 风格数组的轻量类模板：大小 `N` 在编译期就确定，提供 STL 风格的接口（`.size()`、`.begin()`、`.data()`、`operator[]`、迭代器等），且通常不会比原始数组多多少运行开销。

------

## 为什么在嵌入式喜欢它

- **零动态分配**：没有 `new` / `malloc`，适合无堆或受限内存环境。
- **可预测内存布局**：编译期大小、连续存储，方便用于 DMA、裸指针接口。
- **STL 友好**：可以直接传给算法（`std::sort`、`std::fill`）和容器适配器。
- **constexpr 支持**：可以用作编译期查表或常量数据。
- **类型安全与自文档化**：`std::array<uint8_t, 128>` 明确表达意图，比 `uint8_t buf[128]` 更现代。

------

## 基本用法（代码示例）

```cpp
#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>

int main() {
    std::array<uint8_t, 8> buf{}; // value-initialized -> all zeros
    buf[0] = 0xAA;
    buf.at(1) = 0x55; // .at 会做边界检查（抛异常）
    
    // 兼容 STL 算法
    std::fill(buf.begin(), buf.end(), 0xFF);

    // 传给 C API（不会隐式退化）：使用 data()
    // c_function(buf.data(), buf.size());

    for (auto b : buf) std::cout << int(b) << ' ';
    std::cout << '\n';
}
```

小提醒：`.at()` 在异常被禁用或不可用的裸机环境下不适合；用 `operator[]` 并保持索引正确。

------

## 与 C 数组、`std::vector` 的比较

- 和 **C 数组**：`std::array` 是包起来的类，支持 `.size()`、迭代器、`std::get`、结构化绑定，且能作为对象被拷贝/赋值。底层仍是连续内存。
- 和 **std::vector**：`vector` 可动态调整大小（需堆），`std::array` 无堆、大小固定、开销更小、语义更明确，嵌入式通常更倾向 `std::array`。

------

## 常见技巧和细节（嵌入式角度）

### 1. 放静态区还是栈上？

- 小数组（几十、几百字节）可放在栈上。注意任务/ISR 的栈深度限制。
- 较大数组应放为 `static` 或放在 `.bss`，或放入只读闪存（`constexpr` 数据）以节省 RAM。

示例：

```cpp
static std::array<uint8_t, 1024> big_buf; // 在 .bss，程序启动后分配
```

### 2. 用于 DMA / 外设

因为 `std::array` 保证连续内存，你可以安全地传 `arr.data()` 给 DMA 或 HAL。但确保元素类型是 **可复制且没有需要特殊构造的复杂类型**（一般使用 POD 或 trivial 类型）。

### 3. 编译期表与 `constexpr`

`std::array` 可用于编译期常量查表（免运行时初始化）：

```cpp
#include <array>

constexpr std::array<int, 5> make_table() {
    return {0, 1, 4, 9, 16};
}

constexpr auto table = make_table(); // 存在于只读段，可放进 flash
static_assert(table[3] == 9);
```

如果你需要在编译期生成一个更复杂的表，可以配合 `std::index_sequence` 做元编程（不赘述复杂实现，这里先给出思路：用 `index_sequence` 展开索引并在 constexpr 函数中产生元素`）。

### 4. 结构化绑定与 `std::get`

`std::array` 支持 `std::get<0>(arr)` 和结构化绑定（C++17）：

```cpp
std::array<int, 3> a = {1,2,3};
auto [x,y,z] = a; // nice for small fixed tuples
```

### 5. 避免退化为指针的陷阱

C 风格数组在传参时会退化为指针，而 `std::array` 不会，你必须明确传 `.data()` 或 `.size()`：

```cpp
void c_api(uint8_t* p, size_t n);
std::array<uint8_t, 16> arr;
c_api(arr.data(), arr.size());
```

### 6. 与裸机异常策略的兼容性

某些嵌入式编译链把异常支持关掉，这会影响 `.at()`（抛异常）的使用。建议在无异常环境下只用 `operator[]` 并在编译期/开发期做边界检查工具。

------

## 高级话题：当元素不是 POD 时

`std::array<T, N>` 的元素可以是任意类型 `T`。但在嵌入式里常见注意点：

- 如果 `T` 有复杂构造/析构，静态初始化（尤其零初始化）行为会不同，要保证构造成本被接受。
- 对于需要通过 DMA 读写的缓冲区，`T` 应该是 trivially copyable。

## 可以一试——把 `std::array` 用作编译期 CRC 表

```cpp
#include <array>
#include <cstdint>

constexpr std::array<uint32_t, 256> make_crc_table() {
    std::array<uint32_t, 256> t{};
    for (size_t i = 0; i < 256; ++i) {
        uint32_t crc = static_cast<uint32_t>(i);
        for (int j = 0; j < 8; ++j)
            crc = (crc & 1) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        t[i] = crc;
    }
    return t;
}

constexpr auto crc_table = make_crc_table(); // 编译期计算，放到只读段（若编译器支持）
```

在支持的工具链上，这样可以把查表数据放进 flash，节省 RAM。

