把 v 里所有等于 dead 的元素真正删掉,size 要跟着变小。std::remove 自己办不到:它只把留下的元素往前挪,返回新的「逻辑末尾」,从那里到 v.end() 是要扔的尾巴——裁尾巴得您亲自动手。

```cpp
std::vector<int> remove_all(std::vector<int> v, int dead) {
    // 在这里写:remove 挪完元素,用 erase 裁掉尾巴
    return v;
}
```

判题程序断言四组:remove_all({1, 2, 3, 2, 4, 2}, 2) 得 {1, 3, 4};remove_all({7, 7, 7}, 7) 得空向量;remove_all({1, 3, 5}, 2) 一个不删,原样返回;remove_all({2, 9, 2, 9}, 2) 得 {9, 9}。
