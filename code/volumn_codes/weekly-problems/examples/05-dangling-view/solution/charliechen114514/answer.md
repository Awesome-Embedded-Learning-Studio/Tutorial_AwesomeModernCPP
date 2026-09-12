**病根在第 10 行**:`make_greeting()` 按值返回,产生一个**临时 `std::string`**;`std::string_view view` 用它构造出一份非拥有的视图——这条语句结束时(`;` 处),临时 `std::string` 析构,`view` 从此悬垂。之后读 `view` 是未定义行为:实测在 Compiler Explorer(gcc 15, `-O2`)上真就打出了一串乱码——**「能跑对」不等于「没错」**。

修复见 `solution.cpp`:让字符串自己活过使用点。

经验法则:`std::string_view` 是「借」不是「拥有」——凡是**从函数返回值、临时对象**上借视图,都要多看一眼生命周期。这正是卷二 string_view 章节反复强调的坑。
