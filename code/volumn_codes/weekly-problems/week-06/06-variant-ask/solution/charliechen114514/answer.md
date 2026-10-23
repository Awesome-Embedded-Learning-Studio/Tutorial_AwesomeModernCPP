**思路**:

variant 的本事是:按需存 int 或 string 之一,并且自己记着现在存的是谁。这题咱们练的就是取值前先问清这一下。参考实现走的是指针问法:

```cpp
int fetch_int(const std::variant<int, std::string>& v) {
    if (const int* p = std::get_if<int>(&v)) {
        return *p;
    }
    return -1;
}
```

`std::get_if<int>(&v)` 在存的是 int 时给您一个指向它的指针,存的是别的类型时给 nullptr,判空和取值一口气做完。判题断言里 `fetch_int(42)` 传进去的 42 被 variant 选中 int 存下,指针非空,解引用返回 42;传 `"hi"` 时存的是 string,指针为空,走 -1。

另一条路是先问后取:`std::holds_alternative<int>(v)` 问一句「现在存的是 int 吗」,是就拿 `std::get<int>(v)` 取值。两条路都正当,参考实现挑了 get_if,您看它一趟把「问」和「取」都办完。

要是不问就取呢?裸写 `return std::get<int>(v);`,存的是 string 时抛 `std::bad_variant_access`。笔者用 GCC 16.2.1 实测,`what()` 给的原文是 `std::get: wrong index for variant`。这比老 union 讲道理:union 存了谁全靠人脑记,取错了不吭声地读出脏值;variant 取错至少把异常抛到您面前,程序不会揣着错值继续跑。

**排错**:

还是不通过?最常见的错法是不先问、上来就 `return std::get<int>(v);`:传字符串的两条断言(`"hi"` 和空串)当场抛 `std::bad_variant_access`,判题环境里异常就是不过,症状齐整——int 那三条全绿,string 那两条全红。还有一种是问了类型、两个分支都写 `return -1;`,`fetch_int(42) == 42` 这条会把您逮住,对照着改一处就好。
