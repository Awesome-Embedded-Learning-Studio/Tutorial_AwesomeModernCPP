**思路**:

这题五行写完,值得您停下来看的是 optional 的两下比较。先看实现:

```cpp
std::optional<int> first_even(const std::vector<int>& v) {
    for (int x : v) {
        if (x % 2 == 0) {
            return x;
        }
    }
    return std::nullopt;
}
```

循环里撞见第一个偶数,`return x;` 直接返回 int——函数要的是 optional,这一步会把 4 自动装进 `optional<int>` 再交出去,您不用手写构造。循环走完一次都没触发,落到最后 `return std::nullopt;`,「没有」也交得干干净净。

再看判题里那两种比较为什么能直接写:`first_even({1, 3, 4, 5}) == 4`,左边有值、存的是 4,等号成立;`first_even({1, 3, 5}) == std::nullopt`,左边没存值,等号也成立。optional 和值比、和 nullopt 比,这两套比较语言里都自带,不用您拆开 `.value()` 再比。

顺带一个值得亲手试的事实:传 `{0}` 进去,返回的是「存着 0」,和「没存」分得清清楚楚。笔者实测,`has_value()` 为真、值等于 0、和 nullopt 不相等。拿 0 冒充「没找到」的年头,有了 optional 就该让它退休了。

**排错**:

还是不通过?最常见的一步,是「没找到」时返回 0 之类的特殊值来冒充:`first_even({1, 3, 5}) == std::nullopt` 这条挂,因为存着 0 的 optional 和「没存」不相等,判题一眼识破。方向相反的错,是判奇偶写反(`x % 2` 那个条件取了奇数),症状是 `first_even({1, 3, 4, 5})` 返回 1 而不是 4,第一条断言就把您拦下。
