下面这段程序输出什么?把输出原样写在答案框里。

```cpp
#include <iostream>

int main() {
    int i = 42;
    int& r = i;
    ++r;
    std::cout << i << ' ' << r << '\n';
}
```
