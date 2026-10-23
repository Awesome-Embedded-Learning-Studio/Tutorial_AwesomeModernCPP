std::find 找到目标,返回指向它的迭代器;没找到,它返回的是一个特殊位置。空里填的就是那个位置,这也是判「真没找到」的标准写法:

```cpp
#include <vector>
#include <algorithm>

int main() {
    std::vector<int> v{3, 7, 5};
    auto it = std::find(v.begin(), v.end(), 99);
    if (it == v.____) {
        return 1;   // 真的没找到
    }
    return 0;
}
```

v 里没有 99,程序该走 return 1 那条路。空里填一个成员调用,连括号一起。
