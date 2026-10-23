先把这段代码在心里过一遍,不用敲:

```cpp
#include <numeric>
#include <vector>
#include <cstdio>

int main() {
    std::vector<double> v{1.5, 2.5};
    int a = std::accumulate(v.begin(), v.end(), 0);
    double b = std::accumulate(v.begin(), v.end(), 0.0);
    std::printf("%d %.1f\n", a, b);
}
```

同一个 vector,同一个 accumulate,只有初始值不同:一个写 0,一个写 0.0。口答程序输出什么,再展开参考答案,看门道在哪。
