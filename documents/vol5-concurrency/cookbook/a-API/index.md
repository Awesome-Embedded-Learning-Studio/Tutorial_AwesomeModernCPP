# cookbook A 区：API 配方

按 API 查的速查区：每个配方一个问题、一段最小代码、要点与常见错误，末尾链回正文对应篇。读者画像与配方格式的完整约定见 cookbook 区的[定位声明](../index)。配方是查表文，机制的讲解归正文，您从这里链过去。

| 序号 | 配方 | 承源 | 链回正文 |
|------|------|------|----------|
| 01 | [mutex 与 RAII 锁的选择](01-mutex-and-raii) | 旧文《mutex 与 RAII 锁》（ch02/01） | [《用互斥保护共享状态》](../../chapter02-sharing-data/02-mutex-and-locks) |
| 02 | [recursive_mutex 与 timed_mutex](02-recursive-and-timed-mutex) | 从旧文《mutex 与 RAII 锁》拆出来单讲（ch02/01） | [《用互斥保护共享状态》](../../chapter02-sharing-data/02-mutex-and-locks) |
| 03 | [call_once 与 once_flag，以及和 Meyers singleton 的取舍](03-call-once) | 从旧文《thread_local 与 call_once》拆出来单讲（ch01/04） | [《选择内存序需要什么依据》](../../chapter05-atomic-memory-model/03-choosing-memory-orders) |
| 04 | [shared_mutex 读写锁](04-shared-mutex) | 旧文《读写锁与 shared_mutex》（ch02/04） | [《等待一个条件成立》](../../chapter03-wait-and-coordinate/01-wait-for-a-condition) |
| 05 | [多锁获取工具：std::lock、try_lock 回退、层级锁](05-multi-lock) | 从旧文《死锁与锁顺序》拆出来单讲（ch02/02） | [《减少共享与避免死锁》](../../chapter02-sharing-data/04-sharing-less-and-deadlock) |
| 06 | [jthread 与停止协议速查](06-jthread-and-stopping) | 旧文《jthread 与停止令牌》（ch05/03） | [《让工作有序结束：std::jthread 与协作式停止》](../../chapter01-start-concurrency/05-jthread-and-stopping) |
| 07 | [atomic_wait 与 atomic_notify](07-atomic-wait-notify) | 从旧文《atomic_wait 与 atomic_ref》拆出来单讲（ch03/04） | [《原子操作解决到哪一步》](../../chapter05-atomic-memory-model/01-what-atomics-solve) |
| 08 | [atomic_ref：给非原子变量套原子操作](08-atomic-ref) | 从旧文《atomic_wait 与 atomic_ref》拆出来单讲（ch03/04） | [《原子操作解决到哪一步》](../../chapter05-atomic-memory-model/01-what-atomics-solve) |
| 09 | [atomic_signal_fence](09-atomic-signal-fence) | 从旧文《fence 与编译器屏障》拆出来单讲（ch03/03） | [《选择内存序需要什么依据》](../../chapter05-atomic-memory-model/03-choosing-memory-orders) |
| 10 | [shared_future](10-shared-future) | 从旧文《promise 与 packaged_task》拆出来单讲（ch05/02） | [《用 std::async 执行计算》](../../chapter04-organizing-tasks/01-launch-with-async) |

配方里的代码全部由咱们在 `/tmp/concurrency-cookbook/` 实测（GCC 16.2.1，`g++ -std=c++20 -O2 -pthread`），输出见各篇。主题选读区（B 区）见 [../b-topics/](../b-topics/)。
