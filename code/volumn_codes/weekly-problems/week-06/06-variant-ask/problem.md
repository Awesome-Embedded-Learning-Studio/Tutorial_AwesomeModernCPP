`fetch_int` 收到一个 `std::variant<int, std::string>`:此刻里面存的是 int,就把这个 int 返回;存的是 string,返回 -1。例:传 42 得 42,传字符串 `"hi"` 得 -1。

存的是谁,variant 自己记得。您要做的,是先问它、再取值。不问就取会发生什么,展开参考答案时咱们细说。
