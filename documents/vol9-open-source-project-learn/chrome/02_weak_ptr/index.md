# WeakPtr：从 Chromium 学到的弱指针设计

本目录实现 Chromium 风格的 `WeakPtr` 弱指针组件,讲透"不介入所有权、却能安全观察对象存活状态"这套现代 C++ 设计。它是 [OnceCallback 系列](../01_once_callback/) 的姊妹篇——01-4 手搓的取消令牌,工业级正解就是 `WeakPtr`。

## 完整教程（full/）

面向零基础读者,从弱引用概念与前置知识开始,逐步引导到完整组件实现。

前置知识(7 篇):

- [弱引用与生命周期难题](./full/pre-00-weak-ptr-weak-reference-and-lifetime.md)
- [侵入式引用计数与 scoped_refptr](./full/pre-01-weak-ptr-intrusive-refcount-and-scoped-refptr.md)
- [std::atomic 与 memory_order](./full/pre-02-weak-ptr-atomic-and-memory-order.md)
- [序列、SEQUENCE_CHECKER 与 DCHECK/CHECK](./full/pre-03-weak-ptr-sequence-checker-dcheck-check.md)
- [concepts 与 requires 进阶](./full/pre-04-weak-ptr-concepts-and-requires.md)
- [模板友元与 uintptr_t 类型擦除](./full/pre-05-weak-ptr-template-friend-and-uintptr-t.md)
- [TRIVIAL_ABI 与平凡可重locate](./full/pre-06-weak-ptr-trivial-abi.md)

动手实践(6 篇):

- [动机与接口设计](./full/02-1-weak-ptr-motivation-and-api-design.md)
- [核心骨架与控制块](./full/02-2-weak-ptr-core-skeleton-and-control-block.md)
- [WeakPtrFactory 与"最后成员"惯用法](./full/02-3-weak-ptr-factory-and-last-member.md)
- [序列亲和性与 lazy 绑定](./full/02-4-weak-ptr-sequence-affinity-and-lazy-binding.md)
- [与回调集成——关闭 OnceCallback 的环](./full/02-5-weak-ptr-bind-integration.md)
- [测试与性能对比](./full/02-6-weak-ptr-testing-and-perf.md)

## 进阶设计指南（hands_on/）

面向有 C++ 模板与并发经验的读者,快速走读设计动机、实现策略与测试验证:

- [动机、接口与控制块设计](./hands_on/01-weak-ptr-design.md)
- [逐步实现](./hands_on/02-weak-ptr-implementation.md)
- [测试策略与性能对比](./hands_on/03-weak-ptr-testing.md)
