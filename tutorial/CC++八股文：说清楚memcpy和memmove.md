# C/C++八股文：说清楚memcpy和memmove

#### 一句话结论

**`memcpy` 假定源和目标不重叠（重叠是 UB），更快；`memmove` 支持重叠拷贝（定义良好），通常比 `memcpy` 稍慢但安全。**

## 关键区别（要点）

- **重叠行为**

  - `memcpy(dest, src, n)`：当 `dest` 与 `src` 所指的区域有重叠时，行为**未定义**（UB）。
  - `memmove(dest, src, n)`：当两个区域重叠时有**定义的语义**，结果相当于先把源区的 `n` 字节临时保存再复制到目标（或等价的方向感知复制），不会破坏数据。

- **API（签名）**

  ```c
  void *memcpy(void *dest, const void *src, size_t n);
  void *memmove(void *dest, const void *src, size_t n);
  ```

  都在 `<string.h>` / `<cstring>` 中，返回 `dest`。

- **实现策略（常见）**

  - `memcpy`：直接按机器字（word）或更大向量块正向拷贝，**尽可能用最速路径（SIMD/memcpy 内建）。编译器在知道无重叠时可用更激进优化。**
  - `memmove`：先检查 `dest` 与 `src` 的相对位置：
    - 若 `dest < src`：从低地址向高地址正向拷贝（安全）。
    - 若 `dest > src` 且有重叠：从高地址向低地址反向拷贝（避免覆盖还没拷贝的源字节）。
       也有实现会在小拷贝时把数据拷到内部临时缓冲区，但通常采用方向感知拷贝即可避免额外空间。

- **性能**

  - 在不重叠情况下，`memcpy` 通常最快。
  - `memmove` 需要额外判断（或可能使用更保守的路径），因此平均可能稍慢；但现代库/编译器优化良好，差距可能很小。
  - 如果编译器能证明无重叠，它可能把 `memmove` 优化为 `memcpy`。

- **语义层面（C vs C++）**

  - 在 C++ 中，对 **非平凡（non-trivially-copyable）对象** 使用 `memcpy`/`memmove` 通常是不安全/未定义的（会破坏对象语义、破坏 vptr、打破构造/析构契约）。对 POD / trivially-copyable 类型（例如纯数据结构或 `std::byte` 数组）才安全。对对象请用 `std::copy`, 移动构造或序列化方法。

- **特殊情况**

  - 如果 `n == 0`，标准允许某些实现接受 `NULL` 指针（但要小心，最好传有效指针或保证 n>0）。
  - 返回值相同：两者都返回 `dest`，便于链式调用（虽然不常用）。

重叠会出错的 `memcpy`：

```c
char buf[] = "0123456789";
// 试把起始 5 字节向左移 2 字节：结果期待 "2343456789" 还是？
// 使用 memcpy 是 UB，通常会得到错误结果
memcpy(buf, buf + 2, 5);
printf("%s\n", buf); // 未定义，可能是破坏的数据
```

同样场景用 `memmove`（安全）：

```c
char buf[] = "0123456789";
memmove(buf, buf + 2, 5); // 正确：会把 "23456" 移到起始位置
printf("%s\n", buf); // 输出 "2345656789"（按实现）
```

#### 一种简单的伪代码

```c
void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    if (d < s) {
        // 正向拷贝
        for (size_t i = 0; i < n; ++i) d[i] = s[i];
    } else if (d > s) {
        // 反向拷贝
        for (size_t i = n; i-- > 0; ) d[i] = s[i];
    }
    return dest;
}
```

## 人话

- `memcpy`：快，但**重叠 UB**。
- `memmove`：安全处理重叠（方向感知或临时缓冲），稍慢。
- 对**对象语义敏感的类型不要用这两者（非 trivially-copyable）。**
- 如果能保证不重叠用 `memcpy`；若不确定或可能重叠用 `memmove`。

