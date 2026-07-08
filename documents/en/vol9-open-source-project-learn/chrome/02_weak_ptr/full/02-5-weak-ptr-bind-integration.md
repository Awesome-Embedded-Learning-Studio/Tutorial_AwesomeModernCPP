---
chapter: 1
cpp_standard:
- 17
- 20
description: "The eye of the series: wiring WeakPtr into the callback system. We unpack the compile-time wiring that lets BindOnce spot a WeakPtr (kIsWeakMethod) and the call-time dispatch in InvokeHelper<true> that does if(!target) return;, then close the loop on the 01-4 cancellation token."
difficulty: intermediate
order: 5
platform: host
prerequisites:
- 'WeakPtr hands-on (IV): sequence affinity and lazy binding'
- 'OnceCallback hands-on (IV): the cancellation token'
- 'OnceCallback hands-on (I): motivation and API design'
reading_time_minutes: 15
related:
- 'WeakPtr hands-on (VI): tests and performance comparison'
- 'WeakPtr hands-on (I): motivation and API design'
tags:
- host
- cpp-modern
- intermediate
- 智能指针
- weak_ptr
- 回调机制
- 函数对象
title: "WeakPtr Hands-on (V): Callback Integration, Closing the OnceCallback Loop"
---
# WeakPtr Hands-on (V): Callback Integration, Closing the OnceCallback Loop

We've finally reached the eye of the series. Cast your mind back to the opening of [01-4 cancellation token](../../01_once_callback/full/01-4-once-callback-cancellation-token.md) and that dangling callback: the task is still sitting in the queue when the object destructs first, and the callback runs and dereferences a hollow shell. Back then we took the shortcut, slapped an atomic flag plus a pre-call if-check on it, and moved on. But we left a loose end. How does that flag actually reach the callback, and who manages its lifetime? We didn't really think it through.

Now we have a complete `WeakPtr` in hand, and we can wire it into the callback system properly. This piece walks through how Chromium uses `BindOnce` to make a callback bound to a `WeakPtr` go silent **automatically** once the object dies, turning into a no-op. You'll see that the hand-rolled hack from 01-4 maps almost line-for-line onto the industrial implementation. The only difference is two extra layers of engineering finish: type erasure, and a small speculative bet the scheduler makes.

---

## The industrial answer: BindOnce + WeakPtr

In Chromium the real idiom isn't `if (wp) wp->...`. You bind the `WeakPtr` straight into the callback:

```cpp
// This task silently drops itself after controller dies; no dangling deref.
thread_pool.post(
    base::BindOnce(&Controller::on_work_done,
                   controller.weak_factory_.GetWeakPtr()));
```

From the outside it looks like an ordinary `BindOnce`: a member method plus one argument (the WeakPtr), packed into a callable. But `BindOnce` sniffs out "the receiver is a WeakPtr" at **compile time** and quietly routes it down a special dispatch path. Before running, it null-checks the weak pointer; if it's invalid, `return`, do nothing. Only if it's still alive does it actually call the method.

That special path has two halves, compile-time wiring and call-time dispatch. We'll take them apart one at a time.

---

## Compile-time wiring: kIsWeakMethod / IsWeakReceiver

`BindOnce`'s type-erasure machinery (the `BindState` apparatus from [01-1](../../01_once_callback/full/01-1-once-callback-motivation-and-api-design.md)) has to make up its mind at compile time: does this binding take the weak branch or not? The verdict is a constant called `kIsWeakMethod` (`bind_internal.h:436-448`):

```cpp
template <bool is_method, typename... Args>
inline constexpr bool kIsWeakMethod = false;

template <typename T, typename... Args>
inline constexpr bool kIsWeakMethod<true, T, Args...> = IsWeakReceiver<T>::value;
```

Two conditions must line up for it to be true. First, `is_method`: the thing being bound is a **member method**, not a free function. Second, `IsWeakReceiver<T>::value`: the receiver's type `T` is a `WeakPtr<?>`. The definition of `IsWeakReceiver` is blunt (`bind_internal.h:1925-1926`):

```cpp
template <typename T>
struct IsWeakReceiver : std::bool_constant<is_instantiation<T, WeakPtr>> {};
```

In plain terms: is `T` some instantiation of the `WeakPtr` template?

The trigger is deliberately narrow, and that surprised me the first time I read it. It has to be a member method whose first argument (the receiver) is a `WeakPtr`. If you shove a `WeakPtr` in as a plain bound argument that isn't the receiver, or hand one to a free function, it does **not** trigger the weak branch. In that case the WeakPtr is just a value getting copied around, with no automatic no-op treatment. The narrowness is reasonable. Only when the WeakPtr is plainly "the method's receiver" does the sentence "if the object is dead, don't call" even make sense.

---

## Call-time dispatch: InvokeHelper\<true\>::MakeItSo

Once `kIsWeakMethod` evaluates true at compile time, it selects a specialized `InvokeHelper<true>`, the executor for weak calls. The core code is startlingly short (`bind_internal.h:939-961`), short enough to quote in full:

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
        if (!target) {              // <- cancellation point: object dead, return, callback silently no-ops
            return;
        }
        Traits::Invoke(
            Unwrap(std::forward<Functor>(functor)), target,
            Unwrap(std::get<index_tail>(std::forward<BoundArgsTuple>(bound)))...,
            std::forward<RunArgs>(args)...);
    }
};
```

The whole secret of cancellation is that one line, `if (!target) return;`. `target` is the receiver unwrapped from `Unwrap(std::get<0>(bound))`. For a native `WeakPtr<T>`, `Unwrap` is a passthrough (the primary template), so `target` keeps its type as `WeakPtr<T>` unchanged.

Then `if (!target)` goes through `WeakPtr::operator bool` (`weak_ptr.h:255`), which calls `get()`, and inside `get()` the expression `ref_.IsValid() ? ptr_ : nullptr` (`weak_ptr.h:238`) is what actually decides. Unrolled so you can see every link:

```text
if (!target)
  -> target.operator bool()
  -> target.get()
  -> target.ref_.IsValid()
  -> flag_ && flag_->IsValid()      <- DCHECK same-sequence + acquire-load
```

So the cancellation check in a weak call bottoms out at that same sequence-bound, 100% accurate `IsValid` we covered in [02-4](./02-4-weak-ptr-sequence-affinity-and-lazy-binding.md). The moment the object dies, `get()` hands back `nullptr`, `operator bool` flips to false, and `MakeItSo` returns on the spot. The callback silently no-ops, and nobody so much as touches the dangling pointer.

---

## The key: the check is IsValid, not MaybeValid

Here's a point that's easy to get wrong, and that a lot of secondhand material gets wrong. The cancellation check in a weak call goes through `IsValid` (same-sequence, accurate), not `MaybeValid`. We drew this boundary back in [02-4](./02-4-weak-ptr-sequence-affinity-and-lazy-binding.md). `IsValid` is the hard gate before a dereference; `MaybeValid` is only an optimistic hint when crossing sequences. The `!target` line in `MakeItSo` flows through `operator bool`, through `get()`, and lands on `IsValid`. That's a deterministic judgment on the bound sequence, and it can promise "if the liveness check passes, the object is genuinely still breathing right now, safe to call."

So what's `MaybeValid` doing? It travels a **separate channel** and never touches the no-op decision. Chromium's `CallbackCancellationTraits` has a dedicated specialization for weak receivers (`bind_internal.h:1985-2006`) that splits "cancellation query" clean in two:

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

Each channel serves its own master. `IsCancelled(!receiver)` backs `Callback::IsCancelled()`, queried on the bound sequence, and the answer is nailed down. `MaybeValid(receiver.MaybeValid())` backs `Callback::MaybeValid()`, queryable from any sequence at the cost of treating the result as an optimistic estimate.

The `MaybeValid` line really serves the **scheduler / message loop**. Before a task is dispatched, the scheduler is free to speculatively probe `MaybeValid` from any sequence. If it comes back false, the scheduler knows this callback is definitely pointless and can skip it, saving a cross-sequence post in the bargain. But the moment the callback **actually executes**, the cancellation verdict runs through the `!target` line in `MakeItSo` (that is, `IsValid`). That line is the hard gate, and it's accurate.

One sentence to pin it down: cancellation has two paths. Execution-time goes through `IsValid` (accurate); the scheduler's speculative probe goes through `MaybeValid` (optimistic). The execution-time path never touches `MaybeValid`. Draw this boundary cleanly and you're already more precise than most secondhand write-ups out there.

---

## Weak calls are forced to return void

The weak branch hides one more constraint, tucked into `MakeItSo`'s return type: it returns `void`. That's not a coincidence; it's nailed down by the `WeakCallReturnsVoid` static_assert (`bind_internal.h:1028-1040`):

```cpp
if constexpr (WeakCallReturnsVoid<kIsWeakCall>::value) {
    // take the InvokeHelper<kIsWeakCall>::MakeItSo path
}
```

The reason clicks into place once you think about it. Once a callback is cancelled, what executes is `return;` (no value). But what if the method itself returns a value? At the moment of cancellation, what do you hand back? So a weak call **must return `void`**. Write `BindOnce(&Foo::get_value, weak_ptr)` where `get_value` returns `int`, and compile time refuses it outright. This is a textbook case of shutting down ambiguity at the type level. Cancellation semantics demand void, so the signature forces void. No room for fuzzy edges.

---

## The race defense behind "Unwrap before the liveness check"

Look back at that `MakeItSo` snippet. One comment deserves to be pulled out on its own (`bind_internal.h:949-951`):

> Note the validity of the weak pointer should be tested _after_ it is unwrapped, otherwise it creates a race for weak pointer implementations that allow cross-thread usage and perform `Lock()` in `Unwrap()` traits.

Meaning: the step `target = Unwrap(...)` must come **before** `if (!target)`. Why? Some weak-pointer variants allow cross-thread use, and their `Unwrap()` does a `Lock()` (Chromium has weaker-pointer implementations fancier than `WeakPtr` internally). For those, if you check the bool first and then Unwrap, the two steps crack open a race window. The object is alive at the bool check, and gone by the time Unwrap runs. Unwrap first (pulling out the real pointer safely), then check liveness, and the window closes.

Our own `WeakPtr`'s `Unwrap` is a passthrough with no Lock, so for it the two orders don't matter. But `MakeItSo` is a general template and has to cover the more general weak-pointer implementations, so the comment writes this race defense into the contract. Chromium's `bind_unittest.cc` even keeps a dedicated `MockRacyWeakPtr` around (its `operator bool()` always returns true, its `Lock()` always returns nullptr) precisely to exercise the "Unwrap before liveness check" path.

---

## vs Unretained(this): safe no-op after the fact vs UAF alarm after the fact

While we're here, let's pull in another idiom that gets conflated all the time: `base::Unretained(this)`. It also binds a member method into a callback, but it takes the **exact opposite** path, `InvokeHelper<false>`, with no liveness check at all:

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
        // no if (!target) return; -- Run() after the object dies is a dangling deref
    }
};
```

`Unretained`'s receiver unwraps to a raw `T*`, and the liveness check is skipped outright. Run the callback after the object dies and you get a UAF. Its only remaining line of defense is the PartitionAlloc backup-ref inside Chromium's `raw_ptr` memory-safety hardening, and that's an after-the-fact alarm, not avoidance.

The fundamental difference, one sentence:

> **A `WeakPtr` receiver means the callback silently no-ops after the object dies (safe up front). An `Unretained` receiver means UAF after the object dies (alarm after the fact, or UB).**

In production code, if there's any chance at all the object might destruct before the callback runs, reach for `WeakPtr` every time. `Unretained` only earns a spot when you can **statically swear** the object outlives the callback (the callback runs synchronously, entirely within the object's scope, that sort of thing).

---

## A teaching version: bolting WeakPtr onto the 01 OnceCallback

Let's reduce the industrial machinery down to a teaching version and bolt it onto the `OnceCallback` from the [01 series](../../01_once_callback/full/01-1-once-callback-motivation-and-api-design.md). There's nothing to it really, just translating that one `MakeItSo` line:

```cpp
// Platform: host | C++ Standard: C++20
// Simplified: bind a member method + WeakPtr<T> into a void() callback.
// (Here we use the 01 series' OnceCallback as the return type; the standalone
// compilable 18_bind_weakptr_cancel.cpp substitutes std::function, same logic.)
template <typename T, typename... Bound>
auto bind_weak_once(void (T::*method)(Bound...),
                    WeakPtr<T> receiver,
                    Bound... bound_args) {
    return OnceCallback<void()>(
        [method, receiver = std::move(receiver),
         bound = std::make_tuple(std::move(bound_args)...)]() mutable {
            if (!receiver) return;     // <- corresponds to the cancellation point in InvokeHelper<true>::MakeItSo
            std::apply(
                [&](auto&&... args) { (receiver.get()->*method)(args...); },
                bound);
        });
}
```

It looks austere, but it's isomorphic to the industrial `MakeItSo`: `if (!receiver) return;` lines up with `if (!target) return;`. `receiver` is a `WeakPtr<T>`, and `!receiver` flows through `operator bool`, through `get()`, down to `IsValid`; the moment the object dies it silently no-ops. Let's hang it on the 01 OnceCallback and run it:

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
        // Bind a callback whose receiver is c's WeakPtr.
        auto task = bind_weak_once(&Controller::on_work_done, c.get_weak(), 42);

        // Run while c is still alive -> calls on_work_done.
        std::move(task).run();                  // got 42
    }   // c destructs -> weak_factory_ invalidates all WeakPtrs first -> then buf_ destructs.

    // Now bind a fresh one and run it after c is dead.
    auto task2 = [&] {
        // Pretend we got hold of an already-invalidated WeakPtr (simulating
        // the object destructing while the task still sits in the queue).
        return bind_weak_once(&Controller::on_work_done,
                              WeakPtr<Controller>{}, 99);   // empty WeakPtr
    }();
    std::move(task2).run();                     // silent no-op, prints nothing.
    return 0;
}
```

You'll see `got 42` printed once. The second task, its receiver invalid, silently no-ops and prints nothing. The dangling-callback bug from 01-4 finally has a real antidote, and on the user's side the only added cost is one `get_weak_ptr()` call. The whole tangle of cancellation complexity is shouldered by `BindOnce` plus `WeakPtr`.

---

## Closing the loop: 01-4 hand-rolled token vs industrial WeakPtr

The series closes here. Let's put the 01-4 hand-rolled cancellation token and the industrial WeakPtr on the same table, one-to-one:

| 01-4 hand-rolled scheme | Industrial WeakPtr |
|---|---|
| One atomic flag (sharing across callbacks means manually copying the token) | `WeakReference::Flag` (`RefCountedThreadSafe` + `AtomicFlag`); the factory and all WeakPtrs **share the same one automatically** |
| Manually manage the flag's lifetime | `scoped_refptr<Flag>` refcount manages it |
| Pre-call `if (!flag.is_set()) return;` | `if (!target) return;` inside `InvokeHelper<true>::MakeItSo` |
| Manually stuff the flag into the callback | `kIsWeakMethod` / `IsWeakReceiver` **automatically** spots a WeakPtr receiver at compile time and picks the weak branch |
| Single check channel | Split into `IsCancelled` (same-sequence, accurate, for execution time) and `MaybeValid` (cross-sequence hint, for scheduler speculation) |
| Post-cancellation behavior: custom | Post-cancellation **forced silent no-op**, and weak calls are **forced to return void** (no value to hand back on cancellation) |

If you ask me, the two most important leaps are these. First, the factory is bound to the object's identity, and all WeakPtrs share the same Flag automatically, so "one invalidate, every callback drops together" comes for free (in 01-4's hand-rolled version the user still had to copy the token to share it across callbacks, and the flag was a standalone little object with no tie to the object's identity). Second, the wiring happens automatically at compile time (`kIsWeakMethod`). You just write `BindOnce(&C::m, weak_factory_.GetWeakPtr())` and the cancellation machinery slots itself into place. No hand-written if-check required.

That "loose end" we left in 01-4, how the flag gets passed in and who manages its life, is fully closed here. The flag is that shared Flag. Its life is managed by intrusive refcounting. It travels by WeakPtr handle. And it plugs into callbacks through compile-time wiring. Six prerequisite pieces, plus five hands-on pieces, and we've taken every screw out of the Chromium engineers' design and looked at it.

---

## References

- [Chromium `base/functional/bind_internal.h`: kIsWeakMethod / InvokeHelper / WeakCallReturnsVoid](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/bind_internal.h)
- [Chromium `base/functional/callback.h`: IsCancelled/MaybeValid](https://source.chromium.org/chromium/chromium/src/+/main:base/functional/callback.h)
- [OnceCallback hands-on (IV): the cancellation token](../../01_once_callback/full/01-4-once-callback-cancellation-token.md)
- [WeakPtr hands-on (IV): sequence affinity and lazy binding](./02-4-weak-ptr-sequence-affinity-and-lazy-binding.md)
