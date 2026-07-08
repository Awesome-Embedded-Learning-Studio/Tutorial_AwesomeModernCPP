# NoDestructor：从 Chromium 学静态生命周期管理

本目录拆 Chromium 的 `base::NoDestructor<T>`,讲透全局/静态对象的生命周期管理:为什么 Chromium 禁全局构造和析构、placement new 怎么手动管生命周期、magic statics 的线程安全、"故意泄漏"的权衡。它和 [OnceCallback](../01_once_callback/) / [WeakPtr](../02_weak_ptr/) / [flat_map](../03_flat_map/) 是姊妹篇,补 vol9/chrome 的静态生命周期这块。

NoDestructor 是个轻量组件(一个 header-only 的薄包装类),本系列比前三者精简。

## 完整教程（full/）

- 前置知识:[静态存储期、初始化与析构](./full/pre-00-static-storage-and-init.md)、[placement new 与对齐存储](./full/pre-01-placement-new-and-aligned-storage.md)
- 动手实践:[动机与 API](./full/04-1-no-destructor-motivation-and-api.md)、[核心实现](./full/04-2-no-destructor-core-impl.md)、[使用边界](./full/04-3-no-destructor-when-to-use.md)、[LSan 与泄漏](./full/04-4-no-destructor-lsan-and-leak.md)

## 进阶设计指南（hands_on/）

面向有模板与生命周期经验的读者:[动机、接口与实现](./hands_on/01-no-destructor-design-and-impl.md)、[使用边界与测试](./hands_on/02-no-destructor-usage-and-testing.md)。
