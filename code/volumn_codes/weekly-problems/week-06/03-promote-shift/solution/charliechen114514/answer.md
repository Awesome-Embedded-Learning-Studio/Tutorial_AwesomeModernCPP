**思路**:

两个函数体一字不差,都是 `return b << 1;`,差别全在返回类型上。咱们跟着一个 0xFF 走一遍。

咱们把 `b << 1` 这一步拆开看:不管外层函数返回什么,C++ 都先做同一件事,整型提升。比 int 窄的类型(unsigned char 算一个)参与运算前先升成 int,移位是在 int 里做的。所以 `b << 1` 实际是 int 里的 255 左移一位,得 510,这个数没超出 int 的范围,不回绕:

```cpp
int wide_shift(unsigned char b) {
    return b << 1;   // int 里的 510,原样交回
}

unsigned char narrow_shift(unsigned char b) {
    return b << 1;   // int 里的 510,存回 unsigned char 时折了一下
}
```

`wide_shift` 把 510 直接交回,完事。`narrow_shift` 的返回类型是 unsigned char,510 要存进去,您看它按 256 取模:510 = 2 × 256 - 2,折成 254。判题另外两条断言也是同一套规则推出来的:`wide_shift(0x7F)`,127 左移得 254,int 装得下,原样返回;`narrow_shift(0x80)`,128 左移得 256,取模归 0。

复合赋值那条路您顺手一起记:`b <<= 1` 同样先在 int 里算出 510,存回 b 时折成 254,和窄函数的路数一模一样。

**排错**:

还是不通过?最常见的写法,是在 `wide_shift` 里也顺手截一下,比如 `return (unsigned char)(b << 1);`,或者 `b <<= 1; return b;`——510 先折成 254 再交回 int,挂的是 `wide_shift(0xFF) == 510` 这条,报出来的值是 254。您要是发现挂的是宽的那半,八成就是这一步截早了。反过来,在窄函数里纠结「要不要自己取模」,不必:返回类型会替您收掉这一步。
