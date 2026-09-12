**思路**:

嘿嘿，咱们还是别一上来就陷入「枚举每种硬币用几枚」的泥潭，先问问自己：嘿！咋把大问题化简成小问题呢？答案就是拿当前可用的最小面值 `c` 来分类。做一个决策，无非就是「用这枚 `c`」和「不用这枚 `c`」，对吧。用掉一枚 `c` 之后，剩下还没统计出来的方案数就是递归 `helper(total - c, coin)`，面值不变；完全不用 `c` 的话，就换下一个面值 `next`，递归 `helper(total, next)`。两者之和，就是当前状态的全部方案。理解了嘛？

下一步呢？就是思考一下，既然你说递归，分拆逻辑上想好了，那么的话边界如何？答案是：

当`total == 0` 恰好凑出，算 1 种；当`total < 0` 或者没有更大面值（`coin == 0`）就算 0 种。

注意！ `total == 0` 得放最前面，不然会被后面误杀。对着 `solution.cpp` 看：

```cpp
int helper(int total, int coin) {
    if (total == 0) {
        return 1; // 好了真凑到了，算一种！
    }
    if (total < 0 || coin == 0) {
        return 0; // 嗯。。。钱币的概念非法，而且，用这个一下子干过了头，不匹配，不要了~
    }
    ...
}
```

接着 `switch` 干的就是「查下一个面值」这件事：

```cpp
    int next;
    switch (coin) {
        case 1:  next = 5;  break; // 下一次，我们试试看5元的！
        case 5:  next = 10; break;
        case 10: next = 25; break;
        default: next = 0;  break;  // 25 已是最大面值，没有“下一个”咯~
    }
```

`1 → 5 → 10 → 25`，走到 `default` 说明 25 已经是最大面值，于是 `next = 0`，下一轮递归撞上 `coin == 0` 的边界，干净返回 0。（提示，使用`std::optional`语义更加干净！这一点看看是否朋友愿意补充一下题解~）

```cpp
    return helper(total - coin, coin)   // 用这枚
         + helper(total, next);         // 不用这枚，换面值
}
```

整个函数没有一处循环，每个方案在递归树里恰好走一条唯一路径，不重不漏。入口就是：

```cpp
int count_coins(int total) {
    return helper(total, 1);
}
```

从最小面值 1 开始。

还是不通过？是不是把 `coin == 25` 写进 `default: return 0` 了？那样递归里**永远用不上 25 美分**，`count_coins(100)` 会得到 121（等于只用 `{1,5,10}` 的方案数）而不是 242；而 `total < 25` 的测试全都照常通过，bug 藏得很深。记住 25 的 `next` 是 `0`，不是 `return 0`。