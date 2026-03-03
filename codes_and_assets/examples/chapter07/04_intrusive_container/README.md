# Chapter 7.4 - 侵入式容器设计示例

本目录包含《侵入式容器设计》章节的所有代码示例。

## 说明

侵入式（intrusive）容器的关键点：节点信息（next/prev/...）直接放在用户对象内部，而不是另外分配一个 node 包裹对象指针。

## 为什么使用侵入式容器

- **零额外分配** —— 不需要每次 push 都 malloc/new 一个 wrapper
- **更佳缓存局部性** —— 对象和元信息在一起，遍历更快
- **更小的内存占用与确定性** —— 对于内存受限或实时系统非常友好

## 实现策略

1. **基类 hook（inheritance）**：对象继承一个包含 next/prev 的 hook 基类
2. **成员 hook（member hook）**：对象包含一个 hook 成员，使用 `container_of` 技巧

## 示例文件

| 文件 | 说明 | 关键点 |
|------|------|--------|
| `inheritance_example.cpp` | 继承式侵入链表 | 基类 hook，类型安全 |
| `member_hook_example.cpp` | 成员 hook 方式 | container_of 宏，多链表支持 |
| `task_scheduler_example.cpp` | 任务调度器场景 | 实际嵌入式应用 |

## 编译方法

```bash
mkdir build && cd build
cmake ..
make
```

## 运行示例

```bash
./inheritance_example
./member_hook_example
./task_scheduler_example
```

## 环境要求

- C++17 或更高
- GCC 7+ 或 Clang 5+

## 嵌入式注意事项

1. 对象生命周期必须明确：链表中的节点在被销毁前必须先从所有链表中移除
2. 插入前检查状态：防止重复插入
3. 并发场景需要内存屏障/原子性保护
4. 提供 RAII wrapper 保证异常或早返回时对象能安全注销
5. 侵入式容器不适合普通应用层业务逻辑或第三方库对象
