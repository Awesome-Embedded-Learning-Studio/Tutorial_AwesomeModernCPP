同一个文件里,一个老式的 `enum`,一个 `enum class`,两种写法挨着。下面四句赋值,有一句过不了编译,选出它(单选)。

```cpp
enum Color { RED, GREEN };
enum class Fruit { APPLE, PEAR };

int a = RED;                              // 第 1 句
int b = static_cast<int>(Fruit::APPLE);   // 第 2 句
int c = Fruit::PEAR;                      // 第 3 句
Color d = GREEN;                          // 第 4 句
```

选完之后,建议您真拿编译器把这几句跑一遍:过不了的那句,编译器把拒绝理由原样写在错误信息里了。
