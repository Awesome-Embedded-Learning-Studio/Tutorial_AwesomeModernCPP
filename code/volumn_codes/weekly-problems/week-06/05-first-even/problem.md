实现 `first_even`:返回向量 v 里第一个偶数;一个都没有,返回 `std::nullopt`。例:传 `{1, 3, 4, 5}` 得 4;传 `{1, 3}` 得 `std::nullopt`。

返回类型是 `std::optional<int>`。判题的断言里,返回值直接和 `4` 比,也直接和 `std::nullopt` 比——optional 把这两种比较都接得住,您照着写就是。
