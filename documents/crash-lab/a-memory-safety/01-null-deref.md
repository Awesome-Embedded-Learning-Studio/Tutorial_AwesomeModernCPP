---
title: "空指针解引用:崩得最明白的一案"
description: "find_value 找不到返回 nullptr,直接 *result 解引用——Linux 下 exit 139(SIGSEGV),GDB 停在崩溃行、print 出 (int*)0x0,ASAN 报 SEGV on zero page。空指针是少数『必崩』的 UB:操作系统把零页设为不可访问,MMU 当场拦截;但 this==nullptr 调成员函数、无 MMU 的单片机上,它照样能装没事。修复从判空到 std::optional 的类型系统收编。"
chapter: 15
order: 1
difficulty: beginner
platform: host
reading_time_minutes: 10
tags:
  - host
  - cpp-modern
  - beginner
  - 内存管理
  - optional
  - 类型安全
prerequisites:
  - "卷一·ch04: 指针基础"
related:
  - "悬垂指针:释放之后,指针还活着"
cpp_standard: [11, 17]
---

# 任何事业都有 Bootstrap，好消息是我们这里也有 —— 空指针解引用

要说什么B崩溃是最好查的，最快速解决的。我说还是空指针解引用。基本上笔者所在的项目软件不少人用，天天能飞来不少的dump，我要是用windows dmp看到低地址解引用，直接会反向查pdb看看哪个函数搞抽象。基本上加个判空，搞定。。。了嘛？回来，当然这不见得，但是他的确可以让您的软件不至于立马崩溃。

> 关于是不是要崩溃，各位都有己见，一些人认为崩溃没毛病，因为起码不藏起来问题；另一些是别崩溃。起码好看。这里不参与争论，看您的场景。生产软件崩溃了，您恐怕有的忙了（是被老板臭骂两顿还是你的用户直接抛弃软件，都不赖）

扯远了，回来。但是空指针访问，**或者说低地址访问**，的确在悄悄暗示他背后真正的问题——那就是你访问到了你**刻意**没有初始化的对象。好，我们来。


## 先把它造出来

我们先不搞复杂的，毕竟复杂的，您恐怕就要想起来你查崩溃的噩梦例子了。我说您现在是一个A模块的开发同事，B模块的负责人找到你——嘿牢大，咱们这里有个业务哈，对一下接口用一下你的代码。你们欢欢乐乐的对好了接口，你说你要提供一个find_value：

```cpp
int* find_value(int* arr, int size, int target) {
    for (int i = 0; i < size; i++) {
        if (arr[i] == target)
            return &arr[i];
    }
    return nullptr;  // 没找到,返回空指针
}
```

可惜习惯不太好，唯独没说万一没找到咋办。你的对接同事估计也是加班忙晕了，直接就这样用了：

```cpp

int main() {
    int check_value = ... // 你的好同事读取用户的输入
    int data[] = {10, 20, 30, 40, 50}; // 你的后端好哥们告诉你返回这些
    int* result = find_value(data, 5, 999);   // shit，你的用户输入小巧思：999 ，不好意思不存在，喜提 nullptr
    printf("*result = %d\n", *result);        // ← Boom! 没检查就解引用
}
```

Reviewer可能也加班忙晕了（你猜我为什么说加班），就这样稀里糊涂过了。上线的时候开始好好的，突然线上告警——崩溃率爆增，恭喜，你要被喷了。

上面的这个代码比较刻意，但是多少自己写代码的时候真遇到过，反馈的还好不是用户，是测试。笔者这台 Linux(GCC 16.1.1)上,它死得很干脆:

```text
find_value returned: (nil)
exit code: 139
```

139 = 128 + 11,11 号信号就是 SIGSEGV(段错误)。同一份代码在 Windows / MSVC 上,结局相同、说法不同:`exit code: -1073741819(0xC0000005 = STATUS_ACCESS_VIOLATION)`。平台不同,信号名不同,但都是同一件事:**CPU 去访问地址 0,被拦下来了。**低地址显然不是任何意义上的合法对象，你的MMU：嘻嘻，老弟你在访问什么，滚回去！然后进程被干掉了。

## 为什么它崩得这么老实

`nullptr` 就是地址 0。C的朋友知道他跟NULL一样就行。

现代操作系统在安排进程地址空间时,故意把 0 附近的一整页(常叫"零页")标记为不可访问——就是防着这种手滑。所以解引用空指针的链条短得可怜:`*result` 一求值,CPU 就去访问地址 0;MMU 一查页表,这页没权限,硬件异常当场触发;操作系统把它转成信号发回来——Linux 是 SIGSEGV,Windows 是 0xC0000005——进程随即终止。

没有薛定谔,没有延迟引爆。错误在哪一行,崩在哪一行。

## 抓它:三样工具,同一句话

我们 GDB 就跑！起！来！直接停在案发地,还能顺手验一下"凶器"确实是指针为空:

```text
(gdb) run
Program received signal SIGSEGV, Segmentation fault.
0x0000555555555245 in main () at crash.cpp:27
27     printf("*result = %d  <-- null deref!\n", *result);
(gdb) print result
$1 = (int *) 0x0
(gdb) bt
#0  0x0000555555555245 in main () at crash.cpp:27
```

`print result` 给出 `(int *) 0x0`,铁证。开 ASAN 跑,报告会额外提示一句人话:

```text
==28078==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000000
==28078==The signal is caused by a READ memory access.
==28078==Hint: address points to the zero page.
```

`Hint: address points to the zero page`——ASAN 直接告诉您:这就是个空指针。实话说,空指针案不太需要这些重武器,崩得明白,看堆栈就行;但等咱们到了悬垂指针那案您就会明白,同样的 SIGSEGV,查起来的难度是天壤之别。

## 当然，我们这里也有嵌入式的内容是吧，是有一些特殊的

| 场景                                               | 行为          | 为什么                                                                             |
| -------------------------------------------------- | ------------- | ---------------------------------------------------------------------------------- |
| 直接解引用 `nullptr`                               | 基本必崩      | 零页不可访问                                                                       |
| `p->func()` 且 `p == nullptr`,成员函数不碰成员数据 | 很可能不崩    | 成员函数没解引用 this,只是个普通调用                                               |
| 无 MMU 的裸机(单片机)                              | 不崩,读到全零 | 地址 0 是真实存在的 Flash/ROM（对于STM32F103的内存分布，是中断向量，ResetHandler） |

第二行值得展开,笔者为此写了个两函数的最小验证(空指针调不碰成员的函数,再调碰成员的):

```text
safe_func called, this=(nil) (no member access)
survived safe_func
exit: 139
```

`p->member_func()` 在编译器眼里是 `member_func(p)`,`this=(nil)` 传进去,只要函数体里不访问成员,空指针就这么混过去了——直到下一行碰到成员访问,雷才炸。第三行和本站的嵌入式读者直接相关:在 Cortex-M 上,地址 0 是向量表,解引用"空指针"读到的是栈顶指针的值,程序不崩,只是静默地错——这也是嵌入式里"能跑但有诡异 bug"的经典来源。

## 治本:让"可能没有"写进类型里

最朴素的修复是判空,但判空靠自觉——这次记得,下次忘了。真正治本的路是把"可能有值,也可能没有"编码进类型系统,C++17 给的答案是 `std::optional`:

```cpp
// 返回指针:调用方可能忘检查,崩在运行期
int* find_value(int* arr, int size, int target);

// 返回 optional:类型本身就在提醒"这里可能没有"
std::optional<int> find_value(const int* arr, int size, int target);

auto result = find_value(data, 5, 999);
if (result.has_value()) {          // 检查是流程的一部分
    printf("%d\n", *result);
}
int val = result.value_or(-1);     // 或直接给默认值
```

看到了吧，把提示写类型系统里，一切都好了！下一案的凶手,可就没这么客气了:**释放之后,指针还活着**。咱们一不小心猜到了，就是大名鼎鼎的，让我头皮发麻无数次的——Use After Free了！

## 参考

- [cppreference: std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [C++ Core Guidelines: Don't pass nullptr](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Ri-null)
