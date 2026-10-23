vector<int> 的 v[0] 是 int&,拿它绑引用、对它取地址都行。那 vector<bool> 的 v[0] 是什么?先看代码:

```cpp
#include <vector>

int main() {
    std::vector<bool> v{true, false};
    v[0] = true;
    v.flip();
    // 跑到这里,v 是 {false, true}
}
```

下面四条关于 vector<bool> 的说法,选出对的那条。(单选)
