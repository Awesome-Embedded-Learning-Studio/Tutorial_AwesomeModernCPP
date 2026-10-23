**思路**:

对的是第 2 条:v[0] 的类型是一个代理对象,能当 bool 用,但拿它绑 bool& 编不过。咱们把「为什么」从头说。

vector<bool> 是标准库里明写的特例:它一个比特存一个元素,把内存压到八分之一。代价立刻就到:比特没有地址,而 bool& 要求指向一个真实存在的 bool 对象。给不出怎么办?它的 operator[] 交出来的是一个代理对象,类型叫 `std::vector<bool>::reference`:您给它赋值,它把值写回容器里对应的比特;您读它,它转成 bool 交出来。题面两行就是这么跑的:

```cpp
v[0] = true;  // 给代理赋值,写回第 0 个比特
v.flip();     // 全体按位取反:1 变 0、0 变 1
```

「绑 bool& 编不过」这半句,笔者把实测报错贴全:

```text
error: cannot bind non-const lvalue reference of type 'bool&' to an rvalue of type 'bool'
```

咱们看,v[0] 是个右值的代理,转换出来的也是右值 bool,非常量引用接不住,编译器当场回绝。

另外三条,咱们一条条过。第 1 条(是 bool&,还能取地址):两头都错,它不是 bool&,`&v[0]` 对右值取地址,同样编不过。第 3 条(C++20 起弃用、编译器会警告):没有的事。把 vector<bool> 弃用或者重做的提案,委员会收过好几份,一份都没通过,它至今还在标准里,主流编译器也不警告。第 4 条(flip 把顺序倒过来)最有迷惑性:拿题面这组 {true, false} 试,flip 完是 {false, true},倒序完也是 {false, true},结果恰好撞上;咱们换 {true, true, false} 再验——flip 按位取反,给 {false, false, true},倒序给 {false, true, true},当场分家。flip 干的是取反,不是倒序,这几组输出笔者都实测过。

最后咱们串一下周一那题:「auto& 才是元素本体」,这条规矩到 vector<bool> 上会失手。`for (auto& x : v)` 连编译都过不了,原因还是那个代理:*begin() 返回的是个右值,非常量引用接不住,实测报错:

```text
error: cannot bind non-const lvalue reference of type 'std::_Bit_reference&'
to an rvalue of type 'std::_Bit_iterator::reference'
```

真要一格一个真 bool,std::vector<char>、std::deque<bool> 的下标都给得出真引用;您要的就是比特压缩,那记住它的下标不是普通元素就行,auto 和引用都多想一秒。

**排错**:

这道题不判代码,排错排的是选项。您要是选了第 4 条,多半是被 {true, false} 这组数据带进去的:两种解释的结果撞在一起,分不清取反和倒序,上面 {true, true, false} 的对照就是给这个准备的。选第 1 条的,是把 vector<int> 的手感直接搬过来了;选第 3 条的,建议把「提案」和「定案」分开记:有人提议过,不等于标准采纳了。
