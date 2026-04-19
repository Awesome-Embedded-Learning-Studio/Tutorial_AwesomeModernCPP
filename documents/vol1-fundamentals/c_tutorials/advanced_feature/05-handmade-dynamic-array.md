---
title: "手搓动态数组——从零实现容器"
description: "从零设计和实现一个类型安全的动态数组库，理解内存扩缩容策略、错误处理模式与 API 设计原则，打通通向 std::vector 的理解之路"
chapter: 1
order: 105
tags:
  - host
  - cpp-modern
  - intermediate
  - 进阶
  - 容器
  - 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 30
cpp_standard: [11, 14, 17]
prerequisites:
  - "指针进阶：多级指针、指针与 const"
  - "动态内存管理：malloc/free/realloc 的正确使用"
  - "结构体、联合体与内存对齐"
  - "C 语言陷阱与常见错误"
---

# 手搓动态数组——从零实现容器

我们在写 C 程序的时候，最痛苦的事情之一就是数组大小必须在编译期确定。你想存 10 个数据，声明 `int arr[10]`；后来需求变了要存 100 个，你得回去改代码重新编译。更要命的是，很多时候你压根就不知道运行时会有多少个数据入列——用户输入多少条记录、网络收到多少个包、传感器采集多少个样本，这些都是运行时才能确定的。

`malloc` 确实解决了大小不确定的问题，但它只管分配不管增长——塞满了想继续加，就得自己手动 `realloc`、自己管理容量、自己处理错误。散落在代码各处的 `malloc/realloc/free` 和 `size` 变量很快就会变成一场维护噩梦。在 Python 里你可以随手写 `list.append(x)`，在 C++ 里你有 `std::vector`——它们都能自动扩容。但 C 语言标准库里没有这样的东西，我们必须自己造。

今天我们就从零开始，手搓一个完整的动态数组库，在这个过程中搞清楚数据结构设计、内存扩缩容策略、错误处理模式，最后对照 C++ 的 `std::vector` 看看标准库是怎么做这些事的。

> **学习目标**
>
> - [ ] 理解动态数组 size/capacity/data 三字段设计的必要性
> - [ ] 掌握 2x 扩容策略及其摊还 O(1) 复杂度分析
> - [ ] 理解缩容时机选择，避免频繁 realloc
> - [ ] 掌握枚举返回码的错误处理模式
> - [ ] 能够独立设计完整的增删改查 API
> - [ ] 理解 `std::vector` 的内部机制与 C 手搓版本的对应关系

## 环境说明

本文所有代码示例均在标准 C 环境下编译运行。编译时建议始终带上 `-Wall -Wextra`——动态数组的实现涉及大量指针运算和 `memcpy/memmove` 调用，编译器警告能帮你捕捉不少潜在问题。

```text
平台：Linux / macOS / Windows (MSVC/MinGW)
编译器：GCC >= 9 或 Clang >= 12
标准：-std=c11（C 部分）/ -std=c++17（C++ 对比部分）
依赖：无
```

## 第一步——搞清楚动态数组到底是什么

从物理存储的角度看，动态数组本质上还是一块连续的内存，和普通数组没有区别。关键的不同在于，动态数组把"已用空间"和"预留空间"分开了，并且用指针来间接访问这块内存，这样就能在需要的时候换一块更大的。你可以把它想象成一个可以自动"搬到大房子"的仓库——货架空了就换一个货架更多的仓库，旧货物一起搬过去，对外界来说仓库地址变了但存取货物的接口没变。

我们先想一个最简单的雏形：

```c
typedef struct {
    void* data;          // 连续内存块
    size_t size;         // 当前有多少个元素
} DynamicArray;
```

`data` 指向堆上分配的连续内存，`size` 记录当前元素个数。但你会发现一个致命问题：我们用的是 `void*`，不知道每个元素有多大。对于 `int` 数组步长是 4 字节，`double` 是 8 字节，自定义结构体可能是几十字节。没有元素大小信息，我们根本无法定位第 N 个元素。

所以我们需要加入 `capacity` 和 `element_size`：

```c
typedef struct _DynamicArray_ {
    void* data;              // 连续内存块（存储实际数据）
    size_t size;             // 当前元素个数
    size_t capacity;         // 当前分配的总容量（元素个数计）
    size_t element_size;     // 单个元素的字节大小
} DynamicArray;
```

四个字段各司其职：`data` 管"存在哪里"，`size` 管"用了几个"，`capacity` 管"总共有几个坑位"，`element_size` 管"每个坑位多大"。有了 `element_size`，定位第 `i` 个元素的地址就是 `(char*)data + i * element_size`——必须先转成 `char*`，因为 `char` 恰好是 1 字节，这样指针运算才是精确的字节偏移。直接对 `void*` 做加减，编译器会报错（C 标准不允许，虽然 GCC 作为扩展允许，但不可移植）。

> ⚠️ **踩坑预警**
> `size` 是"实际有多少个有效元素"，`capacity` 是"这块内存最多能放多少个元素"，`size <= capacity`。如果你在遍历的时候用了 `capacity` 而不是 `size` 作上界，就会读到未初始化的垃圾数据。

`std::vector` 内部的数据布局和我们几乎一模一样，只不过模板参数 `T` 替代了 `void*` + `element_size` 的组合，类型安全在编译期就得到了保证。`sizeof(std::vector<int>)` 在大多数实现上是 24 字节——三个 8 字节字段（指针 + size + capacity），`element_size` 在模板实例化后不需要存储。

## 第二步——建立错误处理体系

在写功能函数之前，先解决一个工程问题：函数执行出错了怎么办？最偷懒的做法是遇到错误直接 `exit(-1)`——这在教学代码里很常见，但在实际工程中简直是灾难，你总不能因为一个 `push_back` 失败就把整个服务器进程杀掉吧？

我们用一个枚举来建立清晰的错误码体系：

```c
typedef enum _DynamicArrayStatus_ {
    kSuccess            = 0,    // 正常执行
    kNullPointer        = -1,   // 传入了 NULL 指针
    kOutOfMemory        = 1,    // 内存分配失败
    kIndexOutOfRange    = -2,   // 下标越界
    kInvalidOperation   = -3    // 非法操作（如对空数组 pop）
} DynamicArrayStatus;
```

每个函数都返回 `DynamicArrayStatus`，调用者可以判断操作是否成功以及失败原因。配合辅助宏可以输出友好的错误信息：

```c
#define SHOW_ERROR(err)                                                      \
    do {                                                                     \
        const char* msg = "";                                                \
        switch (err) {                                                       \
            case kNullPointer:      msg = "NULL pointer passed";      break; \
            case kOutOfMemory:      msg = "Memory allocation failed";  break; \
            case kIndexOutOfRange:  msg = "Index out of range";       break; \
            case kInvalidOperation: msg = "Invalid operation";        break; \
            default: break;                                                  \
        }                                                                    \
        fprintf(stderr, "[DynamicArray Error] %s\n", msg);                   \
    } while (0)
```

将错误信息的展示和错误码的生成分离是更好的做法——调用者可能想把错误写入日志文件而不是打印到终端，可能想在错误后做资源清理。枚举返回码给了调用者完全的控制权。

## 第三步——实现创建与销毁

### 创建——工厂函数

在面向对象的语言里这叫构造函数，在 C 里我们叫它工厂函数——"生产"一个初始化好的对象并返回给调用者。

```c
/// @brief 创建一个动态数组
/// @param initial_capacity 初始容量
/// @param element_size 单个元素的字节大小
/// @return 指向新创建的动态数组的指针，失败返回 NULL
DynamicArray* dynamic_array_create(size_t initial_capacity, size_t element_size)
{
    DynamicArray* arr = (DynamicArray*)malloc(sizeof(DynamicArray));
    if (arr == NULL) {
        return NULL;
    }

    size_t actual_capacity = (initial_capacity < 8) ? 8 : initial_capacity;
    arr->data = malloc(actual_capacity * element_size);
    if (arr->data == NULL) {
        free(arr);  // 数据区失败，但结构体已分配，记得释放！
        return NULL;
    }

    arr->size = 0;
    arr->capacity = actual_capacity;
    arr->element_size = element_size;
    return arr;
}
```

分配结构体内存后必须立刻检查 `malloc` 返回值——不检查就访问 `arr->data`，程序直接段错误。我们设定了最小容量 8 作为经验值，太小导致频繁扩容，太大浪费内存。

> ⚠️ **踩坑预警**
> 注意 `free(arr)` 的存在。这是一个非常经典的资源泄露场景：结构体分配成功了，但数据区分配失败了。如果你直接 `return NULL` 而不 `free(arr)`，那块结构体内存就永远泄露了。这种"分配了一部分资源但后续步骤失败"的情况是 C 内存管理中最容易出错的地方。

使用方式：

```c
DynamicArray* nums = dynamic_array_create(16, sizeof(int));
if (nums == NULL) {
    fprintf(stderr, "Failed to create dynamic array\n");
    return -1;
}
```

用 `sizeof(int)` 而不是硬编码 `4`——`int` 的大小在不同平台可能不同，`sizeof` 在编译期计算，没有运行时开销。

### 销毁——释放顺序不能反

```c
/// @brief 销毁动态数组，释放所有内存
DynamicArrayStatus dynamic_array_destroy(DynamicArray* arr)
{
    if (arr == NULL) {
        return kNullPointer;
    }
    free(arr->data);   // 先释放数据区
    free(arr);          // 再释放结构体
    return kSuccess;
}
```

释放顺序不能反——先 `free(arr)` 的话，`arr->data` 就是对已释放内存的访问（Use After Free）。另一个问题是 `destroy` 之后 `arr` 指针本身并没有变成 `NULL`，它还指向那块已释放的内存。C 函数参数是值传递，只能靠调用者自觉手动置 NULL：

```c
dynamic_array_destroy(nums);
nums = NULL;  // 手动置 NULL，防止后续误用
```

`std::vector` 的 RAII 机制把这个创建/销毁的配对关系固化在了语言层面——析构函数在对象离开作用域时自动调用，内存绝对不会泄露。而我们的 C 版本每一步资源管理都得靠人工纪律。

## 第四步——搞定容量管理

### 扩容——2x 增长策略

当 `size == capacity` 时数组满了，再插入就需要扩容。问题是扩多大？如果每次加 1，连续插入 N 个元素需要 N 次 `realloc`，总拷贝量是 1 + 2 + ... + N = O(N^2)，完全不可接受。倍增扩容——每次满了把容量翻倍——则只需要约 log₂(N) 次扩容，总拷贝量 ≈ 2N = O(N)，摊还到每次插入上是 O(1)。就像搬家时不是每次多买一个箱子，而是每次把房子面积翻倍——搬的那一次很累，但平均到每一天就没什么感觉了。

```c
/// @brief 将容量扩展到至少 min_capacity
DynamicArrayStatus dynamic_array_reserve(DynamicArray* arr, size_t min_capacity)
{
    if (arr == NULL) return kNullPointer;
    if (min_capacity <= arr->capacity) return kSuccess;

    size_t new_capacity = arr->capacity * 2;
    if (new_capacity < min_capacity) new_capacity = min_capacity;

    void* new_data = realloc(arr->data, new_capacity * arr->element_size);
    if (new_data == NULL) return kOutOfMemory;

    arr->data = new_data;
    arr->capacity = new_capacity;
    return kSuccess;
}
```

`realloc` 会尝试在原位置就地扩展，不行就在堆上找一块更大的空间并把旧数据复制过去。无论哪种情况返回的指针都指向有效内存，旧数据完好无损。

> ⚠️ **踩坑预警**
> `realloc` 可能返回不同的地址！你必须用 `arr->data = new_data` 更新指针。如果你写成 `realloc(arr->data, ...)` 而不接收返回值，搬家后就丢失了新地址，旧地址指向的内存也已经被释放了——双重灾难。

### 缩容——避免抖动

如果一个数组曾增长到 10000 个元素后来删到只剩 10 个，9990 个元素的内存就白白浪费了。但缩容时机比扩容讲究得多——考虑数组在 100 和 50 之间来回震荡：降到 50 就缩容，紧接着又要插入，又扩容到 100——来回折腾就是经典的"抖动"问题。我们的策略是缩到 `size` 但保留最小容量 8，由调用者显式调用：

```c
/// @brief 将容量缩减到接近实际大小
DynamicArrayStatus dynamic_array_shrink_to_fit(DynamicArray* arr)
{
    if (arr == NULL) return kNullPointer;

    size_t new_capacity = (arr->size < 8) ? 8 : arr->size;
    if (new_capacity >= arr->capacity) return kSuccess;

    void* new_data = realloc(arr->data, new_capacity * arr->element_size);
    if (new_data == NULL) return kOutOfMemory;  // 缩容失败不影响现有数据

    arr->data = new_data;
    arr->capacity = new_capacity;
    return kSuccess;
}
```

`shrink_to_fit` 通常只在"确定不会再大幅增长"时调用，比如数据加载完毕后。C++ 标准没有规定 `std::vector` 扩容因子必须是 2x——MSVC 使用 1.5x，libstdc++ 和 libc++ 使用 2x。1.5x 内存利用率更高，但扩容次数略多。

## 第五步——实现元素访问

我们提供两种访问方式：不检查边界的快速版本（类似 `std::vector::operator[]`）和检查边界的安全版本（类似 `std::vector::at()`）。

```c
/// @brief 不检查边界的快速访问
void* dynamic_array_at_unchecked(const DynamicArray* arr, size_t index)
{
    return (char*)arr->data + index * arr->element_size;
}

/// @brief 带边界检查的安全访问
DynamicArrayStatus dynamic_array_at(
    const DynamicArray* arr, size_t index, void* out
)
{
    if (arr == NULL || out == NULL) return kNullPointer;
    if (index >= arr->size) return kIndexOutOfRange;
    memcpy(out, (char*)arr->data + index * arr->element_size, arr->element_size);
    return kSuccess;
}
```

安全版本返回拷贝到调用者缓冲区的方式，因为 C 没有引用概念且数据区是 `void*`，函数没法直接返回正确类型的值。这确实比 C++ 的 `vec.at(i)` 麻烦不少，但这就是 C 泛型编程的代价。

```c
// 使用示例
DynamicArray* nums = dynamic_array_create(8, sizeof(int));
int val = 42;
dynamic_array_push_back(nums, &val);

int* p = (int*)dynamic_array_at_unchecked(nums, 0);
printf("%d\n", *p);  // 42

int out;
dynamic_array_at(nums, 0, &out);
printf("%d\n", out);  // 42
```

## 第六步——实现增删操作

### push_back——尾部追加

```c
/// @brief 在数组尾部追加一个元素
DynamicArrayStatus dynamic_array_push_back(DynamicArray* arr, const void* element)
{
    if (arr == NULL || element == NULL) return kNullPointer;

    if (arr->size >= arr->capacity) {
        DynamicArrayStatus s = dynamic_array_reserve(arr, arr->capacity * 2);
        if (s != kSuccess) return s;
    }

    memcpy(
        (char*)arr->data + arr->size * arr->element_size,
        element,
        arr->element_size
    );
    arr->size++;
    return kSuccess;
}
```

`memcpy` 的目标地址是 `(char*)arr->data + arr->size * arr->element_size`——跳过所有已有元素来到第一个空坑位。由于 2x 增长策略，连续 N 次 `push_back` 的总时间 O(N)，摊还 O(1)。

来验证扩容效果：

```c
DynamicArray* nums = dynamic_array_create(4, sizeof(int));
printf("Initial: size=%zu, capacity=%zu\n", nums->size, nums->capacity);

for (int i = 0; i < 20; i++) {
    dynamic_array_push_back(nums, &i);
}
printf("After 20 pushes: size=%zu, capacity=%zu\n", nums->size, nums->capacity);
dynamic_array_destroy(nums);
nums = NULL;
```

```text
Initial: size=0, capacity=8
After 20 pushes: size=20, capacity=32
```

初始容量 4 被保底到 8，插入 20 个元素后经历了 8 -> 16 -> 32 两次扩容。

### pop_back——尾部删除

```c
/// @brief 删除数组尾部的元素
DynamicArrayStatus dynamic_array_pop_back(DynamicArray* arr)
{
    if (arr == NULL) return kNullPointer;
    if (arr->size == 0) return kInvalidOperation;
    arr->size--;
    return kSuccess;
}
```

被"删掉"的元素还躺在内存里，下次 `push_back` 会被覆盖。

> ⚠️ **踩坑预警**
> 我们没有在 `pop_back` 后触发缩容——刚 `pop` 完马上又 `push` 的话缩容就白做了。缩容应该由调用者显式调用 `shrink_to_fit`。`std::vector::pop_back` 也是同样的设计。

### insert 与 erase——中间插入和删除

`insert` 需要把插入位置之后的元素后移一位，`erase` 则是前移一位覆盖被删除元素。两者都必须用 `memmove` 而非 `memcpy`——因为源和目标内存区域有重叠，`memcpy` 在重叠情况下的行为是未定义的。

```c
/// @brief 在指定位置插入一个元素
DynamicArrayStatus dynamic_array_insert(
    DynamicArray* arr, size_t index, const void* element
)
{
    if (arr == NULL || element == NULL) return kNullPointer;
    if (index > arr->size) return kIndexOutOfRange;

    if (arr->size >= arr->capacity) {
        DynamicArrayStatus s = dynamic_array_reserve(arr, arr->capacity * 2);
        if (s != kSuccess) return s;
    }

    memmove(
        (char*)arr->data + (index + 1) * arr->element_size,
        (char*)arr->data + index * arr->element_size,
        (arr->size - index) * arr->element_size
    );
    memcpy(
        (char*)arr->data + index * arr->element_size,
        element,
        arr->element_size
    );
    arr->size++;
    return kSuccess;
}

/// @brief 删除指定位置的元素
DynamicArrayStatus dynamic_array_erase(DynamicArray* arr, size_t index)
{
    if (arr == NULL) return kNullPointer;
    if (index >= arr->size) return kIndexOutOfRange;

    memmove(
        (char*)arr->data + index * arr->element_size,
        (char*)arr->data + (index + 1) * arr->element_size,
        (arr->size - index - 1) * arr->element_size
    );
    arr->size--;
    return kSuccess;
}
```

验证 insert 和 erase：

```c
DynamicArray* nums = dynamic_array_create(8, sizeof(int));
for (int i = 0; i < 5; i++) dynamic_array_push_back(nums, &i);  // [0,1,2,3,4]
int val = 99;
dynamic_array_insert(nums, 2, &val);    // [0,1,99,2,3,4]
dynamic_array_erase(nums, 0);           // [1,99,2,3,4]

for (size_t i = 0; i < nums->size; i++) {
    printf("%d ", *(int*)dynamic_array_at_unchecked(nums, i));
}
printf("\n");
dynamic_array_destroy(nums);
nums = NULL;
```

```text
1 99 2 3 4
```

`std::vector::push_back` 在 C++11 后有了右值引用重载版本，可以接受 move 语义避免深拷贝。而我们的 C 版本只能通过 `memcpy` 做浅拷贝——如果元素内含动态分配的内存（比如指向 `malloc` 分配的字符串），浅拷贝会导致 double free 崩溃。这是 C 泛型编程的根本性限制。

## 第七步——实现遍历与查找

### 遍历——回调函数模式

容器内部是 `void*`，不知道元素类型，所以"怎么处理每个元素"需要调用者通过回调函数告诉容器——一种"控制反转"：

```c
/// @brief 遍历动态数组，对每个元素调用回调函数
DynamicArrayStatus dynamic_array_foreach(
    const DynamicArray* arr,
    void (*callback)(void* element)
)
{
    if (arr == NULL || callback == NULL) return kNullPointer;
    for (size_t i = 0; i < arr->size; i++) {
        callback((char*)arr->data + i * arr->element_size);
    }
    return kSuccess;
}
```

```c
void print_int(void* element) {
    printf("%d ", *(int*)element);
}

DynamicArray* nums = dynamic_array_create(8, sizeof(int));
for (int i = 10; i <= 50; i += 10) dynamic_array_push_back(nums, &i);
dynamic_array_foreach(nums, print_int);
printf("\n");
```

```text
10 20 30 40 50
```

回调函数模式在 C 标准库里大量使用——`qsort` 的比较函数、`bsearch` 都是这个套路。

### 查找——线性搜索

"比较相等"也需要调用者提供：

```c
/// @brief 在动态数组中查找元素
/// @return 找到返回下标，否则返回 SIZE_MAX
size_t dynamic_array_find(
    const DynamicArray* arr,
    const void* target,
    int (*compare)(const void*, const void*)
)
{
    if (arr == NULL || target == NULL || compare == NULL) return SIZE_MAX;
    for (size_t i = 0; i < arr->size; i++) {
        void* current = (char*)arr->data + i * arr->element_size;
        if (compare(current, target) != 0) return i;
    }
    return SIZE_MAX;
}
```

时间复杂度 O(N)。需要更快的话可以先排序再用二分搜索。C++ 的 `std::find` 使用迭代器配合 lambda 表达式，写起来比回调函数优雅太多；C++20 的 Ranges 更是把遍历、过滤、变换变成了链式调用。

## C++ 对照：std::vector 的设计取舍

到这里我们已经手搓了一个完整的动态数组库。回过头来系统对照 `std::vector`，理解这些设计取舍比记住 API 重要得多。

我们用 `void*` 实现泛型带来了三个问题：没有类型检查、需要手动传 `element_size`、回调函数里要强制类型转换。`std::vector<T>` 用模板完美解决了这三个——编译器在实例化时就确定了类型 `T`，所有类型检查在编译期完成，`sizeof(T)` 也自动计算。`std::vector` 的析构函数会自动释放内部数组，无论函数正常 return 还是因为异常退出，这就是 RAII 的核心思想——资源生命周期和对象生命周期绑定。C++11 的 move 语义让 `vec2 = std::move(vec1)` 变成了 O(1) 的指针交换，而 C 里只能 `memcpy` 整块数据。

有两个容易混淆的函数：`reserve(n)` 只改变 `capacity` 不改变 `size`，预先分配内存但不创建新元素；`resize(n)` 会改变 `size`，多出来的位置被值初始化，多余的元素被析构。我们的 C 版本只实现了 `reserve`，`resize` 留作练习。另外 `std::vector<bool>` 做了位压缩优化（每个 `bool` 只占 1 bit），但代价是不能取单个元素的地址。C++17 的 `std::span<T>` 提供对连续内存的非拥有视图，是非常重要的组合工具。

## 练习

以下练习题只给出函数签名和需求描述，实现留白。

### 练习 1：实现 resize

`reserve` 只改变容量不改变 size，而 `resize` 需要改变 size。当新 size 大于旧 size 时，多出来的位置应该填充默认值。

```c
/// @brief 改变动态数组的元素个数
/// @param default_value 指向默认值的指针（用于填充新增位置），可以为 NULL（填零）
DynamicArrayStatus dynamic_array_resize(
    DynamicArray* arr,
    size_t new_size,
    const void* default_value
);
// TODO: 自行实现
```

### 练习 2：实现 filter

给定一个动态数组和一个过滤谓词，返回一个新创建的动态数组，只包含满足条件的元素。

```c
/// @brief 根据谓词过滤动态数组的元素
DynamicArray* dynamic_array_filter(
    const DynamicArray* arr,
    int (*pred)(const void* element)
);
// TODO: 自行实现
```

### 练习 3：实现 map 变换

给定一个动态数组和一个变换函数，对每个元素应用变换函数，将结果存入新数组返回。

```c
/// @brief 对动态数组的每个元素应用变换函数
/// @param out_element_size 输出数组的元素大小（可能与输入不同）
DynamicArray* dynamic_array_map(
    const DynamicArray* arr,
    void (*transform)(const void* in, void* out),
    size_t out_element_size
);
// TODO: 自行实现
```

### 练习 4：实现拼接

将两个同类型的动态数组拼接成一个新的动态数组。

```c
/// @brief 将两个动态数组拼接成一个新的动态数组
DynamicArray* dynamic_array_concat(
    const DynamicArray* arr1,
    const DynamicArray* arr2
);
// TODO: 自行实现
```

> **难度自评**：如果你在实现练习时感到困难，请回顾对应章节的设计思路。特别是 resize——它本质上是 reserve + memset/memcpy 的组合，想清楚哪些位置需要填充、填充什么值，代码自然就出来了。

## 参考资源

- [cppreference: std::vector](https://en.cppreference.com/w/cpp/container/vector)
- [cppreference: realloc](https://en.cppreference.com/w/c/memory/realloc)
- [cppreference: memmove](https://en.cppreference.com/w/c/string/byte/memmove)
