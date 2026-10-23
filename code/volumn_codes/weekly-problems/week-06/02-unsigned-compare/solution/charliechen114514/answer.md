**思路**:

病根是一套叫「常规算术转换」的规则:int 和 unsigned 放在一起比大小,先统一类型,统一的方向是往 unsigned 靠,int 那边的值按无符号规则重新解释。`-1` 的位模式是全 1,转成 unsigned 成了 4294967295,拿它跟 1 比,当然「大」。笔者实测过一组:`-1 < u` 打印 0,`u > -1` 打印 0,只有两边都落在 int 里的 `-1 < (int)u` 打印 1——常识得两边同为有符号才作数。

修法顺着这条规则来,对着 solution.cpp 看:

```cpp
bool safe_less(int a, unsigned b) {
    if (a < 0) {
        return true;
    }
    return static_cast<unsigned>(a) < b;
}
```

a 小于 0 时,任何 unsigned 的数学值都不小于 0,大小关系已经定了,咱们直接返回 true,一步都不用比。a 不小于 0 时,它的值落在 unsigned 能精确表示的范围里,`static_cast<unsigned>(a)` 只是换个类型,值一个二进制位都不动;两边都是 unsigned,比较就诚实了。

顺带说一句,往 unsigned 靠这个方向是标准写死的,各家编译器一致;`-Wall` 下的 `-Wsign-compare` 警告一直在提醒这件事,您编译题面那两行时它就跳出来了。

**排错**:

还是不通过?先看挂的是哪几条。`safe_less(-1, 1u)` 和 `safe_less(-1, 0u)` 这两条,专逮直写 `return a < b;` 的版本:-1 被转成 4294967295,比谁都大,返回 false,断言要 true,一挂挂两条;其余四条倒都碰巧能过,您会觉得「差不多对了」,差的那两条恰恰是考点的全部。另一个方向,把 b 转过来写 `return a < (int)b;`,判题数据里 b 最大是 10,这么写能全过,但 b 真超过 INT_MAX 时它是错的。判题这回没拦您,这句实话笔者替判题交代在这儿。
