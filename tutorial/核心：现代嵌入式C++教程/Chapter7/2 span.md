# 嵌入式C++教程：std::span——轻量、非拥有的数组视图

把 `std::span` 想象成 C++ 里的「透明的传送带」：它不拥有上面的货物（内存），只是平静又高效地告诉你“这里有多少个元素、从哪里开始”。在嵌入式里，我们经常需要把一段内存传给函数——既不想拷贝，也不想丢失类型信息或边界信息，`std::span` 就是为这种场景生的。

或者说，直到C++20，一个标准的视图容器才出现。

- `std::span<T>` 是**非拥有**（non-owning）的视图：不负责内存释放。
- 它通常是一个指针 + 长度（非常轻量，拷贝成本低）。
- 函数参数用 `std::span<const T>` 可以优雅地接受 `T[]`、`std::array`、`std::vector`、裸指针+长度 等多种来源。
- **关键注意**：不要让 `span` 的生存期超过底层数据的生存期 —— 悬垂指针依旧会把你咬一口。

------

## 引子：为什么不直接用指针或 vector？

在嵌入式代码里，我们常看到这样的函数签名：

```cpp
void process_buffer(uint8_t* buf, size_t n);
```

这招确实灵活，但缺点：读者得同时记住 `buf` 的类型、长度单位是“元素数”还是“字节数”、函数是否要修改数据……出错的地方太多。 `std::span` 把这些语义显式化：类型和值（length）都在同一个对象里，阅读性和安全性都提升了。

------

## 基本用法

```cpp
#include <span>
#include <vector>
#include <array>
#include <iostream>

void print_bytes(std::span<const uint8_t> s) {
    for (auto b : s) std::cout << std::hex << int(b) << ' ';
    std::cout << std::dec << '\n';
}

int main() {
    uint8_t buffer[] = {0x10, 0x20, 0x30};
    std::vector<uint8_t> v = {1,2,3,4};
    std::array<uint8_t, 3> a = {9,8,7};

    print_bytes(buffer);             // 从内置数组构造
    print_bytes(v);                  // 从 vector 构造
    print_bytes(a);                  // 从 std::array 构造
    print_bytes({v.data(), 2});      // 从 pointer + size 构造
}
```

`print_bytes` 用 `std::span<const uint8_t>` 接收输入：既说明了不修改内容，又接受多种容器来源，调用方无需拷贝数据。

------

## 动态与静态 extent

`std::span` 有两种形态：

- `std::span<T>`（或 `std::span<T, std::dynamic_extent>`）：运行时大小；
- `std::span<T, N>`：编译期固定元素数 `N`（称为静态 extent）。

示例：

```cpp
int arr[4];
std::span<int, 4> s_fixed(arr);      // 只有长度为 4 的数组能绑定
std::span<int> s_dyn(arr, 4);        // 任意长度，运行时记录
```

静态 `Extent` 可以在某些场景下启用额外的编译期检查或优化，但在嵌入式中，动态 extent 更常用（因为 buffer 长度常由运行时决定）。

------

## 有用的成员函数

```cpp
s.size();          // 元素个数
s.size_bytes();    // 字节数（注意！元素个数 * sizeof(T)）
s.data();          // 指向首元素的指针（可能为 nullptr 当 size()==0）
s.empty();
s.front(), s.back();
s[i];              // 下标，不做运行时检查（与 operator[] 语义一致）
s.subspan(offset, count);   // 切片，返回新的 span（仍为 non-owning）
s.first(n), s.last(n);     // 前 n 个或后 n 个元素视图
std::as_bytes(s);          // 将 span<T> 视为 span<const std::byte>
std::as_writable_bytes(s); // 视为 span<std::byte>（当 T 可写时）
```

注意：`operator[]` 不检查越界；如果需要边界检查，自行用 `at`-like wrapper 或在调试时加断言。

------

## 进阶示例：subspan 与字节操作

```cpp
#include <span>
#include <cstddef> // for std::byte

void recv_packet(std::span<uint8_t> buffer) {
    if (buffer.size() < 4) return;
    auto header = buffer.first(4);
    uint16_t len = header[2] | (header[3] << 8);

    if (buffer.size() < 4 + len) return;
    auto payload = buffer.subspan(4, len);

    // 把 payload 当作字节流传给 CRC 函数
    auto bytes = std::as_bytes(payload);
    // crc_check(bytes.data(), bytes.size()); // 示例：调用检验函数
}
```

这种把整体 buffer 切片成 header/payload 的写法尤其适合嵌入式协议解析，简洁而安全（只要你保证传进来的 `buffer` 有效）。

------

## 当做函数参数的最佳实践

把 API 设计成接收 `std::span` 有几个好处：

- 调用者可以传入数组、`std::array`、`std::vector` 或裸指针+长度；
- 函数签名清楚地表达“这是一个视图（可能只读）”；
- 函数内不需要 template 泛型来支持各种容器。

示例：

```cpp
void process(std::span<const int> data); // 明确：不修改数据
void mutate(std::span<int> data);         // 明确：会修改数据
```

这比写 `template<class Container> void process(const Container& c)` 更直观，也避免了不必要的编译膨胀。

------

## 常见坑

1. **悬垂视图**：最常见错误。不要把 `std::span` 绑定到局部 `std::vector` 的 `data()` 并把它返回给调用者：

   ```cpp
   std::span<int> bad() {
       std::vector<int> v = {1,2,3};
       return v; // ❌ v 被销毁，返回的 span 悬垂
   }
   ```

2. **以为有所有权**：span 不持有内存，不会析构或释放。若需要所有权，用 `std::vector`、`unique_ptr` 等。

3. **不恰当的字节视图**：`std::as_bytes` 返回 `span<const std::byte>`，用于只读字节访问；`as_writable_bytes` 仅在底层可写时使用。

4. **越界访问**：`operator[]` 不检查边界。必要时做显式检查或使用调试断言。

5. **不是以 null 结尾的字符串**：`std::span<char>` 不是 `C` 字符串，不保证以 `'\0'` 结尾。处理字符串请用 `std::string_view` 或明确长度处理。

------

## 与 `std::string_view` 的对比

- `std::string_view` 是专门为字符序列设计的（只读视图），并带有字符串语义（常用于文本）。
- `std::span<char>`/`std::span<std::byte>` 通用于任意元素类型，包括可写情况。
  在处理二进制协议/缓冲区时，`std::span` 更合适；处理不可变文本时，用 `string_view` 更语义化。

------

## 嵌入式场景快速举例

- DMA 回调把数据放进固定 buffer，回调把 `std::span` 传给处理函数，无需拷贝。
- 从 Flash 读出数据到缓冲区，然后用 `std::span` 切片解析头和块。
- 在中断或实时路径中传递小段数据，`span` 的拷贝开销极低。

------

## 代码小贴士

1. 将函数参数写成 `std::span<const T>`，以表达只读意图。
2. 若想允许传入大小为 N 的 buffer，但不更改逻辑，可接受 `std::span<T, N>`（静态 extent）。
3. 使用 `subspan`, `first`, `last` 构造子视图，而非手动计算指针偏移。
4. 在公共 API 文档里明确说明：**span 不负责生命周期管理**。

------

## 速查 API

`s` 为 `std::span<T>`：

- `s.size()`, `s.size_bytes()`, `s.data()`, `s.empty()`
- `s[i]`（无边界检查）、`s.front()`、`s.back()`
- `s.begin()`, `s.end()`（支持范围 for）
- `s.subspan(offset, count)`, `s.first(n)`, `s.last(n)`
- `std::as_bytes(s)`、`std::as_writable_bytes(s)`

