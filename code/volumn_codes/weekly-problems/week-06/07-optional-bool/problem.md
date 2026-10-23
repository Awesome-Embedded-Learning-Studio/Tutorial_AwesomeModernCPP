```cpp
#include <cstdio>
#include <optional>

int main() {
    std::optional<bool> ok = false;
    if (ok) {
        std::puts("A");
    } else {
        std::puts("B");
    }
}
```

`ok` 里明明存着 false。您先别运行,口答三件事:程序打印的是 A 还是 B?为什么存着 false,判断还是这个走向?还要说清,要按「里面的值」来判,该怎么改。答完展开参考答案对照——您要是不服,拿编译器跑一遍对质。
