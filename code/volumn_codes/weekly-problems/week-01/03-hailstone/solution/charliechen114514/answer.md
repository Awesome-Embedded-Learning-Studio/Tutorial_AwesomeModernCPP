**思路**:

看到 “冰雹序列” 这四个大字，是不是第一反应想开个 `vector` 把整个序列存下来再打印？别急，这回玩的不是递归，是 `std::generator`——一个 C++23 的协程小玩具。先问问自己：嘿！咋能一边算一边往外吐数字，还不用提前把整条序列憋在内存里呢？答案就是 `co_yield`。算一个、吐一个、再算下一个，序列想多长就多长，调用方爱拿几个拿几个。

对着代码看：

```cpp
std::generator<int> hailstone(int n) {
    while (true) {
        co_yield n;
        if (n == 1) {
            while (true) co_yield 1;  // 序列末尾之后,无限产出 1，这也是递归的一个终点~
        }

        // 更新n
    }
}
```

这几行就是核心。`while (true)` 是个无限循环，每次先把当前的 `n` 用 `co_yield n` 吐出去，挂起自己，等调用方再来要。然后判断：如果 `n == 1`，说明序列到头了，进入内层 `while (true) co_yield 1;`，之后永远只吐 1，再也不往下算——这样调用方就算不 break，也不会再触发新的计算，只是拿到一串没完没了的 1。否则就老老实实按冰雹规则算下一步：偶数除 2，奇数乘 3 加 1，`n = (n % 2 == 0) ? n / 2 : 3 * n + 1;`，干净利落。

再对着 `main` 看：

```cpp
int main() {
    int n = 0;
    std::cin >> n;
    bool first = true;
    for (int value : hailstone(n)) {
        if (!first) {
            std::cout << ' ';
        }
        std::cout << value;
        first = false;
        if (value == 1) {
            break;
        }
    }
    std::cout << '\n';
}
```

`for (int value : hailstone(n))` 这个范围 for 就是协程的调用方，每次迭代向生成器要一个值，拿到就打印。`first` 那个布尔量只是用来控制空格，第一个数字前面不加空格，后面的都加一个，避免行尾多出空格。最后 `if (value == 1) break;` 是调用方自己喊停——因为生成器在 1 之后会无限吐 1，不 break 的话这个 for 永远出不来。`break` 一执行，生成器被销毁，协程也跟着结束，干干净净。

整个程序没有 `vector`、没有提前算完整条链、也没有手写状态机.但是看着很爽，对吧，这就是C++20引入的协程~。
