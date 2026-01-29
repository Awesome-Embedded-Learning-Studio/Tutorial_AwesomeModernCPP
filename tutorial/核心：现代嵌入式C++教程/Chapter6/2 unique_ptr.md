# 现代嵌入式C++教程——std::unique_ptr：零开销的独占所有权

> 笔者突然想起来，好像我都没给这个系列写`std::unique_ptr`的意思。

想象一下：有一个对象你只想给它一个主人——没有共享、没有争抢、没有复杂的引用计数。你要的是简洁、确定、尽可能“无成本”的管理方式。欢迎进入 `std::unique_ptr` 的世界：C++ 标准库给嵌入式开发者准备的一把轻量级、明确且高效的所有权钥匙。

------

## 为什么在嵌入式也要爱 `unique_ptr`

嵌入式常常流行“手工 new/free”，或者根本不鼓励堆分配。事实是，合理使用堆（或定制分配策略）能让代码更清晰、模块更松耦合。相比裸指针，`unique_ptr` 的好处一言以蔽之：

- 明确的所有权语义：谁持有，谁负责销毁。
- 零或极低的运行时开销：在典型实现下，`sizeof(unique_ptr<T>) == sizeof(T*)`。
- 无拷贝、可移动：防止误拷贝引发双重释放（double free）。
- 与 RAII 完美契合：资源在析构里自动释放，异常安全（在允许异常的系统里尤其好）。

好了，废话不多说，看点代码。

------

## 最基本的用法（又简单又安全）

```cpp
#include <memory>

struct Sensor { void shutdown(); ~Sensor() { shutdown(); } };

void f() {
    auto p = std::make_unique<Sensor>(); // 推荐：安全、异常友好
    // 使用 p->...
} // 离开作用域时自动 delete
```

`std::make_unique` 是首选：一行代码既申请内存又构造对象，避免了 `new` 与构造之间的临界窗口（异常安全）。对于嵌入式项目，把 `make_unique` 和自定义分配器结合使用可以做到既安全又可控（后面示范）。

------

## 真正的“零开销”是什么意思？

`unique_ptr` 的“零开销”不是玄学，而是几条可检验的事实：

- 标准 `unique_ptr<T, std::default_delete<T>>` 在多数实现中只包含一个指针字段，因此大小等于裸指针。可以用下面的静态断言验证（在可编译的环境里）：

```cpp
static_assert(sizeof(std::unique_ptr<int>) == sizeof(int*), "通常应相等");
```

- 为什么能这样？因为默认删除器 `std::default_delete<T>` 是空类型，且编译器会利用空基类优化（EBO）把它“挤掉”。也就是说，`unique_ptr` 实际上只需要存储那根指针。

但注意：当你使用**有状态**的删除器（例如捕获了闭包的 lambda）时，删除器本身包含状态，`unique_ptr` 的大小可能会增加——这就是“零开销”条件：**无状态删除器**。

------

## 自定义删除器：强大但要小心

嵌入式里管理的资源不只有 `new`/`delete`，还可能是 `malloc`、文件描述符、裸 C 接口返回的句柄，或是自定义分配器分配的内存。`unique_ptr` 支持自定义删除器：

```cpp
// 使用 malloc / free
std::unique_ptr<char, void(*)(void*)> buf(
    static_cast<char*>(std::malloc(128)),
    [](void* p){ std::free(p); }
);

// 或者：用函数指针（注意：函数指针会占空间）
void free_fn(void* p) { std::free(p); }
std::unique_ptr<char, void(*)(void*)> buf2(
    static_cast<char*>(std::malloc(128)),
    free_fn
);
```

关键提醒：

- 捕获外部变量的 lambda 会变成有状态的删除器，从而可能 **增加 `unique_ptr` 的大小**。如果尺寸敏感（比如放入许多小对象的数组或表中），请避免捕获，改用函数指针或无状态函数对象。
- 如果 deleter 是函数指针，`unique_ptr` 内部需要存储那指针（所以比裸指针多一倍空间），但函数指针适合共享同一删除逻辑的场景。

------

## 管理数组、C 接口与自定义分配器

使用 `unique_ptr<T[]>` 来管理数组：它会在析构时调用 `delete[]` 而不是 `delete`。

```cpp
auto arr = std::make_unique<int[]>(64); // 分配 64 个 int，析构时调用 delete[]
arr[0] = 42;
```

嵌入式常见场景：使用专用堆或分配器（例如来自 RTOS 或定制内存池）。`unique_ptr` 无法直接接受分配器对象，但你可以把删除器写成调用分配器释放的函数或函数对象：

```cpp
struct Pool { void* alloc(size_t); void free(void*); };
extern Pool g_pool;

auto p = std::unique_ptr<MyType, void(*)(MyType*)>(
    static_cast<MyType*>(g_pool.alloc(sizeof(MyType))),
    [](MyType* t){ t->~MyType(); g_pool.free(t); }
);
```

如果池的释放函数不需要对象完整类型（例如仅内存回收），你可以将析构和回收分开，注意析构调用时类型需完整。

------

## 与多态一起用：必须注意析构器

如果你用 `unique_ptr<Base>` 指向 `Derived`，确保 `Base` 有虚析构函数，否则 `delete` 会是未定义行为：

```cpp
struct Base { virtual ~Base() = default; };
struct Derived : Base { /* ... */ };

std::unique_ptr<Base> p = std::make_unique<Derived>();
```

这是面向对象设计的基本规则，不是 `unique_ptr` 的特例。

------

## 转移所有权、释放与重置

`unique_ptr` 不可拷贝，但可以移动，这是它防止双重释放的核心：

```cpp
auto p1 = std::make_unique<int>(7);
auto p2 = std::move(p1); // p1 变成空，p2 拥有对象
```

有几个实用小函数：

- `p.release()`：返回原始指针并将 `unique_ptr` 置空（不会调用删除器）。谨慎使用：你拿回裸指针就要自己负责释放。
- `p.reset(new T(...))`：销毁旧资源并接管新资源。
- `p.get()`：返回内部裸指针（不转移所有权）。

在嵌入式里，如果你必须与 C API 交互，`release()` 很常见，但记得把释放责任写清楚，避免内存泄漏。

------

## 不要在中断/ISR里做堆操作

这是工程常识：不要在 ISR 中进行 `new`/`delete` 或会阻塞的操作。即便 `unique_ptr` 很轻量，但如果它持有堆分配的对象，分配/释放仍是堆操作。所以在 ISR 场景下，建议：

- 使用预分配对象池 + `unique_ptr` 的自定义删除器返回到池；
- 或者仅在任务/线程上下文使用 `unique_ptr`，ISR 只使用指针或信号量。

------

## 与标准容器配合（移动异常安全）

`unique_ptr` 的移动构造/赋值通常标记为 `noexcept`，这对容器（如 `std::vector`）很重要：在扩容时，容器更倾向于移动元素而非拷贝（拷贝不可行），且保证异常安全行为。换句话说，`unique_ptr` 与容器搭配，既安全又高效。

------

## 前向声明与 PIMPL（妙用）

`unique_ptr` 支持不完整类型的持有者，这非常适合 PIMPL（编译单元隐藏实现）：

头文件 `foo.h`：

```cpp
struct Impl;
class Foo {
    std::unique_ptr<Impl> pImpl;
public:
    Foo();
    ~Foo(); // 在实现文件中定义，Impl 完整
};
```

源文件 `foo.cpp` 中 `~Foo()` 可以看到 `Impl` 的完整定义并正确 delete。这个技巧能大幅减少编译依赖，是嵌入式大工程里常用的手段。

------

## 小结

`std::unique_ptr` 是 C++ 给我们的一位朴素而可靠的朋友：它把“谁负责释放”这件事写清楚了，并在绝大多数情况下做到**零或极低的额外开销**。对嵌入式开发者来说，`unique_ptr` 能把杂乱的资源释放逻辑封装得干净、可维护，同时保持性能。如果你还在用裸 `new`/`delete`，不妨试着用 `unique_ptr` 把那些责任交给 RAII——你会发现代码更稳、更容易审计，也更像成年人的工程。