# 嵌入式现代C++教程——std::shared_ptr如何呢

unique_ptr在我们上一篇博客的时候，已经讲过了它可以表达资源独占的含义。那么，智能指针还有一个朋友，就是std::shared_ptr。理解std::shared_ptr，我们需要把 `std::shared_ptr` 想象成一个会记账的托管管家——谁拿着这把钥匙就多记一笔，钥匙都收回时，管家把东西收拾干净。听起来美好；在桌面/服务器上也常常很管用。但把它搬到内存受限、实时性敏感、没有操作系统的嵌入式世界，要先把账本翻一翻：这把“管家”到底要多大的办公桌、会不会老是在你耳边叨叨（原子操作）、会不会把内存分散得像散落的螺丝钉。

------

## 总分总的总

`shared_ptr` 很方便，但代价不小：额外内存、原子开销（多线程场景）、可能的堆分配与碎片化、以及循环引用导致的内存泄漏。在嵌入式优先考虑 `unique_ptr` 或更轻量的引用计数实现（intrusive/ref-counted pool）。只有当共享所有权确实能显著简化设计时，才用 `shared_ptr`。

------

## `shared_ptr` 真正会付出的代价

1. **控制块（control block）与额外内存**
   `shared_ptr` 不只是个指针：它还有一个控制块（reference counts、deleter、allocator info 等）。最常见的情况是 `make_shared` 把对象和控制块一次性分配（节省一次分配，节约内存并提升局部性）；而 `std::shared_ptr<T>(new T)` 则通常需要两次独立分配（对象 + 控制块），会增加碎片和开销。
2. **原子操作（atomic increment/decrement）**
   默认实现对引用计数使用原子操作以保证多线程安全。每次拷贝/析构 `shared_ptr` 都会产生一次原子加/减，这在高频路径或实时要求严格的场景里可能是致命的性能负担。
3. **堆分配（malloc/free）与碎片**
   控制块与对象的分配会触发堆操作；在长期运行的嵌入式系统中频繁的堆分配会导致内存碎片，最终可能出现分配失败。
4. **循环引用（memory leak）**
   两个对象互相持有 `shared_ptr` 会导致引用计数永远不为 0，资源无法释放。要用 `weak_ptr` 打破环路。
5. **不可在 ISR 中使用**
   因为会涉及堆和原子操作，**不要在中断服务例程（ISR）中使用 `shared_ptr` 或执行堆分配/释放**。

------

## 怎么用（以及如何用得更安全、更高效）

### 推荐：尽量用 `std::make_shared`

```cpp
auto p = std::make_shared<MyType>(args...);
```

`make_shared` 在多数实现里把控制块和对象放在一个连续内存块里，既减少一次分配，也提升缓存友好性。对于嵌入式，这点非常重要：少一次 malloc 就少一次碎片风险。

### 如果你在乎内存布局，考虑显式池 + 自定义删除器

如果你自己有内存池（你很可能有 —— 你之前的项目就实现过内存池），可以让 `shared_ptr` 在销毁时把内存还给池，而不是 `delete`：

```cpp
// 假设有全局池 g_pool：alloc/free
void pool_deleter(MyType* p) {
    p->~MyType();
    g_pool.free(p);
}

std::shared_ptr<MyType> make_from_pool() {
    void* mem = g_pool.alloc(sizeof(MyType));
    if(!mem) throw std::bad_alloc();
    MyType* obj = new(mem) MyType(...); // placement new
    return std::shared_ptr<MyType>(obj, pool_deleter);
}
```

注意：上面这种方式如果你用 `std::shared_ptr<T>(new T)` 的替代，要避免额外的控制块分配问题；最好把 `shared_ptr` 构造时直接传入自定义删除器（如例）。

### 破环：用 `weak_ptr` 避免循环引用

```cpp
struct A;
struct B {
    std::shared_ptr<A> a;
};
struct A {
    std::weak_ptr<B> b; // 如果用 shared_ptr，会循环引用
};
```

`weak_ptr` 不增加引用计数，仅查看对象是否仍存活。

### `enable_shared_from_this` 的正确使用

如果对象方法需要返回一个 `shared_ptr` 指向自身，请通过 `enable_shared_from_this` 实现，而别尝试用 `shared_ptr(this)`（那会导致双重控制块和极其糟糕的后果）：

```cpp
struct Foo : std::enable_shared_from_this<Foo> {
    std::shared_ptr<Foo> getptr() { return shared_from_this(); }
};
```

------

## 轻量侵入式引用计数示例（单线程场景友好）

当你确定运行在单线程或你能保证外层加锁时，非原子计数更快、更节省空间：

```cpp
struct RefCounted {
    int ref = 0; // 非原子——只在单线程或外层已加锁时使用
    void add_ref() { ++ref; }
    int release_ref() { return --ref; }
protected:
    virtual ~RefCounted() = default;
};

template<typename T>
class SimpleIntrusivePtr {
    T* p = nullptr;
public:
    SimpleIntrusivePtr(T* t = nullptr) : p(t) { if(p) p->add_ref(); }
    SimpleIntrusivePtr(const SimpleIntrusivePtr& o) : p(o.p) { if(p) p->add_ref(); }
    SimpleIntrusivePtr& operator=(const SimpleIntrusivePtr& o){
        if(p==o.p) return *this;
        if(p && p->release_ref()==0) delete p;
        p = o.p;
        if(p) p->add_ref();
        return *this;
    }
    ~SimpleIntrusivePtr(){ if(p && p->release_ref()==0) delete p; }
    T* get() const { return p; }
    T* operator->() const { return p; }
};
```

优点：没有额外控制块分配、计数非常局部（对象内部），缺点：对象必须继承该基类，侵入性强。

------

## 常见误区（要是面试官问就拿出来秀）

- “`shared_ptr` 就是个指针，没啥开销。” —— 错。它有控制块、可能的额外分配、以及原子操作代价。
- “只要不循环引用，用 `shared_ptr` 就安全。” —— 部分正确，但仍要考虑性能与内存碎片。
- “`make_shared` 比 `shared_ptr(new T)` 慢/一样” —— 通常 `make_shared` 更快且更节省内存（一次分配），更局部化缓存。

------

## 结论

- 把 `shared_ptr` 当作工具箱里“有时必需但要慎用”的工具：当多处代码真正需要共享所有权且能接受额外开销时使用它。
- 在嵌入式优先考虑更轻量的替代：`unique_ptr`、对象池 + 自定义删除器、或侵入式引用计数。
- 始终考虑分配次数、原子开销和 ISR 约束。把 `make_shared`、自定义删除器和 `weak_ptr` 作为你的防守手段——正确使用它们可以避免大多数坑。

