# Chapter 5.3 - 对象池（Object Pool）模式

本目录包含《对象池（Object Pool）模式》章节的代码示例。

## 说明

对象池是嵌入式系统中管理内存的实用模式：提前分配一组对象，运行时从池中借出、用完归还，实现确定性的内存使用与低延迟分配/回收。

## 示例文件

| 文件 | 说明 | 关键点 |
|------|------|--------|
| `object_pool.cpp` | 通用对象池实现 | 可配置同步策略、RAII包装器、Packet示例 |
| `specialized_pool.cpp` | 专用对象池 | UInt32池、位图池、类型化池 |

## 编译方法

```bash
mkdir build && cd build
cmake ..
make
```

## 运行示例

```bash
./object_pool
./specialized_pool
```

## 核心概念

### 何时使用对象池

- 对象尺寸和数量可预估
- 频繁分配/释放需要确定性延迟
- 系统不允许运行时内存碎片
- 长期运行的设备

### API设计

```cpp
// 非阻塞借出，耗尽返回 nullptr
T* try_acquire();

// 阻塞式借出（断言失败）
T* acquire();

// 归还对象
void release(T* obj);

// 状态查询
size_t free_count() const;
size_t used_count() const;
```

### 同步策略

- **NoLockPolicy**: 单线程环境
- **InterruptLockPolicy**: ISR上下文，关中断保护
- **MutexLockPolicy**: RTOS环境，使用互斥锁

### 使用建议

1. **ISR中分配**: 使用InterruptLockPolicy
2. **RTOS任务**: 使用MutexLockPolicy
3. **单线程**: 使用NoLockPolicy零开销
4. **RAII包装**: 使用PooledPtr自动释放
5. **池大小**: 根据最大并发需求确定

## 环境要求

- C++17 或更高
- GCC 7+ 或 Clang 5+
