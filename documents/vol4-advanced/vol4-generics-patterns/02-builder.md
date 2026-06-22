---
title: "构建器模式:从一坨构造参数到流式构造器"
description: "从最原始的「一锅炖构造函数」开始,一步步逼出流式构建器,顺手用 std::optional 干掉 isValid 标志位,最后用阶段式构建器把「忘填必填项」从运行时错压成编译期错"
chapter: 11
order: 2
tags:
  - host
  - cpp-modern
  - intermediate
  - 构建器模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 22
related:
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
---

# 构建器模式:从一坨构造参数到流式构造器

## 我们到底在解决什么问题

想一个特别朴素的场景。你写了一个待办项 `Task`,它有必填字段——优先级、截止时间、任务描述,也有选填字段——标题、备注。第一版你图省事,直接给 `Task` 塞一个把所有字段全列出来的构造函数,然后调用处长这样:

```cpp
Task* a_task = new Task(
    Task::Priority::High,
    CTime{2025, 9, 24, 20, 38, 11},
    "This is a Demo Task",
    "Demo Tasks are placed for a detailed test",
    "A Task");
```

写完你盯着这行代码看三秒。问题已经在往外冒了:调用方必须记住参数的顺序,标题塞到第三个位置还是第五个位置全靠数逗号;哪天你想加一个新字段,比如外链 `links`,这个构造函数的签名一改,全仓库成千上万处调用都得跟着改;更糟的是,构造函数一旦变得复杂,它会在你没法控制的流程里抛异常——构造失败,对象压根不存在,可你手里已经攥着一个半初始化的状态,接也接不住,改也改不动。你的同事已经在对你进行严酷的git blame审判，你只好急得团团转。

真正的问题是我们在一行代码里**同时干了三件事**:第一,把构造材料(那串参数)提交进去;第二,执行构造流程本身(校验、赋值、可能还连了数据库);第三,让 `a_task` 真正指向一个合法存在的 `Task` 对象。提交材料、执行构造、交付对象——这三步被死死焊在了一个构造函数里,我们没有任何一个环节插得进手。

> 请注意，笔者之前接触过一点Java，我就发现构建器在被滥用了，所以笔者认为——只有发现场景如上述所说的那样复杂了，请上构建器，要不然该怎么构造就这么构造。

构建器模式要解决的就是这件事:**把「收集材料」「校验」「真正构造」这三步拆开,挪到一个专门的中间人——Builder——手里,让客户程序员有机会优雅地、可插拨地把对象一步步搭起来。** 接下来我们就一步步来,从最蠢的写法开始,看每一步为什么还不够。

## 第一步:最原始的写法——一个巨型构造函数(错误示范)

很多人的第一反应就是上面那一坨。说实话,小对象这么写完全没问题,`Point(int x, int y)` 谁都觉得舒服。可一旦字段超过四五个,还掺杂着必填、选填,这个构造函数就开始反人类了。

这里有两层毛病。第一层是**可读性**:五个 `std::string` 挤在一起,你分不清哪个是标题、哪个是描述、哪个是备注,IDE 的参数提示救得了开发机,救不了 code review 时的肉眼。第二层更隐蔽,是**耦合**:构造函数承担了「接收参数 + 校验合法性 + 可能还做点副作用(写日志、连数据库)」的全部职责,而这些事情一旦失败,你连一个「构造到一半」的对象都拿不到——构造函数要么成功,要么抛异常走人,中间没有缓冲带。

你可能会想:那我不抛异常,我加一个 `bool is_valid{false}` 成员,构造完手动检查不就行了?这条路也能走,但代价是每个 `Task` 对象从此都要背一个 `is_valid` 标志位,业务代码里到处都得判 `if (task.is_valid)`,类的状态被一个「有效性」标志污染得一塌糊涂。**我们用对象,本来就是为了把状态封装起来,现在倒好,封装出了一个自带「我可能是个废对象」的标志位。**

所以这条路也走不通。我们得让构造这件事可以分步、可以中途检查、可以把校验逻辑从 `Task` 本体里挪出去。

## 第二步:getter/setter 化简——把选填项挪到构造函数外

有经验的朋友已经开始嘀咕了:选填字段根本不该塞进构造函数,给它们一对 getter/setter 不就完了?完全正确。我们先把字段分成两类——「`Task` 存在之前必须有效」的必填项,和「之后可以慢慢配」的选填项,必填项留在构造函数里,选填项用 setter 后配:

```cpp
class Task {
public:
    enum class Priority { Immediate, High, Medium, Low };
    struct CTime { int year, month, day, hour, minute, second; };

    // 必填:优先级、截止时间、描述
    Task(Priority p, CTime ddl, const std::string& desc)
        : priority_(p), ddl_(ddl), description_(desc) {
        if (desc.empty()) {
            throw std::invalid_argument("Invalid Task Description");
        }
        // 可能还要写日志、连数据库……
    }

    void set_title(std::string t)   { title_ = std::move(t); }
    void set_details(std::string d) { details_ = std::move(d); }

private:
    Priority                       priority_;
    CTime                          ddl_;
    std::string                    description_;
    std::optional<std::string>     title_;
    std::optional<std::string>     details_;
};
```

这一步已经比一坨构造函数强多了——构造函数瘦了下来,选填项可以按需补。但你现在看 `Task` 这个类,会发现它背着两个职责:**它既是「一个待办项的业务对象」,又是「构造自己的工具」**。校验逻辑、setter、那个写日志的副作用,全挤在 `Task` 里。构造逻辑和业务逻辑搅在一起,类越来越脏。

更难受的是,校验失败还是只能抛异常。`Task` 一旦复杂起来,构造函数里的校验、赋值、副作用会越堆越多,你想换个失败处理策略(比如从抛异常改成返回错误码),就得改 `Task` 本体——可 `Task` 是业务对象,被全仓库引用,你动它一根毛都得拉一票人 review。（顺便还会有喷你的口水）

事情到这里还没完。我们真正想问的是:**能不能把「怎么构造」这件事,整体从 `Task` 里抽出来,交给一个专门的工具类?** 这样 `Task` 只管自己的业务语义,构造的细节、校验的策略、失败的兜底,都归那个工具管,互不打扰。

## 第三步:把构造任务委托出去——简单构建器

这个「专门负责构造的工具类」,就是 **Builder**。我们让 `Task` 把 `Builder` 认作友元,自己只留一个私有的「把字段塞进来」的口子,所有收集材料、校验、组装的活儿全交给 `TaskBuilder`。

这里有个特别顺手的设计:字段「有没有被填过」这件事,我们不再用 `bool` 标志位记,而是直接用 `std::optional` 记。`std::optional<Task::Priority>` 既是「一个 `Priority` 值的容器」,又是「这个值填没填」的开关——你像查指针一样 `if (priority_)` 就能判断有没有填,`*priority_` 就能取值。省掉了一堆 `is_xxx_set` 标志位,类的状态干干净净。

```cpp
class TaskBuilder {
public:
    void set_priority(Task::Priority p) { priority_ = p; }
    void set_ddl(Task::CTime d)          { ddl_ = d; }
    void set_description(std::string d)  { description_ = std::move(d); }
    void set_title(std::string t)        { title_ = std::move(t); }
    void set_details(std::string d)      { details_ = std::move(d); }

    std::optional<Task> build() const {
        // 必填项没填齐,就返回 nullopt,把失败内化进返回类型
        if (!priority_ || !ddl_ || !description_) {
            return std::nullopt;
        }
        Task t(*priority_, *ddl_, *description_);
        if (title_)   t.set_title(*title_);
        if (details_) t.set_details(*details_);
        return t;
    }

private:
    std::optional<Task::Priority> priority_;
    std::optional<Task::CTime>    ddl_;
    std::optional<std::string>    description_;
    std::optional<std::string>    title_;
    std::optional<std::string>    details_;
};
```

你看,校验逻辑现在住在 `TaskBuilder` 里,跟 `Task` 的业务本体彻底解耦了。`build()` 返回的是 `std::optional<Task>`,这意味着「构造可能失败」这件事直接编码进了返回类型——调用方拿到结果,被迫去处理「可能是 `nullopt`」这层,你再也忘不掉失败分支了。比起抛异常,这种方式更稳:构造失败是一个「正常预期内的结果」,而不是一个突然炸出来的控制流跳转。

::: tip std::optional 是个极好用的工具类
我们可以像检查指针一样检查这个成员有没有被填过——`if (priority_)` 判断、`*priority_` 取值。现在我们终于不用维护一堆 `is_xxx_valid` 的 bool 标志位了,「有没有值」这件事直接内化进了 `std::optional` 的类型语义里。
:::

用起来是这样的,一行一个 setter,最后 `build()`:

```cpp
TaskBuilder builder;
builder.set_priority(Task::Priority::High);
builder.set_ddl({2025, 9, 25, 10, 0, 0});
builder.set_description("Prepare blog post");
builder.set_title("Simple Builder");
builder.set_details("Non-fluent style");

std::optional<Task> maybe_task = builder.build();
if (maybe_task) {
    maybe_task->do_work();
}
```

挺好,能跑。但你写着写着就会觉得累——每设一个字段都得重复一遍 `builder.`,五遍、十遍地敲下去,眼睛也花,手也酸。用过 Kotlin 的 `apply`、写过 jQuery 的朋友会更敏感:**这种「链式」的 API,明明可以一句话串起来的,为什么要断成十行?**

## 第四步:让构建器流动起来——流式构建器

诀窍小到几乎免费:每个 `with_*` 方法在设完字段之后,**返回构建器自身的引用** `return *this;`。这样一来,上一次调用的返回值就是构建器自己,你立刻能在它身上接下一次调用,调用链就流动起来了。

```cpp
class TaskBuilder {
public:
    TaskBuilder& with_priority(Task::Priority p) {
        priority_ = p;
        return *this;
    }
    TaskBuilder& with_ddl(Task::CTime d)         { ddl_ = d;            return *this; }
    TaskBuilder& with_description(std::string s) { description_ = std::move(s); return *this; }
    TaskBuilder& with_title(std::string t)       { title_ = std::move(t);       return *this; }
    TaskBuilder& with_details(std::string d)     { details_ = std::move(d);     return *this; }

    Task build() const {
        if (!priority_ || !ddl_ || !description_) {
            throw std::runtime_error("Cannot build Task: missing required field");
        }
        Task t(*priority_, *ddl_, *description_);
        if (title_)   t.set_title(*title_);
        if (details_) t.set_details(*details_);
        return t;  // RVO
    }

private:
    std::optional<Task::Priority> priority_;
    std::optional<Task::CTime>    ddl_;
    std::optional<std::string>    description_;
    std::optional<std::string>    title_;
    std::optional<std::string>    details_;
};
```

注意这里我顺手把 `build()` 的失败策略从「返回 `std::optional`」换成了「抛异常」。两种都是合法的工程选择,区别在于你怎么看待「构造失败」这件事:如果你觉得它是预期内、调用方该顺手处理的小概率,`std::optional` 更合适,失败被编码进类型;如果你觉得「必填项都没填齐就 build」是程序员犯了糊涂、属于不该发生的逻辑错误,抛异常更直接,能把错误冒泡到上层统一兜底。这里我们用异常,因为它能把后面的演示做得更清楚。

调用现场一下子就读起来像一句话了:

```cpp
Task task = TaskBuilder{}
                .with_priority(Task::Priority::High)
                .with_ddl({2025, 9, 25, 10, 0, 0})
                .with_description("Finish Builder blog")
                .with_title("Blog Writing")
                .with_details("Explain fluent builder")
                .build();
```

这里我们先验证一下,这串链式调用真的能跑通,缺必填项也真的会抛异常:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra builder_verify.cpp -o builder_verify
$ ./builder_verify
Task{desc=Finish Builder blog, prio=1, ddl=2025-9-25, title=Fluent Builder, details=return *this chains the calls}
caught: Cannot build Task: missing required field
```

完整构造的对象字段齐全,漏填 `priority` 和 `ddl` 的那次直接被 `build()` 拦下抛了异常。链式调用、`std::optional` 标志位、运行时校验,三件事都对上了。

接下来问题来了。链式调用有个副作用,它让构建器本身变成了一个**可以到处传递的中间状态**——这恰恰是它的另一个本事:延迟构造。你看,既然每个 `with_*` 都返回构建器自己,我们完全可以把构造过程「暂停」在某一步,把构建器当参数丢给别的子系统,等那边查完数据库、拿到真正的标题,再接着填:

```cpp
auto partial = TaskBuilder{}
                   .with_priority(Task::Priority::High)
                   .with_ddl({2025, 9, 25, 10, 0, 0})
                   .with_description("Complete the final project report.");

// 把半成品构建器传出去,等异步查到真正的标题再接着 build
std::string title = data_base.query_title_by_time({2025, 9, 25, 10, 0, 0});
Task task = partial.with_title(title)
                  .with_details("Check all data points.")
                  .build();
```

你会发现这里有个特别值钱的好处:**在代码里流通的 `Task` 对象,从此都是「构造完毕、字段有效」的对象**,不会再有「半初始化的 `Task` 满世界乱窜」的尴尬。半成品的状态被锁在 `TaskBuilder` 里,只有 `build()` 出关的那一刻,才放出一个完整的 `Task`。类型系统替我们把「成品」和「在制品」隔离开了。

::: warning 别在多线程里复用同一个构建器
流式构建器是有可变状态的。一个 `TaskBuilder` 被两个线程同时 `with_*` 然后 `build()`,字段读写没有任何同步,是纯粹的数据竞争。要么每个线程各用各的构建器实例,要么把构建器当作「构造完即弃」的临时对象——上面那种 `TaskBuilder{}...build()` 写法,构建器用完就销毁,是最安全的用法。想跨线程传递半成品,要么传值拷贝一份,要么老老实实加锁。
:::

## 这里先验证一下:RVO 真的省掉了那次拷贝吗

`build()` 里有一个局部对象 `t`,然后 `return t;`。直觉上从函数里把一个大对象搬出来,怎么也得 move 一次吧?我们先别急着信,实测一下,给对象挂一个 move/copy 计数器:

```cpp
class Tracked {
public:
    int v;
    static inline int kMoveCount = 0;
    static inline int kCopyCount = 0;

    Tracked() : v(0) {}
    explicit Tracked(int x) : v(x) {}
    Tracked(Tracked&& o) noexcept : v(o.v) { ++kMoveCount; }
    Tracked(const Tracked& o) : v(o.v)     { ++kCopyCount; }
};

class TrackedBuilder {
public:
    TrackedBuilder& with_value(int x) { value_ = x; return *this; }
    Tracked build() const {
        Tracked t(*value_);   // 局部对象
        return t;             // 预期被 NRVO / RVO 消除
    }
private:
    std::optional<int> value_;
};

int main() {
    Tracked t = TrackedBuilder{}.with_value(42).build();
    std::cout << "value=" << t.v
              << " moves=" << Tracked::kMoveCount
              << " copies=" << Tracked::kCopyCount << "\n";
}
```

为了排除「是优化器在 `-O2` 下变魔术」的怀疑,我们 `-O2` 和 `-O0` 各跑一次:

```sh
$ g++ -std=c++23 -O2 rvo_verify.cpp -o rvo_verify && ./rvo_verify
value=42 moves=0 copies=0
$ g++ -std=c++23 -O0 rvo_verify.cpp -o rvo_verify_O0 && ./rvo_verify_O0
value=42 moves=0 copies=0
```

关掉优化,moves 和 copies 还是 0。这不是编译器的恩赐,是**标准保证**——C++17 起,`return` 一个同名局部对象时,拷贝/移动**被强制省略**(*mandatory copy elision*),对象直接在调用方的栈帧上构造,中间压根没有「先造一个临时对象再搬过来」这一步。所以我们放心地在 `build()` 里 `return t;`,无论 `Task` 多重,都不付一次拷贝的代价。

> 请注意，C++17开始才有这个福利，所以，别着急拿到C++11~14去，很可能只有在开优化的时候才奏效。

## 真正的坑在后面:运行时才发现「忘填必填项」

流式构建器好是好,但它有一个怎么都绕不过去的毛病——**必填项的校验只能拖到运行时**。你写 `TaskBuilder{}.with_ddl(...).build()`,漏了 `priority` 和 `description`,编译器一句废话都没有,稳稳编过,直到程序跑起来、`build()` 一抛异常你才恍然大悟。

问题出在哪儿?出在 `TaskBuilder` 这个类型本身。它把「填了 priority 的构建器」「填了 priority 和 ddl 的构建器」「全都填齐的构建器」统统表达成了**同一个类型** `TaskBuilder`。类型系统分不清它们,自然就没法在编译期替你把关——它看到的只是一个「字段都还没填」的 `TaskBuilder`,你调不调 `with_priority` 是你的事,它管不着。

那有没有办法让类型系统参与进来?有。思路是:**每填一个必填项,就让构建器「变身」成一个新的类型,只有走完了所有必填阶段,你才拿得到那个「能 `build()`」的类型。** 漏掉任何一步,你手里攥着的类型压根就没有 `build()` 方法,编译器当场就把你拦下。这就是**阶段式构建器(Staged Builder / Typed Builder)**。

## 第五步:把必填项校验压到编译期——阶段式构建器

我们先定义一个把所有字段攒在一起的内部草稿 `TaskDraft`,它会被在各阶段之间 move 传递。然后,我们给每一个「填字段」的步骤设计一个独立的类型——`SetPriority`、`SetDdl`、`SetDescription`、`OptionalStage`——每个类型的 `with_*` 方法返回的是**下一个阶段的类型**:

```cpp
struct TaskDraft {
    std::optional<Task::Priority> priority;
    std::optional<Task::CTime>    ddl;
    std::optional<std::string>    description;
    std::optional<std::string>    title;
    std::optional<std::string>    details;
};

struct SetDdl;
struct SetDescription;
struct OptionalStage;

struct SetPriority {
    TaskDraft d;
    SetDdl with_priority(Task::Priority p);          // 返回下一阶段
};
struct SetDdl {
    TaskDraft d;
    SetDescription with_ddl(Task::CTime ddl);        // 返回下一阶段
};
struct SetDescription {
    TaskDraft d;
    OptionalStage with_description(std::string desc);  // 进入可选阶段
};
struct OptionalStage {
    TaskDraft d;
    OptionalStage& with_title(std::string t)   { d.title = std::move(t);   return *this; }
    OptionalStage& with_details(std::string det) { d.details = std::move(det); return *this; }
    Task build() {
        // 三个必填字段已被类型系统强制填过,这里无需运行时校验
        Task t(*d.priority, *d.ddl, std::move(*d.description));
        if (d.title)   t.set_title(*d.title);
        if (d.details) t.set_details(*d.details);
        return t;
    }
};
```

注意一个关键差别:`OptionalStage::build()` 里**再也没有 `if (!priority || ...)` 这段校验了**。为什么不需要?因为类型系统已经替你保证了:你能拿到 `OptionalStage` 这个类型的唯一路径,就是依次走完 `with_priority` → `with_ddl` → `with_description`——而每一步都把对应的 `optional` 填实了。到 `build()` 那一刻,三个必填字段必然非空,`*d.priority` 这种解引用是绝对安全的。这就是「把运行时检查压成编译期保证」的味道。

用起来是这样一个严格的链:

```cpp
struct TaskBuilder {
    static SetPriority create() { return SetPriority{TaskDraft{}}; }
};

Task t = TaskBuilder::create()
             .with_priority(Task::Priority::High)
             .with_ddl({2025, 9, 25, 10, 0, 0})
             .with_description("Staged builder")
             .with_title("Typed")
             .build();
```

我们先验证一下正确用法能跑:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra staged_builder_verify.cpp -o staged_builder_verify
$ ./staged_builder_verify
Task{desc=Staged builder, title=Typed}
```

接下来就是见证威力的时刻。我们故意犯两种最常见的错,看看编译器怎么拦。

第一种:**漏填必填项直接 build**。假设我们只填了 `priority` 和 `ddl`,跳过 `with_description`,直接想 `.build()`:

```cpp
Task t = TaskBuilder::create()
             .with_priority(Task::Priority::High)
             .with_ddl({2025, 9, 25, 10, 0, 0})
             .build();   // ← 试图在 SetDescription 上调 build()
```

编译器的反应:

```sh
$ g++ -std=c++23 staged_missing.cpp -o staged_missing
staged_missing.cpp:7:19: error: 'struct SetDescription' has no member named 'build'
```

`SetDescription` 这个类型根本没有 `build()` 方法——`build()` 只存在于 `OptionalStage` 上。你拿不到 `OptionalStage`(因为没调 `with_description`),自然就 build 不出来。漏填必填项,编译期直接红牌。

第二种:**顺序写反**。有人手快,把 `with_ddl` 写在了 `with_priority` 前面:

```cpp
auto x = TaskBuilder::create()
             .with_ddl({2025, 9, 25, 10, 0, 0});   // ← 在 SetPriority 上调 with_ddl()
```

编译器的反应:

```sh
$ g++ -std=c++23 staged_wrongorder.cpp -o staged_wrongorder
staged_wrongorder.cpp:4:36: error: 'struct SetPriority' has no member named 'with_ddl'
```

`SetPriority` 没有 `with_ddl` 方法——`with_ddl` 是 `SetDdl` 阶段的事。你必须先 `with_priority` 变身成 `SetDdl`,才配调 `with_ddl`。调用顺序被类型流硬钉住了。

这就是阶段式构建器把两件事都压成编译期错的实锤:**漏填必填项、调用顺序颠倒,都过不了编译。** 运行时异常那套彻底免了。

::: tip 阶段式构建器的代价
天下没有免费的午餐。这套机制的代价是**类型设计变复杂**——每个必填阶段都要单独定义一个 struct,字段在阶段间 move 传递。字段一多,阶段的数量也会跟着涨。所以它适合「必填项不多、但绝对不能漏」的场景(比如协议头、安全相关的配置);如果你的对象可选字段一大堆、必填项就那么两三个,前面那套普通流式构建器配运行时校验,通常就够用了,没必要为编译期校验背上类型膨胀的包袱。
:::

## 第六步:职责切分——组合式构建器

回头再看,流式构建器把所有 `with_*` 方法都堆在一个 `TaskBuilder` 类里。字段一多,这个类就会膨胀成一个无所不包的「超级构造器」,必填的、可选的、甚至是「按业务域分组的」(比如「安全相关字段」「日志相关字段」)全挤一块。哪天你想给某个业务域加一组新字段,就得改 `TaskBuilder` 本体——这就违背了我们费半天劲要追求的开闭原则(OCP)。

组合式构建器(Composite Builder)的思路是把职责切开:**一个基础 Builder 持有所有字段、负责最终的 `build()`;围绕它派生出若干个子构造器,每个子构造器只负责一类字段。** 子构造器不持有字段副本,而是持有基础 Builder 的引用——设完字段后调一个 `done_xxx()` 切回基础 Builder,再从基础 Builder 跳到下一个子构造器。要加新的一组字段?新写一个子构造器挂上去就行,基础 Builder 和别的子构造器一行都不用动。

```cpp
class TaskBuilder;        // 基础 Builder:持有所有字段 + build()
class BuilderMain;        // 子构造器 A:负责必填字段
class BuilderOptional;    // 子构造器 B:负责可选字段

class TaskBuilder {
public:
    std::optional<Task::Priority> priority;
    std::optional<Task::CTime>    ddl;
    std::optional<std::string>    description;
    std::optional<std::string>    title;
    std::optional<std::string>    details;

    BuilderMain     main();       // 进入「必填字段」子构造器
    BuilderOptional optional();   // 进入「可选字段」子构造器

    Task build() const {
        if (!priority || !ddl || !description) {
            throw std::runtime_error("Task build error: missing required field");
        }
        Task t(*priority, *ddl, *description);
        if (title)   t.set_title(*title);
        if (details) t.set_details(*details);
        return t;
    }
};

class BuilderMain {
public:
    explicit BuilderMain(TaskBuilder& b) : b_(b) {}
    BuilderMain& with_priority(Task::Priority p) { b_.priority = p;            return *this; }
    BuilderMain& with_ddl(Task::CTime d)         { b_.ddl = d;                 return *this; }
    BuilderMain& with_description(std::string s) { b_.description = std::move(s); return *this; }
    TaskBuilder& done_main() { return b_; }       // 设完必填,切回基础 Builder
private:
    TaskBuilder& b_;
};

class BuilderOptional {
public:
    explicit BuilderOptional(TaskBuilder& b) : b_(b) {}
    BuilderOptional& with_title(std::string t)   { b_.title = std::move(t);   return *this; }
    BuilderOptional& with_details(std::string d) { b_.details = std::move(d); return *this; }
    TaskBuilder& done_optional() { return b_; }   // 设完可选,切回基础 Builder
private:
    TaskBuilder& b_;
};

BuilderMain     TaskBuilder::main()     { return BuilderMain(*this); }
BuilderOptional TaskBuilder::optional() { return BuilderOptional(*this); }
```

这段代码有几个细节值得拆开看。基础 Builder 的字段全用 `public`,不是图省事——是为了让子构造器能直接读写它们,避免一层层 getter/setter。子构造器持有的是 `TaskBuilder&` 引用而不是副本,所以「在 `BuilderMain` 里设字段」「在 `BuilderOptional` 里设字段」其实是在改同一个基础 Builder,最后 `build()` 读的是同一份状态。`done_main()` / `done_optional()` 返回的是基础 Builder 的引用,这让「子构造器 → 基础 Builder → 另一个子构造器」的切换能串成一条链。

调用现场因此长得像一段分了段落的句子——先进 `main()` 设必填,`done_main()` 回到基础 Builder,再进 `optional()` 设可选,`done_optional()` 回来,最后 `build()`:

```cpp
TaskBuilder base;

Task t = base.main()
             .with_priority(Task::Priority::High)
             .with_ddl({2025, 9, 25, 10, 0, 0})
             .with_description("Composite builder")
             .done_main()
             .optional()
             .with_title("Project Report")
             .with_details("Check all data points")
             .done_optional()
             .build();
```

同样验证一下,完整构造和漏填必填两种情况:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra composite_builder_verify.cpp -o composite_builder_verify
$ ./composite_builder_verify
Task{desc=Composite builder, title=Project Report, details=Check all data points}
caught: Task build error: missing required field
```

到这一步,我们就有了一个职责分明、可扩展、满足开闭原则的构建器:基础 Builder 管最终装配,子构造器管各自的字段分组,要加新的字段分组,只要新挂一个子构造器,旧代码一行都不用动。

## 这几种构建器到底怎么选

我们把一路走过的几种形态横向对一下,你看哪种最贴你的场景:

| 风格 | 调用形态 | 强在哪 | 弱在哪 |
|---|---|---|---|
| 巨型构造函数 | `Task(p, ddl, desc, t, d)` | 写起来最快,小对象够用 | 字段一多可读性崩;构造逻辑耦合进业务类 |
| 简单构建器(非流式) | `b.set_xxx(...)` 一行行调 | 实现最直白 | 调用啰嗦;链式用不了 |
| 流式构建器(Fluent) | `b.with_x().with_y().build()` | 读起来像句子;可中途暂停传递半成品 | 必填校验拖到运行时;有可变状态,跨线程要小心 |
| 阶段式构建器(Staged) | 每步返回不同类型 | 漏填必填、乱序都在编译期拦下 | 类型设计复杂;必填项一多阶段爆炸 |
| 组合式构建器(Composite) | 基础 Builder + 多个子构造器 | 职责分明,新增字段分组不改旧代码 | 设计成本高;API 学习成本略陡 |

记下这几条结论就行:**可选字段多、必填校验不严,选流式;必填项绝对不能漏、顺序有讲究,选阶段式;字段能按业务域分组、团队会持续加新字段,选组合式。** 多数项目里,流式构建器是性价比最高的默认选择,阶段式和组合式是为更苛刻的约束准备的升级路径。

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 巨型构造函数 | 一个构造函数塞下所有字段 | 字段一多不可读;构造逻辑耦合进业务类;失败只能抛异常或背 `is_valid` 标志 |
| getter/setter 化简 | 必填留构造函数,选填用 setter | `Task` 同时背业务职责和构造职责,类越来越脏 |
| 简单构建器 | 委托给 `TaskBuilder`,`std::optional` 当标志位 | 一行行 `b.set_xxx()` 太啰嗦,断成十行 |
| 流式构建器 | `with_*` 返回 `*this`,链式调用 | 必填校验只能拖到运行时;构建器有可变状态 |
| 阶段式构建器 | 每步返回不同类型,类型流钉死顺序 | 类型设计变复杂,必填项一多阶段爆炸 |
| 组合式构建器 | 基础 Builder + 子构造器,引用共享状态 | 设计成本高,但满足开闭原则,扩展性最好 |

记下这几条关键结论:

- **构建器模式的本质,是把「收集材料 / 校验 / 构造」这三步,从一个焊死的构造函数里拆出来**,交给一个专门的中间类,让 `Task` 只管自己的业务语义。
- **`std::optional` 是替代 `is_valid` 标志位的利器**——「字段填没填」直接内化进类型语义,类的状态干干净净。
- **`build()` 的 `return t;` 零拷贝**,C++17 起 *mandatory copy elision* 保证同名局部对象直接在调用方栈帧上构造,大对象也放心 return。
- **流式构建器的必填校验是运行时的**——类型系统分不清「填了几个字段」的构建器。要把它压到编译期,上阶段式构建器,让每个必填步骤返回不同的类型。
- **构建器是有状态的中间对象**,跨线程复用就是数据竞争。要么用完即弃,要么传值拷贝,要么加锁。

::: tip 配套可编译工程
本节的例子在仓库 `code/volumn_codes/vol4/design-patterns/Builder/` 下有完整可编译工程(`.h` + main + `CMakeLists.txt`),`cmake -S . -B build && cmake --build build` 即可跑出上面这些输出。
:::

## 参考资源

- [cppreference:`std::optional`](https://en.cppreference.com/w/cpp/utility/optional)(C++17 起,「可能没有值」的语义类型)
- [cppreference:Return value optimization / Copy elision](https://en.cppreference.com/w/cpp/language/copy_elision)(C++17 起 *mandatory copy elision*)
- Fedor G. Pikus,《Hands-On Design Patterns with C++》第 5 章(构建器与流式接口)
- 本卷姊妹篇:[单例模式:从注释约束到 Meyer's Singleton](./01-singleton.md)
