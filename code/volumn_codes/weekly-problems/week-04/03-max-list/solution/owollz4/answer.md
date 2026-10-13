**思路**:

投稿人 owollz4 的原稿就是参考答案,思路由笔者代述。

这题练的是 va_list 四件套。对着 solution.cpp 看,四件套各司其职:

```cpp
    va_list args;
    int max = first;
    va_start(args, first);
    int tmp;
    while ((tmp = va_arg(args, int)) >= 0)
    {
        if (tmp > max)
            max = tmp;
    }
    va_end(args);
```

`va_list` 声明游标,`va_start(args, first)` 让游标从 `first` 之后开始读,`va_arg(args, int)` 每次取一个并按 int 解释,`va_end` 收尾。别因此以为 first 被排除在外——它自己也是候选之一,`max_list(42, -1)` 得 42 靠的就是它。循环条件一行做两件事:取一个数,顺手查哨兵。读到负值就停,负值只报结束,不参与比大小。判题里 `max_list(5, -2, -3, -1)` 得 5,这组提醒您哨兵不必长成 -1,列表中段一露负值就该收工。开头的 `if(first < 0) return -1;` 是约定之外的防御:第一个参数就是负的,列表视作无效。

再往深看一层:va_arg 既不检查类型也不检查个数,您说取 int 它就按 int 取,实参真给了 double,取出来的是垃圾还没人报警。这份不管不问是 C 的老传统,垃圾值和没人报警就是它的代价。C++ 后来拿可变参数模板换掉了它,类型和个数都在编译期点清,站里『C++ 特性参考卡』参考卷的《可变参数模板》一篇有讲。本周压轴的迷你 sprintf 会把这条线走到底:您从读参数的人,换到定规则的那一侧。

owollz4
