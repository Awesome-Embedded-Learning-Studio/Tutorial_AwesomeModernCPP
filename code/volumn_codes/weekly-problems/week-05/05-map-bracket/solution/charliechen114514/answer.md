**思路**:

病根在第 9 行:`if (stock[name] > 0)` 里那声 `stock[name]`,咱们先看它坏在哪条规矩上。

map 的 operator[] 立着一条规矩:键不在,就地默认构造一个映射值塞进表里(int 默认构造得 0),再把这个值的引用交给您。它天生带写入,哪怕您只想看一眼。咱们跟着程序走一遍:apple 在表里,取 3,known 计到 1;pear 在表里,取 0,不大于 0,不计;peach 不在,operator[] 当场插入 {"peach", 0},表从 2 条变 3 条,取出来的 0 也不大于 0。于是打印 known=1 size=3,题面没有说谎。

它为什么非这样设计?咱们看签名:operator[] 要返回 `mapped_type&`,指向值对象的引用。引用必须指着真实存在的东西,peach 的条目压根不存在,拿什么给您引用?只能先造一个,再给引用。标准自己也认账:operator[] 没有 const 版本,您拿 const map 去调它,GCC 当场拒绝,实测报错:

```text
error: passing 'const std::map<std::__cxx11::basic_string<char>, int>' as 'this' argument discards qualifiers [-fpermissive]
```

只查不动表,您把第 9 行的条件换掉就行:`stock.count(name) > 0` 问条目在不在,或者 `stock.find(name) != stock.end()` 拿迭代器问个明白,跑完 size 就停在 2。顺带提防一句:`stock.at(name)` 确实只读,可键不在时它抛 out_of_range,这题里真换它,程序就带着异常退场了,救不了。

**排错**:

最常见的错标是第 13 行 printf:那里只是把 size 打出来,把表改大的另有其人。第 6 行也有人标:造表时表里确实只有 apple 和 pear 两条,平平无奇。判「哪一行在写」的法子就一条,您盯着这一行执行前后看:表变没变——全程序动过表的,只有第 9 行那对方括号。
