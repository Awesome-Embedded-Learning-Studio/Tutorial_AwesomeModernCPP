下面这段程序跑完打印 known=1 size=3:

```cpp
#include <cstdio>
#include <map>
#include <string>

int main() {
    std::map<std::string, int> stock{{"apple", 3}, {"pear", 0}};
    int known = 0;
    for (const char* name : {"apple", "pear", "peach"}) {
        if (stock[name] > 0) {
            ++known;
        }
    }
    std::printf("known=%d size=%d\n", known, (int)stock.size());
}
```

表里明明只造了两条:apple 3 个、pear 0 个。peach 从头到尾没进过货,size 却是 3。有一行看起来只是「读」,实际把表改大了。点行号标出它,再展开答案对照:为什么、怎么改。
