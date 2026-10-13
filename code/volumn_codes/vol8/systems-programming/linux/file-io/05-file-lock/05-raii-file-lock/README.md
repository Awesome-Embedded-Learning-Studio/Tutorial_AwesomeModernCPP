# 05-raii-file-lock —— flock 的 RAII 封装(E5)

环境:WSL2 内核 6.18.33.2,g++ 16.2.1 `-std=c++20 -Wall -Wextra -Wpedantic -O2`,数据文件在 ext4(`~/l05_scratch/e5/raii.bin`)。

## file_lock 的设计要点(`file_lock.hpp`)

- **构造即 open + 阻塞加锁,析构即放锁 + close**:锁的生死 == 对象的生死(close 本身也会释放 flock 锁,显式 `LOCK_UN` 图个可读);
- `defer_lock` 标签构造(std::unique_lock 同思路):只 open 不上锁,留给 `try_lock`/`try_lock_for`;
- `try_lock()` 非阻塞,拿不到返回 false(EWOULDBLOCK 不算错误);其余 errno 理应抛,但 noexcept 语境只能吞——真实项目可放宽成可抛接口(头文件注释里记了这笔账);
- `try_lock_for(d)`:flock 没有「带超时的等待」原生参数,只能 LOCK_NB + 1 ms 轮询,**分辨率即轮询间隔**;
- move-only:moved-from 对象析构不许放别人的锁(锁跟 fd 所有权走);
- 底座是 `common/article.hpp` 的 `unique_fd`/`sys_call`/`errno_code` 三件公共工具。

## 演示时序(`raii_demo.out`)

```
A:构造 file_lock,已写 "ticket=42",持锁 700 ms
B:try_lock() = false(锁在 A 手里)
B:try_lock_for(200ms) = false(实际等了 200 ms,超时)
A:作用域将尽 … A:已放锁
B:try_lock_for(3s) = true(等了 499 ms —— A 一放锁就拿到)
B:读到 "ticket=42"(临界区数据完好)
```

move 场景:lk1 持锁中探针被挡 → `lk2 = std::move(lk1)` → lk1(moved-from)析构后探针**仍被挡**(没放别人的锁)→ `lk2.reset()` 后探针拿到。

## 复现

```sh
mkdir -p ~/l05_scratch/e5
g++ -std=c++20 -Wall -Wextra -Wpedantic -O2 -I ../common -I . raii_demo.cpp -o /tmp/e5
/tmp/e5 | tee raii_demo.out    # 约 0.8 s
```

注意 `-I .`:file_lock.hpp 与 demo 同目录。CMake 用户走上层 CMakeLists 即可。
