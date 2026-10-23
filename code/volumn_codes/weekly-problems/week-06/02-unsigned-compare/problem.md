「-1 比 1 小」,这在数学上没争议。可下面两行(头文件就当已包含)喂给编译器,打印出来的是 0:

```cpp
unsigned u = 1;
std::cout << (-1 < u);   // 打印 0
```

您这题的任务,是实现 `safe_less(int a, unsigned b)`,当且仅当 a 的数学值小于 b 的数学值,返回 true,把上面那个毛病绕开。例:`safe_less(-1, 1u)` 要返回 true,`safe_less(3, 2u)` 要返回 false。判题一共六条断言,负数那两条,专等着 `return a < b;` 这种写法撞上去。
