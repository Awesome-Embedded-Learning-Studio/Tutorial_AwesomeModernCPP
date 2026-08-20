---
title: "卷四 Project 参考实现"
description: "卷四综合项目（schedule-lite 任务调度器）的完整参考实现：分层任务逐步讲解，每步标注知识点链接回教材章节，含指定初始化器、default 三路比较、concept 约束策略、命令撤销、RAII 观察者、sanitizer 质量门与编译期优先级序对账的真实运行输出。"
chapter: 4
order: 6
tags:
  - host
  - advanced
  - cpp-modern
  - 模板元编程
  - concepts
  - 策略模式
  - 命令模式
  - 观察者模式
  - 回调机制
difficulty: advanced
platform: host
cpp_standard: [17, 20, 23]
reading_time_minutes: 13
prerequisites: []
related: []
---

# 卷四 Project 参考实现

> 全部输出在 WSL Arch（g++ 16.1.1）真实运行得到。参考实现只是**一种**过关方式；你的实现不一样、验收标准对得上，就都是对的。整个项目单文件 `pj.cpp`，编译 `g++ -std=c++20 -Wall -Wextra -Werror pj.cpp -o pj`，会话输入 `session.txt` 用 `<` 重定向喂进去。

## 核心任务（L2）：能跑起来的调度器 {#pj-core}

**思路**：`Task` 成员按 `priority, deadline, id, name` 声明，default `<=>` 自动按这个顺序做字典序比较——「优先级排序」因此免费到手；指定初始化器必须按声明顺序写（作业 4.16-A 实测验证过的 C++20 硬规则）。

**`Task` 与 default 三路比较**——成员顺序即比较顺序，designator 顺序与声明顺序一致。→ 知识点：[三路比较运算符](../05-spaceship-operator.md)「比较顺序」一节、[指定初始化器](../vol2-modern-cpp17/06-designated-initializers.md)（C++20 顺序规则）

```cpp
struct Task
{
    int priority;   // 越小越先执行
    int deadline;
    int id;
    std::string name;

    auto operator<=>(const Task&) const = default;
    bool operator==(const Task&) const = default;
};
```

**`TaskQueue`**——定长 `std::array` + 计数，裸指针迭代器；`pop_last()` 是给撤销留的口子。→ 知识点：[综合项目:fixed_vector](../vol1-basics-cpp11-14/10-fixed-vector.md)「实现骨架」

```cpp
class TaskQueue
{
public:
    void add(const Task& t)
    {
        if (size_ >= kCapacity) throw std::out_of_range("TaskQueue full");
        tasks_[size_++] = t;
    }
    void pop_last()
    {
        if (size_ > 0) --size_;
    }
    std::size_t size() const { return size_; }
    const Task& at(std::size_t i) const { return tasks_[i]; }
    Task* begin() { return tasks_.data(); }
    Task* end() { return tasks_.data() + size_; }
    const Task* begin() const { return tasks_.data(); }
    const Task* end() const { return tasks_.data() + size_; }

private:
    static constexpr std::size_t kCapacity = 16;
    std::array<Task, kCapacity> tasks_{};
    std::size_t size_ = 0;
};
```

**验证输出**（L1 热身 + 会话前段）：

```text
$ g++ -std=c++20 -Wall -Wextra -Werror pj.cpp -o pj && ./pj < session.txt
L1: t1 < t2 ? false
L1: t1 == t1 ? true
commands: add <name> <priority> <deadline> <id> | list | run | rundl | undo | quit
added flash
added radio
added sensor
   #  name    prio  ddl
   0  flash   3   100  (id=1)
   1  radio   1   300  (id=2)
   2  sensor   2   200  (id=3)
```

`t1 < t2` 为 false：priority 3 不小于 1——比较按声明顺序先看 priority。

## 进阶任务（L3）：concept 约束的调度策略 {#pj-advanced}

**思路**：策略抽象成「一个提供 `before(a, b)` 的静态函数」，concept 把这条契约写进签名；`stable_sort` 保证同优先级保持插入序。

```cpp
struct PriorityPolicy
{
    static bool before(const Task& a, const Task& b) { return a < b; }
};

struct DeadlinePolicy
{
    static bool before(const Task& a, const Task& b) { return a.deadline < b.deadline; }
};

template <typename Policy>
concept SchedulePolicy = requires(Task a, Task b) {
    { Policy::before(a, b) } -> std::convertible_to<bool>;
};

static_assert(SchedulePolicy<PriorityPolicy>);
static_assert(SchedulePolicy<DeadlinePolicy>);
static_assert(!SchedulePolicy<int>);

template <SchedulePolicy Policy>
std::vector<Task> schedule(const TaskQueue& q)
{
    std::vector<Task> v(q.begin(), q.end());
    std::stable_sort(v.begin(), v.end(),
                     [](const Task& a, const Task& b) {
                         return Policy::before(a, b);
                     });
    return v;
}
```

→ 知识点：[策略模式](../vol4-generics-patterns/12-strategy.md)「第三步：把策略搬进编译期」「concept 怎么给策略上编译期约束」、[使用 Concepts 约束模板](../vol3-metaprogramming-cpp20-23/02-constraining-templates.md)

`static_assert(!SchedulePolicy<int>)` 成立：`int` 没有 `before` 成员。concept 在这里替调用者挡的是「把不是策略的类型塞进 `schedule`」——报错会点名「`SchedulePolicy<int>` 约束不满足」而不是在模板深处爆炸。

**验证输出**：

```text
> run
run [radio] id=2 priority=1 deadline=300
run [sensor] id=3 priority=2 deadline=200
run [flash] id=1 priority=3 deadline=100
> rundl
run [flash] deadline=100
run [sensor] deadline=200
run [radio] deadline=300
```

同一批任务：`run` 按 priority 升序、`rundl` 按 deadline 升序——两种策略、一个 `schedule` 模板。

## 再进阶任务（L4）：命令撤销 + 观察者 + 质量门 {#pj-expert}

**思路**：命令把「add 一个任务」打包成可撤销对象；观察者用 RAII 令牌让退订自动发生、snapshot 让回调里的增删不炸遍历。

**`AddTaskCommand` + `UndoStack`**——`execute()` 不标 const（命令要记住接收者与参数）。→ 知识点：[命令模式](../vol4-generics-patterns/13-command.md)「第二步：把动作封装成对象」

```cpp
struct Command
{
    virtual ~Command() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
};

class AddTaskCommand : public Command
{
public:
    AddTaskCommand(TaskQueue& q, Task t) : q_(q), t_(std::move(t)) {}
    void execute() override { q_.add(t_); }
    void undo() override { q_.pop_last(); }

private:
    TaskQueue& q_;
    Task t_;
};

class UndoStack
{
public:
    void execute(std::unique_ptr<Command> c)
    {
        c->execute();
        history_.push_back(std::move(c));
    }
    void undo()
    {
        if (history_.empty()) return;
        history_.back()->undo();
        history_.pop_back();
    }

private:
    std::vector<std::unique_ptr<Command>> history_;
};
```

**`EventSource`：RAII 订阅令牌 + snapshot 通知**。→ 知识点：[观察者模式](../vol4-generics-patterns/17-observer.md)「第四步：RAII 订阅」「snapshot 通知」一节

```cpp
struct TaskDoneEvent
{
    std::string name;
};

class EventSource
{
public:
    using Callback = std::function<void(const TaskDoneEvent&)>;

    class Subscription
    {
    public:
        Subscription() = default;
        Subscription(std::size_t id, EventSource* owner) : id_(id), owner_(owner) {}
        Subscription(const Subscription&) = delete;
        Subscription& operator=(const Subscription&) = delete;
        Subscription(Subscription&& o) noexcept
            : id_(o.id_), owner_(o.owner_)
        {
            o.owner_ = nullptr;
            o.id_ = 0;
        }
        Subscription& operator=(Subscription&& o) noexcept
        {
            if (this != &o) {
                unsubscribe();
                id_ = o.id_;
                owner_ = o.owner_;
                o.owner_ = nullptr;
                o.id_ = 0;
            }
            return *this;
        }
        ~Subscription() { unsubscribe(); }

        void unsubscribe()
        {
            if (owner_) {
                owner_->detach(id_);
                owner_ = nullptr;
                id_ = 0;
            }
        }

    private:
        std::size_t id_ = 0;
        EventSource* owner_ = nullptr;
    };

    Subscription subscribe(Callback cb)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        std::size_t id = next_id_++;
        cbs_.emplace(id, std::move(cb));
        return Subscription{id, this};
    }

    void detach(std::size_t id)
    {
        std::lock_guard<std::mutex> lk(mtx_);
        cbs_.erase(id);
    }

    void emit(const TaskDoneEvent& e)
    {
        std::vector<Callback> snap;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (auto& [id, cb] : cbs_) snap.push_back(cb);
        }
        for (auto& cb : snap) {
            try {
                cb(e);
            } catch (...) {
            }
        }
    }

private:
    std::mutex mtx_;
    std::unordered_map<std::size_t, Callback> cbs_;
    std::size_t next_id_ = 1;
};
```

**验证输出**（`run` 触发 `[done]`、`undo` 撤销最近一次 add）：

```text
> run
run [radio] id=2 priority=1 deadline=300
  [done] radio
run [sensor] id=3 priority=2 deadline=200
  [done] sensor
run [flash] id=1 priority=3 deadline=100
  [done] flash
> undo
undone
   #  name    prio  ddl
   0  flash   3   100  (id=1)
   1  radio   1   300  (id=2)
> add gps 5 50 4
added gps
> run
run [radio] id=2 priority=1 deadline=300
  [done] radio
run [flash] id=1 priority=3 deadline=100
  [done] flash
run [gps] id=4 priority=5 deadline=50
  [done] gps
```

**质量门**：`-Werror` 构建零警告；sanitizer 构建跑完整会话零报告：

```text
$ g++ -std=c++20 -Wall -Wextra -O1 -g -fsanitize=address,undefined pj.cpp -o pj_san
$ ./pj_san < session.txt
...（完整会话输出,末尾无任何 sanitizer 报告）
-- L5 reconcile --
compile-time order: 1 2 5 9 9
runtime order:     1 2 5 9 9
```

## 终极挑战（L5）：编译期优先级序与运行期对账 {#pj-l5}

**思路**：`CCons<V, T>` 把 NTTP 值装进类型列表；`SortAsc` 是选择排序——每轮挑最小值放最前、递归处理剩余；运行期的 `schedule<PriorityPolicy>` 用的比较规则（priority 升序）与编译期完全同一套，所以两条线必然对得上。

**编译期元函数**。→ 知识点：[非类型模板参数](../vol1-basics-cpp11-14/05-non-type-parameters.md)（NTTP 值）、[模板特化与偏特化](../vol1-basics-cpp11-14/04-specialization-partial.md)（递归特化）、[模板导论](../vol1-basics-cpp11-14/01-templates-introduction.md)「编译期图灵完备」

```cpp
struct Nil {};

template <auto V, typename T>
struct CCons
{
    static constexpr auto Head = V;
    using Tail = T;
};

template <typename List>
struct MinVal;

template <>
struct MinVal<Nil>
{
    static constexpr int value = 2147483647;
};

template <auto V, typename T>
struct MinVal<CCons<V, T>>
{
    static constexpr int tail = MinVal<T>::value;
    static constexpr int value = V < tail ? V : tail;
};

template <typename List, int V2>
struct RemoveFirstOfValue;

template <int V2>
struct RemoveFirstOfValue<Nil, V2>
{
    using type = Nil;
};

template <auto V, typename T, int V2>
struct RemoveFirstOfValue<CCons<V, T>, V2>
{
    using type = std::conditional_t<
        (V == V2),
        T,
        CCons<V, typename RemoveFirstOfValue<T, V2>::type>>;
};

template <typename List>
struct SortAsc;

template <>
struct SortAsc<Nil>
{
    using type = Nil;
};

template <auto V, typename T>
struct SortAsc<CCons<V, T>>
{
    static constexpr int m = MinVal<CCons<V, T>>::value;
    using rest = typename RemoveFirstOfValue<CCons<V, T>, m>::type;
    using type = CCons<m, typename SortAsc<rest>::type>;
};

using Priorities = CCons<2, CCons<9, CCons<1, CCons<5, CCons<9, Nil>>>>>;
using SortedP = typename SortAsc<Priorities>::type;

static_assert(SortedP::Head == 1, "");
static_assert(SortedP::Tail::Head == 2, "");
static_assert(SortedP::Tail::Tail::Head == 5, "");
static_assert(SortedP::Tail::Tail::Tail::Head == 9, "");
static_assert(SortedP::Tail::Tail::Tail::Tail::Head == 9, "");
```

**对账段**——运行期用同一组 priority 建任务、同一套「priority 升序」规则排序：

```cpp
    std::cout << "-- L5 reconcile --\n";
    std::cout << "compile-time order: ";
    print_vals<SortedP>();

    TaskQueue l5q;
    l5q.add(Task{.priority = 2, .deadline = 0, .id = 1, .name = "a"});
    l5q.add(Task{.priority = 9, .deadline = 0, .id = 2, .name = "b"});
    l5q.add(Task{.priority = 1, .deadline = 0, .id = 3, .name = "c"});
    l5q.add(Task{.priority = 5, .deadline = 0, .id = 4, .name = "d"});
    l5q.add(Task{.priority = 9, .deadline = 0, .id = 5, .name = "e"});
    auto ordered = schedule<PriorityPolicy>(l5q);
    std::cout << "runtime order:     ";
    for (const auto& t : ordered) std::cout << t.priority << " ";
    std::cout << "\n";
```

**验证输出**：

```text
-- L5 reconcile --
compile-time order: 1 2 5 9 9
runtime order:     1 2 5 9 9
```

两条线对上，因为比较规则是同一套（priority 升序）；`SortAsc` 是选择排序（每轮扫全表挑最小），编译期复杂度 O(n²)——n 是任务数，规模小无所谓。

到这里，「本卷的知识点是一体的」就有了实物：一个调度器，三路比较与指定初始化器是数据、concept 是契约、策略是调度心脏、命令与观察者是工程骨架、sanitizer 是底线，而模板元编程在编译期提前把调度顺序验了一遍。
