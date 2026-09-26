---
chapter: 1
cpp_standard:
- 17
- 20
description: "The eye of the series: wiring WeakPtr into the callback system. We take apart the compile-time wiring that lets BindOnce detect a WeakPtr (kIsWeakMethod) and the call-time dispatch (InvokeHelper<true>'s if (!target) return;), then tie it back to the 01-4 cancellation token"
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'WeakPtr hands-on (IV): sequence affinity and lazy binding'
- 'OnceCallback hands-on (IV): designing the cancellation token'
- 'OnceCallback hands-on (I): motivation and API design'
reading_time_minutes: 15
related:
- 'WeakPtr Hands-on (VI): Tests and Performance Comparison'
- 'WeakPtr hands-on (I): motivation and API design'
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- weak_ptr
- 回调机制
- 函数对象
title: 'WeakPtr hands-on (V): integrating with callbacks to close the OnceCallback loop'
translation:
  source: documents/vol9-open-source-project-learn/chrome/02_weak_ptr/full/02-5-weak-ptr-bind-integration.md
  source_hash: 6e4a0acad2c54fbf17acd4ecd925a87753c9ec4300256a128e64a6e00ec38ba0
  translated_at: '2026-09-26T01:46:34+00:00'
  engine: anthropic
  token_count: 7500
---
# WeakPtr hands-on (V): integrating with callbacks to close the OnceCallback loop

We have finally reached the eye of the series. Remember the dangling callback from the opening of [01-4, the cancellation token piece](../../01_once_callback/full/01-4-once-callback-cancellation-token.md)? The task is still waiting in the queue when the object destructs first, and the callback then runs and dereferences a hollow shell. Back then I took the easy way out — an atomic flag plus an if-check right before the call — and shoved the bug down. But a loose end was left flapping: how exactly does that flag reach the callback's hands, and who manages its life? I honestly had not thought it through at the time.

Now we have a complete `WeakPtr` in hand, and we can wire it into the callback system for real. This piece looks at how Chromium uses `BindOnce` to make a callback bound to a `WeakPtr` go mute **automatically** once the object dies, degrading into a no-op. You will find that the crude contraption I hand-rolled in 01-4 maps onto the industrial implementation almost line for line — it just wears two extra layers of engineering clothing: type erasure, and a small speculative scheme cooked up by the scheduler.

---

## The industrial answer: BindOnce + WeakPtr

The real idiom in Chromium is not `if (wp) wp->...`. You bind the `WeakPtr` straight into the callback:

```cpp
// This task silently drops itself after controller dies — no dangling dereference
thread_pool.post(
    base::BindOnce(&Controller::on_work_done,
                   controller.weak_factory_.GetWeakPtr()));
```

From the outside this is an ordinary `BindOnce` — a member method plus one argument (the WeakPtr), packed into a callable. But at **compile time** `BindOnce` has already sniffed out "the receiver is a WeakPtr", and it quietly picks a special dispatch path for the binding: before executing, null-check the weak pointer; if it is invalid, `return` and do nothing; only if it is still valid does the method actually get called.

That special path comes in two halves — compile-time wiring and call-time dispatch — and we will take them apart one at a time.

---

## Compile-time wiring: kIsWeakMethod / IsWeakReceiver

The type-erasure machinery of `BindOnce` (the `BindState` apparatus from [01-1](../../01_once_callback/full/01-1-once-callback-motivation-and-api-design.md)) has to make up its mind at compile time: "does this binding take the weak branch or not?" The verdict comes from a constant, `kIsWeakMethod` (`bind_internal.h:436-448`):

```cpp
template <bool is_method, typename... Args>
inline constexpr bool kIsWeakMethod = false;

template <typename T, typename... Args>
inline constexpr bool kIsWeakMethod<true, T, Args...> = IsWeakReceiver<T>::value;
```

Two conditions have to line up for it to be true. First, `is_method`: what is bound is a **member method**, not a free function. Second, `IsWeakReceiver<T>::value`: the receiver's type `T` is a `WeakPtr<?>`. The definition of `IsWeakReceiver` is perfectly blunt (`bind_internal.h:1925-1926`):

```cpp
template <typename T>
struct IsWeakReceiver : std::bool_constant<is_instantiation<T, WeakPtr>> {};
```

In plain terms: "is `T` some instantiation of the `WeakPtr` template?"

The trigger is deliberately narrow — it stopped me for a beat the first time I read it. It has to be "member method + first argument (the receiver) is a `WeakPtr`", nothing less. Stuff a `WeakPtr` in as an ordinary bound argument (not the receiver), or hand one to a free function, and the weak branch does **not** trigger. In that case the WeakPtr is just a value being copied around, with none of that automatic no-op treatment. The narrowness is sound: only when the WeakPtr is unmistakably "the method's receiver" does the sentence "if the object is dead, don't call" even make sense.

---

## Call-time dispatch: InvokeHelper\<true\>::MakeItSo

Once `kIsWeakMethod` is true at compile time, a specialized `InvokeHelper<true>` gets selected — it is the executor for weak calls. The core code is startlingly short (`bind_internal.h:939-961`), short enough that we can paste the whole thing:

```cpp
template <typename Traits, typename ReturnType,
          size_t index_target, size_t... index_tail>
struct InvokeHelper<true, Traits, ReturnType, index_target, index_tail...> {
    template <typename Functor, typename BoundArgsTuple, typename... RunArgs>
    static inline void MakeItSo(Functor&& functor, BoundArgsTuple&& bound,
                                RunArgs&&... args) {
        static_assert(index_target == 0);
        // Note: the validity of the weak pointer must be tested after Unwrap,
        // otherwise it creates a race for weak pointer implementations that
        // allow cross-thread usage and perform Lock() inside Unwrap().
        const auto& target = Unwrap(std::get<0>(bound));
        if (!target) {              // ← cancellation point: after the object dies, return here; the callback silently no-ops
            return;
        }
        Traits::Invoke(
            Unwrap(std::forward<Functor>(functor)), target,
            Unwrap(std::get<index_tail>(std::forward<BoundArgsTuple>(bound)))...,
            std::forward<RunArgs>(args)...);
    }
};
```

The whole secret of cancellation hides in that one line, `if (!target) return;`. `target` is the receiver unwrapped from `Unwrap(std::get<0>(bound))` — for a native `WeakPtr<T>`, `Unwrap` is a passthrough (the primary template), so `target`'s type stays `WeakPtr<T>`, untouched.

From there, `if (!target)` goes through `WeakPtr::operator bool` (`weak_ptr.h:255`), which calls `get()`, and inside `get()` the expression `ref_.IsValid() ? ptr_ : nullptr` (`weak_ptr.h:238`) is where the real decision gets made. Unrolled layer by layer so we can see it clearly:

```text
if (!target)
  → target.operator bool()
  → target.get()
  → target.ref_.IsValid()
  → flag_ && flag_->IsValid()         ← DCHECK same-sequence + acquire-load
```

So the cancellation check in a weak call bottoms out at exactly that same-sequence, 100%-accurate `IsValid` we covered in [02-4](./02-4-weak-ptr-sequence-affinity-and-lazy-binding.md). The moment the object dies, `get()` hands back `nullptr`, `operator bool` flips to false, and `MakeItSo` returns on the spot — the callback silently no-ops, and nobody so much as lays a finger on the dangling pointer.

---

## The key: the check goes through IsValid, not MaybeValid

Here is a point that is remarkably easy to get wrong — and one that plenty of secondhand material gets wrong anyway: the cancellation check in a weak call goes through `IsValid` (same-sequence, accurate), not `MaybeValid`. We drew the boundary between the two back in [02-4](./02-4-weak-ptr-sequence-affinity-and-lazy-binding.md) — `IsValid` is the hard gate before a deref; `MaybeValid` is merely an optimistic hint for crossing sequences. The `!target` expression in `MakeItSo`, flowing through `operator bool` and `get()`, lands on `IsValid`: a deterministic judgment made on the bound sequence, one that can guarantee "liveness check passed ⇒ the object is genuinely still breathing at this instant, safe to call".

So where does `MaybeValid` end up? Inside a weak call it travels a **separate, independent channel** and never touches the no-op decision at all. Chromium's `CallbackCancellationTraits` keeps a dedicated specialization for weak receivers (`bind_internal.h:1985-2006`) that splits "cancellation query" clean in two:

```cpp
template <typename Functor, typename... BoundArgs>
    requires internal::kIsWeakMethod<...>
struct CallbackCancellationTraits<Functor, std::tuple<BoundArgs...>> {
    static constexpr bool is_cancellable = true;

    template <typename Receiver, typename... Args>
    static bool IsCancelled(const Functor&, const Receiver& receiver, const Args&...) {
        return !receiver;                    // same-sequence, via IsValid, accurate
    }

    template <typename Receiver, typename... Args>
    static bool MaybeValid(const Functor&, const Receiver& receiver, const Args&...) {
        return MaybeValidTraits<Receiver>::MaybeValid(receiver);  // cross-sequence, via WeakPtr::MaybeValid
    }
};
```

The two channels each serve their own master. `IsCancelled(!receiver)` backs `Callback::IsCancelled()`: queried on the bound sequence, its verdict is set in stone. `MaybeValid(receiver.MaybeValid())` backs `Callback::MaybeValid()`: any sequence may ask, at the price of treating the answer as an optimistic estimate only.

The `MaybeValid` line truly serves the **scheduler / message loop**: before a task is dispatched, the scheduler is perfectly free to speculatively cup an ear to `MaybeValid` from any sequence. If it comes back false, the scheduler knows the score — this callback is dead weight for sure — so it skips it outright and pockets the saved cost of one cross-sequence post. But at the instant the callback **actually executes**, the cancellation verdict runs through the `!target` line in `MakeItSo` (that is, `IsValid`) — that line is the hard gate, and it is accurate.

One sentence to pin it down: cancellation has two paths. Execution time goes through `IsValid` (accurate); the scheduler's speculation goes through `MaybeValid` (optimistic); and the execution-time path never once touches `MaybeValid`'s edge. Draw this boundary cleanly, and you are already more accurate than the vast majority of secondhand write-ups on the market.

---

## Weak calls are forced to return void

The weak branch hides one more small constraint, tucked into `MakeItSo`'s return type: it returns `void`. That is no accident — it is nailed down by the `WeakCallReturnsVoid` `static_assert` (`bind_internal.h:1028-1040`):

```cpp
if constexpr (WeakCallReturnsVoid<kIsWeakCall>::value) {
    // take the InvokeHelper<kIsWeakCall>::MakeItSo path
}
```

The reasoning clicks into place once you think about it: once a callback is cancelled, what executes is `return;` (no value) — but what if the method itself returns a value? At the instant of cancellation, what do you hand back? So a weak call **must return `void`**. Write `BindOnce(&Foo::get_value, weak_ptr)` (with `get_value` returning `int`), and compile time refuses you outright. This is a textbook case of strangling ambiguity at the type level: cancellation semantics demand void, so the signature forces void, and nobody gets to be vague about it.

---

## The race defense behind "Unwrap first, then check liveness"

Look back at that `MakeItSo` snippet: one comment there deserves to be pulled out on its own (`bind_internal.h:949-951`):

> Note the validity of the weak pointer should be tested _after_ it is unwrapped, otherwise it creates a race for weak pointer implementations that allow cross-thread usage and perform `Lock()` in `Unwrap()` traits.

The meaning: the step `target = Unwrap(...)` must sit **before** `if (!target)`, no exceptions. Why? Some weak-pointer variants do allow cross-thread use, and their `Unwrap()` performs a `Lock()` inside (Chromium has weak-pointer implementations internally that are fancier than `WeakPtr`). For one of those, if you test the bool first and Unwrap second, the two steps crack open a window of race — the object is alive at the liveness check, yet may already be gone by the time Unwrap runs. Unwrap first (pulling the real pointer out securely), then check liveness, and the window seals shut.

Our own `WeakPtr`'s `Unwrap` is a passthrough that never Locks, so for it the order of the two steps is immaterial. But `MakeItSo` is a general-purpose template and has to accommodate those more general weak-pointer implementations, which is why the comment deliberately writes this race defense into the contract. Chromium's `bind_unittest.cc` even keeps a dedicated `MockRacyWeakPtr` on hand (its `operator bool()` always returns true, its `Lock()` always returns nullptr) precisely to exercise this "Unwrap first, then check liveness" path.

---

## vs Unretained(this): a safe no-op after the fact vs a UAF alarm after the fact

Since we are here, let's grab another perpetually-conflated idiom and hold it up next to this one: `base::Unretained(this)`. It also binds a member method into a callback, but it walks the **exactly opposite** path — `InvokeHelper<false>`, with no liveness check whatsoever:

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
        // no if (!target) return; -- Run() after the object dies is a dangling dereference
    }
};
```

An `Unretained` receiver gets unwrapped to a raw `T*`, and the liveness-check layer is simply omitted. Run the callback after the object dies and you have a UAF; its last remaining line of defense is the PartitionAlloc backup-ref from Chromium's `raw_ptr` memory-safety hardening — but that thing reports an error after the fact, it does not steer you away beforehand.

The fundamental difference, in one sentence:

> **A `WeakPtr` receiver = the callback silently no-ops after the object dies (safe up front); an `Unretained` receiver = UAF after the object dies (an alarm after the fact, or UB).**

In production code, if there is even a sliver of a chance that "the object might destruct before the callback executes", reach for `WeakPtr`, full stop. Only when you can **statically swear** that the object absolutely outlives the callback (say, the callback runs synchronously, entirely within the object's scope) does `Unretained` get its turn on stage.

---

## A teaching implementation: bolting WeakPtr onto the 01 OnceCallback

Let's boil the industrial machinery down to a concentrate, shape it into a teaching version, and bolt it onto the `OnceCallback` implemented in the [01 series](../../01_once_callback/full/01-1-once-callback-motivation-and-api-design.md). At its core there is nothing more than translating that one `MakeItSo` line:

```cpp
// Platform: host | C++ Standard: C++20
// Simplified: bind a member method + WeakPtr<T> into a void() callback
// (Here the 01 series' OnceCallback serves as the return type; the accompanying
//  standalone-compilable 18_bind_weakptr_cancel.cpp substitutes std::function,
//  same logic.)
template <typename T, typename... Bound>
auto bind_weak_once(void (T::*method)(Bound...),
                    WeakPtr<T> receiver,
                    Bound... bound_args) {
    return OnceCallback<void()>(
        [method, receiver = std::move(receiver),
         bound = std::make_tuple(std::move(bound_args)...)]() mutable {
            if (!receiver) return;     // ← corresponds to the cancellation point in InvokeHelper<true>::MakeItSo
            std::apply(
                [&](auto&&... args) { (receiver.get()->*method)(args...); },
                bound);
        });
}
```

This looks shabby, but it is isomorphic to the industrial `MakeItSo`: `if (!receiver) return;` is the counterpart of `if (!target) return;`. `receiver` is a `WeakPtr<T>`, and `!receiver` flows through `operator bool` and `get()` down to `IsValid` — the moment the object dies, it silently no-ops. Let's hang it on the 01 OnceCallback and give it a run:

```cpp
class Controller {
public:
    void on_work_done(int v) { std::cout << "got " << v << '\n'; }
    WeakPtr<Controller> get_weak() { return weak_factory_.get_weak_ptr(); }
private:
    std::vector<int> buf_;
    WeakPtrFactory<Controller> weak_factory_{this};   // last member
};

int main() {
    WeakPtr<Controller> alive_marker;
    {
        Controller c;
        // Bind a callback whose receiver is c's WeakPtr
        auto task = bind_weak_once(&Controller::on_work_done, c.get_weak(), 42);

        // Run while c is still alive → calls on_work_done
        std::move(task).run();                  // got 42
    }   // c destructs → weak_factory_ invalidates all WeakPtrs first → only then does buf_ destruct

    // Now bind a fresh one and run it after c is dead
    auto task2 = [&] {
        // Pretend we obtained an already-invalidated WeakPtr from elsewhere
        // (simulating the object destructing while the task still sits in the queue)
        return bind_weak_once(&Controller::on_work_done,
                              WeakPtr<Controller>{}, 99);   // empty WeakPtr
    }();
    std::move(task2).run();                     // silent no-op, prints nothing
    return 0;
}
```

You will see `got 42` printed once; the second task, its receiver invalid, silently no-ops and prints nothing at all. The dangling-callback bug from 01-4 finally has a thorough antidote here — and on the user's side, all it took was **one extra `get_weak_ptr()` call**. The entire tangle of cancellation complexity is carried off for you by `BindOnce` + `WeakPtr`.

---

## Closing the loop: the 01-4 hand-rolled token vs industrial WeakPtr

The series has come full circle here. Let's put the 01-4 hand-rolled cancellation token and the industrial WeakPtr on the same table, matched one to one:

| 01-4 hand-rolled scheme | Industrial WeakPtr |
|---|---|
| One atomic flag (sharing across multiple callbacks means manually copying the token) | `WeakReference::Flag` (`RefCountedThreadSafe` + `AtomicFlag`); the factory and all WeakPtrs **automatically share the very same one** |
| Manually manage the flag's lifetime | `scoped_refptr<Flag>` refcounting manages it automatically |
| Pre-call `if (!flag.is_set()) return;` | `if (!target) return;` inside `InvokeHelper<true>::MakeItSo` |
| Manually stuff the flag into the callback | `kIsWeakMethod` / `IsWeakReceiver` **automatically** recognize a WeakPtr receiver at compile time and pick the weak branch |
| A single check channel | Split into two: `IsCancelled` (same-sequence, accurate, used at execution time) and `MaybeValid` (cross-sequence hint, used for scheduler speculation) |
| Post-cancellation callback behavior: whatever you define | After cancellation, **forced silent no-op**; and weak calls are **forced to return void** (no value to hand back at the moment of cancellation) |

As for the most crucial advances, I would say there are exactly two. The first: the factory is bound to the object's identity, and all WeakPtrs automatically share the same Flag — "one invalidate, every callback drops dead together" comes for free (in the 01-4 hand-rolled version the user still had to copy the token around to share it across multiple callbacks, and the flag was a freestanding little object with no tie to the object's identity). The second: the wiring is done automatically at compile time (`kIsWeakMethod`): you just write `BindOnce(&C::m, weak_factory_.GetWeakPtr())`, the cancellation machinery seats itself, and no hand-written if-check is needed.

That "loose end" I left in 01-4 — how the flag travels, and who manages its life — is sealed up completely here: the flag is that shared Flag; its life is managed by intrusive reference counting; it travels by WeakPtr handle; and it plugs into callbacks via compile-time wiring. Six prerequisite pieces plus five hands-on pieces, and we have unscrewed every last bolt of the Chromium engineers' design and had a good look at it.

---

## References

- [Chromium `base/functional/bind_internal.h`: kIsWeakMethod / InvokeHelper / WeakCallReturnsVoid](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/bind_internal.h)
- [Chromium `base/functional/callback.h`: IsCancelled/MaybeValid](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/callback.h)
- [OnceCallback hands-on (IV): designing the cancellation token](../../01_once_callback/full/01-4-once-callback-cancellation-token.md)
- [WeakPtr hands-on (IV): sequence affinity and lazy binding](./02-4-weak-ptr-sequence-affinity-and-lazy-binding.md)
