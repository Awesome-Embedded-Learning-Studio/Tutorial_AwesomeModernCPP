---
chapter: 1
cpp_standard:
- 17
- 20
description: 系列之眼——把 WeakPtr 接进回调系统。拆解 BindOnce 检测 WeakPtr 的编译期接线(kIsWeakMethod)
  与调用期分派(InvokeHelper<true> 的 if(!target) return;),并回扣 01-4 的取消令牌
difficulty: intermediate
order: 5
platform: host
prerequisites:
- WeakPtr 实战（四）：序列亲和性与 lazy 绑定
- OnceCallback 实战（四）：取消令牌设计
- OnceCallback 实战（一）：动机与接口设计
reading_time_minutes: 15
related:
- WeakPtr 实战（六）：测试与性能对比
- WeakPtr 实战（一）：动机与接口设计
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- weak_ptr
- 回调机制
- 函数对象
title: "WeakPtr 实战（五）：与回调集成——关闭 OnceCallback 的环"
---
# WeakPtr 实战（五）：与回调集成——关闭 OnceCallback 的环

咱们总算走到整个系列的"眼"了。还记得 [01-4 取消令牌设计](../../01_once_callback/full/01-4-once-callback-cancellation-token.md) 开篇那个悬空回调吗——任务还在队列里排着,对象先一步析构了,回调跑起来解引用一个空壳。当时笔者图省事,甩了个原子标志加上调用前的 if-check 把它压下去了,可留了个尾巴:那个标志到底怎么递到回调手里、它的命谁来管,笔者那会儿其实没认真想。

现在手头有了完整的 `WeakPtr`,可以把它正经接进回调系统了。咱们这一篇就看 Chromium 怎么用 `BindOnce`,让一个绑了 `WeakPtr` 的回调在对象死后**自动**哑掉,变成 no-op。您会发现 01-4 那个笔者手搓的土办法,在工业级实现里几乎是逐行对应——只是多裹了两层工程外衣:类型擦除,以及调度器那点投机的小心思。

---

## 工业级答案:BindOnce + WeakPtr

Chromium 里真正的写法,不是 `if (wp) wp->...`,而是把 `WeakPtr` 直接绑进回调:

```cpp
// 这条任务在 controller 死后会自动静默丢弃,不会悬空解引用
thread_pool.post(
    base::BindOnce(&Controller::on_work_done,
                   controller.weak_factory_.GetWeakPtr()));
```

从外面看,这就是一次普通的 `BindOnce`——成员方法搭一个参数(WeakPtr),打包成可调用对象。可 `BindOnce` 在**编译期**就嗅出了"receiver 是个 WeakPtr",于是悄悄给它挑了一条特殊的分派路径:执行前先 null-check 弱指针,失效就 `return`,什么都不干;没失效才真正调方法。

这条"特殊路径"分两段——编译期接线和调用期分派——咱们各拆一段。

---

## 编译期接线:kIsWeakMethod / IsWeakReceiver

`BindOnce` 的类型擦除机制([01-1 讲过](../../01_once_callback/full/01-1-once-callback-motivation-and-api-design.md)的 `BindState` 那套)在编译期就得拿定主意:"这个绑定要不要走 weak 分支?" 判定靠一个常量 `kIsWeakMethod`(`bind_internal.h:436-448`):

```cpp
template <bool is_method, typename... Args>
inline constexpr bool kIsWeakMethod = false;

template <typename T, typename... Args>
inline constexpr bool kIsWeakMethod<true, T, Args...> = IsWeakReceiver<T>::value;
```

它有两个条件,凑齐了才为真:① `is_method`——绑定的是**成员方法**,不是自由函数;② `IsWeakReceiver<T>::value`——receiver 的类型 `T` 是个 `WeakPtr<?>`。`IsWeakReceiver` 的定义直白得很(`bind_internal.h:1925-1926`):

```cpp
template <typename T>
struct IsWeakReceiver : std::bool_constant<is_instantiation<T, WeakPtr>> {};
```

说白了就是"`T` 是不是 `WeakPtr` 模板的某个实例化"。

触发条件故意收得很窄,这一点笔者第一次读时还愣了一下:非得是"成员方法 + 第一参数(receiver)是 `WeakPtr`"才行。您要是把 `WeakPtr` 当个普通绑定参数(不是 receiver)塞进去,或者递给一个自由函数,**不会**触发 weak 分支——那时候 WeakPtr 就是个被拷来拷去的值,没那个自动 no-op 的待遇。这条件窄得合理:只有当 WeakPtr 摆明了是"方法的接收者","对象死了就别调"这句话才说得通。

---

## 调用期分派:InvokeHelper\<true\>::MakeItSo

`kIsWeakMethod` 一旦在编译期为真,就会选中一个特化版的 `InvokeHelper<true>`——它就是 weak 调用的执行器。核心代码短得吓人(`bind_internal.h:939-961`),咱们整段贴出来:

```cpp
template <typename Traits, typename ReturnType,
          size_t index_target, size_t... index_tail>
struct InvokeHelper<true, Traits, ReturnType, index_target, index_tail...> {
    template <typename Functor, typename BoundArgsTuple, typename... RunArgs>
    static inline void MakeItSo(Functor&& functor, BoundArgsTuple&& bound,
                                RunArgs&&... args) {
        static_assert(index_target == 0);
        // 注意:weak pointer 的有效性必须在 Unwrap 之后再测,
        // 否则对允许跨线程、在 Unwrap() 里执行 Lock() 的弱指针实现会造成 race。
        const auto& target = Unwrap(std::get<0>(bound));
        if (!target) {              // ← 取消点:对象死后这里 return,回调静默 no-op
            return;
        }
        Traits::Invoke(
            Unwrap(std::forward<Functor>(functor)), target,
            Unwrap(std::get<index_tail>(std::forward<BoundArgsTuple>(bound)))...,
            std::forward<RunArgs>(args)...);
    }
};
```

取消的全部秘密,就藏在 `if (!target) return;` 那一行。`target` 是 `Unwrap(std::get<0>(bound))` 解出来的 receiver——对原生 `WeakPtr<T>` 来说,`Unwrap` 是透传(主模板),所以 `target` 的类型原封不动就是 `WeakPtr<T>`。

接下来 `if (!target)` 走的是 `WeakPtr::operator bool`(`weak_ptr.h:255`),它调 `get()`,而 `get()` 内部那句 `ref_.IsValid() ? ptr_ : nullptr`(`weak_ptr.h:238`)才是真正拍板的地方。一层层展开看清楚:

```text
if (!target)
  → target.operator bool()
  → target.get()
  → target.ref_.IsValid()
  → flag_ && flag_->IsValid()         ← DCHECK 同序列 + acquire-load
```

所以 weak 调用的取消检查,走到底就是咱们 [02-4](./02-4-weak-ptr-sequence-affinity-and-lazy-binding.md) 讲过的那个同序列、100% 准确的 `IsValid`。对象一死,`get()` 交还 `nullptr`,`operator bool` 翻成 false,`MakeItSo` 当场 `return`——回调静默 no-op,那个悬垂指针连碰都没人碰一下。

---

## 关键:检查走 IsValid,不是 MaybeValid

这里有个极易写错、而且不少二手资料偏偏就写错的点:weak 调用的取消检查走的是 `IsValid`(同序列、准确),不是 `MaybeValid`。咱们 [02-4](./02-4-weak-ptr-sequence-affinity-and-lazy-binding.md) 已经把这两个分过界了——`IsValid` 是 deref 前那道硬门,`MaybeValid` 不过是跨序列时一个乐观的 hint。`MakeItSo` 里那句 `!target`,经 `operator bool`、`get()` 一路落到 `IsValid`,是同序列上的确定性判断,能担保"判活通过 ⇒ 此刻对象真的还喘着气,可以调"。

那 `MaybeValid` 干嘛去了?它在 weak 调用里走的是**另一条独立的通道**,压根不插手 no-op 的判定。Chromium 的 `CallbackCancellationTraits` 专门给 weak receiver 留了一份特化(`bind_internal.h:1985-2006`),把"取消查询"一劈两半:

```cpp
template <typename Functor, typename... BoundArgs>
    requires internal::kIsWeakMethod<...>
struct CallbackCancellationTraits<Functor, std::tuple<BoundArgs...>> {
    static constexpr bool is_cancellable = true;

    template <typename Receiver, typename... Args>
    static bool IsCancelled(const Functor&, const Receiver& receiver, const Args&...) {
        return !receiver;                    // 同序列、走 IsValid,准确
    }

    template <typename Receiver, typename... Args>
    static bool MaybeValid(const Functor&, const Receiver& receiver, const Args&...) {
        return MaybeValidTraits<Receiver>::MaybeValid(receiver);  // 跨序列、走 WeakPtr::MaybeValid
    }
};
```

这两路各伺候各的主子。`IsCancelled(!receiver)` 是给 `Callback::IsCancelled()` 用的,在绑定序列上查,结果板上钉钉;`MaybeValid(receiver.MaybeValid())` 则是给 `Callback::MaybeValid()` 用的,任意序列都能查,代价是结果只能当乐观估计看。

`MaybeValid` 这条线真正伺候的是**调度器/message loop**:任务派发之前,调度器大可以从任意序列投机地探一耳朵 `MaybeValid`,要是返回 false,它心里就有数了——这回调铁定没意义,直接跳过,顺手省一次跨序列投递的开销。但等到回调**真正执行**那一刻,取消判定走的是 `MakeItSo` 里那行 `!target`(也就是 `IsValid`)——这条线是硬门,准。

一句话收住:取消判定有两条路,执行期走 `IsValid`(准),调度器投机走 `MaybeValid`(乐观),执行期那条永远不沾 `MaybeValid` 的边。您把这条界线划清,就比市面上绝大多数二手资料都准了。

---

## 弱调用强制 void 返回

weak 分支还藏着一个小约束,在 `MakeItSo` 的返回类型里:它返的是 `void`。这不是凑巧,是 `WeakCallReturnsVoid` 这个 `static_assert` 拍下来的(`bind_internal.h:1028-1040`):

```cpp
if constexpr (WeakCallReturnsVoid<kIsWeakCall>::value) {
    // 走 InvokeHelper<kIsWeakCall>::MakeItSo
}
```

道理想想就通:回调一旦被取消,执行的是 `return;`(没值),可方法本身要是带返回值呢?取消那一刻您拿什么返?所以 weak 调用**必须返回 `void`**。您写一句 `BindOnce(&Foo::get_value, weak_ptr)`(`get_value` 返 int),编译期直接给您拒了。这是典型的"把歧义在类型层面就堵死"——取消语义要求 void,那签名就强制 void,谁也别含糊。

---

## "先 Unwrap 再判活"的 race 防御

回头再看 `MakeItSo` 那段,有处注释值得单独拎出来(`bind_internal.h:949-951`):

> Note the validity of the weak pointer should be tested _after_ it is unwrapped, otherwise it creates a race for weak pointer implementations that allow cross-thread usage and perform `Lock()` in `Unwrap()` traits.

意思是:`target = Unwrap(...)` 这步,非得排在 `if (!target)` **前面**不可。为啥?有些弱指针变体是允许跨线程用的,而且 `Unwrap()` 里头会 `Lock()`(Chromium 内部就有比 `WeakPtr` 更花哨的弱指针实现)。对这种实现,您要是先判 bool、再 Unwrap,两步之间就开了一道 race 的缝——判活那一下对象还在,等到 Unwrap 的时候它可能已经没了。先 Unwrap(把真指针稳稳取出来)、再判活,这道缝就合上了。

咱们这位 `WeakPtr` 自己的 `Unwrap` 是透传、不 Lock,所以对它来说这两步谁先谁后都无所谓;但 `MakeItSo` 是个通用模板,得照顾那些更一般的弱指针实现,所以注释里特意把这条 race 防御写进了契约。Chromium 的 `bind_unittest.cc` 里还专门养了个 `MockRacyWeakPtr`(`operator bool()` 恒 true、`Lock()` 恒 nullptr),就是为了验这条"先 Unwrap 再判活"的路径。

---

## vs Unretained(this):事后安全 no-op vs 事后报警 UAF

讲到这儿,顺手拎出另一个老被搞混的写法比一比:`base::Unretained(this)`。它也把成员方法绑成回调,可走的却是**完全相反**的路径——`InvokeHelper<false>`,压根不判活:

```cpp
template <typename Traits, typename ReturnType, size_t... indices>
struct InvokeHelper<false, Traits, ReturnType, indices...> {
    template <typename Functor, typename BoundArgsTuple, typename... RunArgs>
    static inline ReturnType MakeItSo(Functor&& functor, BoundArgsTuple&& bound,
                                      RunArgs&&... args) {
        return Traits::Invoke(
            Unwrap(std::forward<Functor>(functor)),
            Unwrap(std::get<indices>(std::forward<BoundArgsTuple>(bound)))...,
            std::forward<RunArgs>(args)...);
        // 没有 if (!target) return; —— 对象死了还 Run() 就是悬垂解引用
    }
};
```

`Unretained` 的 receiver 被解包成裸 `T*`,判活那层直接省了。对象死了还跑回调,就是 UAF;它仅剩的防线,是 Chromium 内存安全加固那套 `raw_ptr` 的 PartitionAlloc backup-ref——可那玩意儿是事后报错,不是事前躲开。

根本差别,一句话:

> **`WeakPtr` receiver = 对象死后回调静默 no-op(事前安全);`Unretained` receiver = 对象死后 UAF(事后报警或 UB)。**

生产代码里,但凡有一丝"对象可能在回调执行前析构"的可能,一律上 `WeakPtr`。只有您**能静态拍胸脯**保证对象绝对活得过回调(比如回调是同步执行、整个在对象作用域里头),才轮得到 `Unretained` 上场。

---

## 简版实现:在 01 的 OnceCallback 上接 WeakPtr

咱们把工业那套机制熬浓一下,做个教学版,接到 [01 系列](../../01_once_callback/full/01-1-once-callback-motivation-and-api-design.md) 里实现的 `OnceCallback` 上。核心没别的,就是把 `MakeItSo` 那行翻译过来:

```cpp
// Platform: host | C++ Standard: C++20
// 简化:把成员方法 + WeakPtr<T> 绑成一个 void() 回调
// (这里用 01 系列的 OnceCallback 作返回类型;配套可独立编译的 18_bind_weakptr_cancel.cpp
//  用 std::function 代替,逻辑一致)
template <typename T, typename... Bound>
auto bind_weak_once(void (T::*method)(Bound...),
                    WeakPtr<T> receiver,
                    Bound... bound_args) {
    return OnceCallback<void()>(
        [method, receiver = std::move(receiver),
         bound = std::make_tuple(std::move(bound_args)...)]() mutable {
            if (!receiver) return;     // ← 对应 InvokeHelper<true>::MakeItSo 的取消点
            std::apply(
                [&](auto&&... args) { (receiver.get()->*method)(args...); },
                bound);
        });
}
```

这段看着寒酸,可跟工业级 `MakeItSo` 是同构的:`if (!receiver) return;` 对应的就是 `if (!target) return;`。`receiver` 是个 `WeakPtr<T>`,`!receiver` 经 `operator bool`、`get()` 一路落到 `IsValid`,对象一死就静默 no-op。咱们把它挂到 01 的 OnceCallback 上跑跑看:

```cpp
class Controller {
public:
    void on_work_done(int v) { std::cout << "got " << v << '\n'; }
    WeakPtr<Controller> get_weak() { return weak_factory_.get_weak_ptr(); }
private:
    std::vector<int> buf_;
    WeakPtrFactory<Controller> weak_factory_{this};   // 最后成员
};

int main() {
    WeakPtr<Controller> alive_marker;
    {
        Controller c;
        // 绑一条回调,receiver 是 c 的 WeakPtr
        auto task = bind_weak_once(&Controller::on_work_done, c.get_weak(), 42);

        // 在 c 还活着时跑 → 调用 on_work_done
        std::move(task).run();                  // got 42
    }   // c 析构 → weak_factory_ 先失效所有 WeakPtr → buf_ 才析构

    // 现在重新绑一条,在 c 死后跑
    auto task2 = [&] {
        // 这里假设从别处拿到一个已失效的 WeakPtr(模拟任务还在队列时对象析构)
        return bind_weak_once(&Controller::on_work_done,
                              WeakPtr<Controller>{}, 99);   // 空 WeakPtr
    }();
    std::move(task2).run();                     // 静默 no-op,什么都不打印
    return 0;
}
```

您会看到 `got 42` 打出来一次,第二个 task 因为 receiver 失效,静默 no-op,啥也不印。01-4 那个悬空回调的 bug,到这儿算是有了彻底的解药——而且**用户这边就多写了一个 `get_weak_ptr()`**,取消的一整套复杂度,全让 `BindOnce` + `WeakPtr` 替您扛走了。

---

## 闭环:01-4 手搓令牌 vs 工业 WeakPtr

系列走到这儿,算是闭环了。咱们把 01-4 手搓的取消令牌,跟工业级 WeakPtr 摆一张桌上,一比一对照:

| 01-4 手搓方案 | 工业级 WeakPtr |
|---|---|
| 一个原子标志(多个回调要手动拷 token 才能共享) | `WeakReference::Flag`(`RefCountedThreadSafe` + `AtomicFlag`),factory 与所有 WeakPtr **自动共享同一枚** |
| 手动管理标志的生命周期 | `scoped_refptr<Flag>` 引用计数自动管 |
| 调用前 `if (!flag.is_set()) return;` | `InvokeHelper<true>::MakeItSo` 里 `if (!target) return;` |
| 手动把标志塞进回调 | `kIsWeakMethod`/`IsWeakReceiver` **编译期自动**识别 WeakPtr receiver 并选中 weak 分支 |
| 单一检查通道 | 拆成 `IsCancelled`(同序列准,执行期用)和 `MaybeValid`(跨序列 hint,调度器投机用)两条 |
| 取消后回调行为:自定义 | 取消后**强制静默 no-op**;且 weak 调用**强制 void 返回**(取消时无值可返) |

要论最要紧的进化,笔者觉得就两条。头一条,factory 跟对象身份绑在一起,所有 WeakPtr 自动共享同一枚 Flag——"一次 invalidate、所有回调集体失效"这就白捡了(01-4 手搓版还得用户自己拷 token 才能让多个回调共享,而且 flag 是个独立小对象,跟对象身份不挂钩)。另一条,编译期就自动把线接好(`kIsWeakMethod`):您只管写 `BindOnce(&C::m, weak_factory_.GetWeakPtr())`,取消机制自己就位,不用您再手写一个 if-check。

01-4 那个笔者留着的"尾巴"——标志怎么传、谁来管它的命——到这儿彻底收口了:标志就是那枚共享的 Flag,命用侵入式引用计数管着,传递靠 WeakPtr 句柄,接进回调靠编译期接线。六篇前置知识,加五篇实战,Chromium 工程师这套设计,咱们算是把每一颗螺丝都拧下来看过了。

---

## 参考资源

- [Chromium `base/functional/bind_internal.h` —— kIsWeakMethod / InvokeHelper / WeakCallReturnsVoid](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/bind_internal.h)
- [Chromium `base/functional/callback.h` —— IsCancelled/MaybeValid](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/callback.h)
- [OnceCallback 实战（四）：取消令牌设计](../../01_once_callback/full/01-4-once-callback-cancellation-token.md)
- [WeakPtr 实战（四）：序列亲和性与 lazy 绑定](./02-4-weak-ptr-sequence-affinity-and-lazy-binding.md)
