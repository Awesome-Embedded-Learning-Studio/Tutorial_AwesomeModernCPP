**思路**:

这一题笔者摆得极短:一个循环,一句乘 2。错的人几乎都错在同一个字上——循环变量那个 `auto`。咱们把 range-for 摊开看,它大致是这么回事:

```cpp
for (auto it = v.begin(); it != v.end(); ++it) {
    auto x = *it;   // 每轮新声明一个 x,拷贝一份
    // 您写的循环体
}
```

`auto x = *it` 是拷贝:元素本体待在容器里,x 里躺的是一份复制品。复制品乘 2 乘 100,都是它自己的事,容器纹丝不动。想让循环体够得着本体,引用得您亲手写上,这就是 solution.cpp 的全部:

```cpp
void double_all(std::vector<int>& v) {
    for (auto& x : v) {
        x *= 2;
    }
}
```

`auto& x = *it` 绑的是元素本身,乘 2 直接落在容器里,咱们要的四条断言全绿。顺手再记一个搭配:只读遍历大对象时写 `const auto&`,省一次拷贝,还防您手滑改;这一题要的就是改,所以 `auto&`,不带 const。

**排错**:

还是不通过?九成是循环变量写了 `auto`。那个版本编得过,但 GCC 16 开着 -Wall 会给一句提醒,笔者实测原样贴给您:

```text
warning: variable 'x' set but not used [-Wunused-but-set-variable=]
```

症状您也认得出:四条断言里唯独空容器那条是绿的——没有元素可拷贝,`v.empty()` 照样成立;其余三条全红,{1, 2, 3} 过后还是 {1, 2, 3}。
