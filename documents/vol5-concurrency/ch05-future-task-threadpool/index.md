---
title: "future、任务与线程池"
description: "从 std::async 到 promise/packaged_task，构建灵活的异步任务通道与线程池基础设施"
---

# future、任务与线程池

前几章我们一直在跟底层原语打交道：thread、mutex、atomic、condition variable。这些东西给了我们精确的控制力，但也带来了大量的手动管理负担——你需要自己设计同步机制、自己传递结果、自己处理错误。C++ 标准库提供了一套更高层的异步抽象来简化这些工作：future 是结果容器，promise 是值的写端，packaged_task 是任务的封装器，async 是最便捷的启动方式。它们组合在一起，构成了线程池和任务队列的基础设施。

这一章我们从 `std::async` 和 `std::future` 开始，理解异步任务的启动策略和结果获取机制；然后深入 `std::promise` 和 `std::packaged_task`，学会手动控制值的设置和任务的执行；最后我们会讨论 `std::shared_future` 和线程池的设计模式，把前面学到的所有组件串联起来。

## 本章内容

<ChapterNav variant="sub">
  <ChapterLink href="01-std-async-and-future">std::async 与 future</ChapterLink>
  <ChapterLink href="02-promise-and-packaged-task">promise 与 packaged_task</ChapterLink>
  <ChapterLink href="03-jthread-and-stop-token">jthread 与停止令牌</ChapterLink>
  <ChapterLink href="04-thread-pool">线程池设计</ChapterLink>
</ChapterNav>
