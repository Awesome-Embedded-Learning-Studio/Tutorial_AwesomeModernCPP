编辑器里的 `Box` 会记录自己是被拷贝还是被移动:每被拷贝一次,`SwapStats::copies` 加一;每被移动一次,`SwapStats::moves` 加一。`swap_check(a, b)` 会造两个 `Box`,值分别设成 a 和 b,调一次您的 `my_swap`,然后汇报:值换没换对、过程里发生了几次拷贝。

您要实现的是:

```cpp
template <typename T>
void my_swap(T& a, T& b) {
    // 值要换对,一次拷贝都不许发生
}
```

判题就看两条:**值换对了**、**拷贝数为 0**。用移动语义写交换——这正是标准库 `std::swap` 自 C++11 以来的写法,`std::move` 已经在 `<utility>` 里等您。辅助代码(`Box`、`swap_check` 等)在编辑器上半部分,请勿改动。
