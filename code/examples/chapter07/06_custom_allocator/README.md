# Chapter 7.6 - 自定义分配器示例

本目录包含《自定义分配器（Allocator）》章节的所有代码示例。

## 说明

在嵌入式世界里，内存不是"无限"的抽屉。默认的 `new` / `malloc` 往往是潜在的性能炸弹、不可预测的延迟来源、以及碎片化萌生地。写一个"自定义分配器"是工程师的基本修行。

## 常见分配器类型

1. **Bump（线性）分配器**：分配 O(1)，不支持释放单个对象，适合启动期分配
2. **固定大小内存池（Free-list）**：适合大量相同大小的小对象，分配/释放都 O(1)
3. **Stack（栈）分配器**：LIFO 场景的神器，支持标记回滚

## 示例文件

| 文件 | 说明 | 关键点 |
|------|------|--------|
| `bump_allocator_demo.cpp` | 线性分配器 | 启动时分配，一次性重置 |
| `fixed_pool_demo.cpp` | 固定大小内存池 | 空闲链表，零碎片 |
| `stack_allocator_demo.cpp` | 栈分配器 | 标记/回滚机制 |
| `placement_new_demo.cpp` | Placement new | 对象构造与析构 |

## 编译方法

```bash
mkdir build && cd build
cmake ..
make
```

## 运行示例

```bash
./bump_allocator_demo
./fixed_pool_demo
./stack_allocator_demo
./placement_new_demo
```

## 环境要求

- C++17 或更高
- GCC 7+ 或 Clang 5+

## 嵌入式注意事项

1. 在嵌入式中，禁用异常或在异常敏感代码中使用 `noexcept` 的 allocate
2. 好多实现返回 `nullptr` 而不是抛异常
3. 分配器只提供原始内存；对象的构造/析构工作还需要 placement new
4. 线程安全时需要加锁或使用 lock-free 结构
5. 如果构建环境允许，优先考虑 `std::pmr::memory_resource`
