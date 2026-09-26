---
title: 'Iterator Pattern: From Manual Traversal to Generic Element Sequences with C++20 Ranges'
description: 'Starting from the most primitive style of hardcoding traversal logic into the caller, we squeeze out step by step an iterator that pairs with range-for, algorithms, and ranges adapters, and dismantle the hidden gate of the C++20 iterator concepts along the way'
chapter: 11
order: 18
tags:
  - host
  - cpp-modern
  - intermediate
  - 迭代器模式
difficulty: intermediate
platform: host
cpp_standard: [11, 17, 20]
reading_time_minutes: 22
related:
  - 'Singleton Pattern: From Comment-Only Constraints to Meyer''s Singleton'
prerequisites:
  - 'Chapter 6: Classes and Object-Oriented Programming'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/18-iterator.md
  source_hash: c96fcb06dae43aeffc2c69bfdaddd606331fa31c7c610c432b1d9354f445e227
  translated_at: '2026-09-26T05:42:50+00:00'
  engine: anthropic
  token_count: 12500
---

# Iterator Pattern: From Manual Traversal to Generic Element Sequences with C++20 Ranges

## What problem are we actually solving

Let's skip the definition for now. Picture a very common scenario: you have a binary tree and want to print it out in in-order, or copy it into a `std::vector` for sorting and deduplication. What's the most intuitive way to write it? Most likely something like this — a recursive function that also takes the target container as a parameter:

```cpp
void inorder_collect(TreeNode* node, std::vector<int>& out) {
    if (!node) return;
    inorder_collect(node->left, out);
    out.push_back(node->val);
    inorder_collect(node->right, out);
}

std::vector<int> result;
inorder_collect(root, result);
```

It runs, and it takes no effort to write. But it's not that simple. Swap in a new requirement: I don't want to copy into a vector, I want to filter out the even numbers while traversing; another one: I want to stop the in-order traversal at the first value equal to 5; yet another: I want to feed it to `std::count_if` to count the nodes satisfying some predicate. Every new requirement means writing yet another recursive function for this tree, copy-pasting the logic of "traverse this tree" over and over, when the only thing that actually differs is "what to do once you have a value". **The traversal strategy and the action taken after traversal are welded together for good**.

Replacement is even more painful. The day you decide "in-order isn't enough, I also want pre-order, post-order, level-order", you discover that each order needs its own complete set of `xxx_collect`, `xxx_find`, `xxx_count`. What the caller gets hold of is a concrete function name, not a "traversable thing" — it has no way to process this tree with generic tools.

This entanglement is exactly what the Iterator pattern solves. Its core idea fits in one sentence: **extract "how to traverse a collection" out of both the collection itself and the caller, encapsulate it into a standalone "iterator" object, and let the caller depend only on the uniform interface the iterator provides (dereference, advance, equality); whether what's being traversed is an array, a linked list, a tree, or a database cursor, the caller doesn't care at all**. In fact, you use this pattern every day without realizing it: `std::vector::iterator`, `std::map::iterator`, the range-based for loop, `std::sort`/`std::find`/`std::transform` — the Iterator pattern is behind all of them. The reason the standard library can apply one set of algorithms to dozens of containers at once is precisely this abstraction.

But "writing an iterator" in C++ has a commonly overlooked trap: **you think you got it right, it passes range-for, yet it doesn't satisfy the C++20 iterator concepts at all — so the ranges adapters and concept constraints are all unusable**. So let's proceed step by step: start from the dumbest way of writing it, see why each step falls short, and finally squeeze out an iterator that genuinely fits into the ranges ecosystem.

## Step 1: The most primitive approach — baking traversal into the caller (an anti-example)

Let's push the earlier approach one step further. Suppose you want to traverse a binary tree but don't want to recurse yourself sick every single time; you might just flatten the tree into a `std::vector` and traverse that:

```cpp
std::vector<int> flatten(const BinaryTree& tree) {
    std::vector<int> out;
    inorder_collect(tree.root(), out);
    return out;
}

for (int v : flatten(tree)) {  // Flatten the whole tree first, then read element by element
    if (v == target) break;
}
```

This "flatten first, then traverse" style is very common in production code — for a while it was even the standard way of handling tree-shaped data. It runs, and it looks quite clean. But the cost is buried deep — **to traverse a few elements, you materialize the entire tree into a `std::vector`**. If the tree has millions of nodes and you only want to find the first one satisfying a condition and break, then all the allocation, copying, and destruction for the remaining millions of elements are wasted work. Worse, if the tree itself is generated on demand (pruning in a search tree, infinite sequences), you fundamentally cannot "compute all the elements first" — this style strangles the very possibility of laziness.

The essence of the problem: **the traversal logic is not decoupled from the "materialization logic"**. What the caller receives is not an iterator that can "fetch the next element at any time", but a blob of memory whose contents are already fully computed. What we want is something in the spirit of "compute only when you ask me, save the effort when you don't" — advance on demand, dereference on demand, and that is the iterator's core contract.

## Step 2: The external iterator — one object that remembers "where the traversal has gotten to"

The external iterator is the school of thought the standard library follows: **stuff the traversal state into a standalone object, an object that knows "who should be returned next", while the caller is responsible for pushing it forward**. For an in-order traversal of a binary tree, the classic implementation uses a stack to remember "the chain of ancestors not yet fully visited".

Let's build the tree node and the iterator first, then explain how it moves:

```cpp
template<typename T>
struct TreeNode {
    T val;
    TreeNode* left = nullptr;
    TreeNode* right = nullptr;
    explicit TreeNode(T v) : val(v) {}
};

template<typename T>
class InorderIterator {
public:
    using Node = TreeNode<T>;

    InorderIterator() = default;              // A default-constructed one is end()
    explicit InorderIterator(Node* root) {
        push_left(root);                       // Push the left subtree onto the stack all the way, stopping at the leftmost leaf
    }

    T& operator*() const { return stack_.top()->val; }

    InorderIterator& operator++() {
        Node* node = stack_.top();
        stack_.pop();                           // Current node fully visited, pop it
        if (node->right) push_left(node->right); // Turn to the right subtree, keep pushing the left chain
        return *this;
    }

    bool operator==(const InorderIterator& other) const {
        // Equal if both stacks are empty (both at end); otherwise compare top pointers
        if (stack_.empty() && other.stack_.empty()) return true;
        if (stack_.empty() != other.stack_.empty()) return false;
        return stack_.top() == other.stack_.top();
    }

private:
    std::stack<Node*> stack_;
    void push_left(Node* node) {
        while (node) {
            stack_.push(node);
            node = node->left;
        }
    }
};
```

This little snippet is the Iterator pattern in its most classic form. Let's take the key lines apart. What is the `push_left(root)` call in the constructor doing? The rule of in-order traversal is "left, root, right", so the first node visited in any subtree is always its leftmost descendant. Starting from `root`, we keep executing `stack_.push(node); node = node->left;` until we run into `nullptr`; at this point the top of the stack is the smallest node in the whole tree — the starting point of the in-order sequence. This step finishes the entire job of "finding the starting point".

And how does one step of `operator++` move? In in-order, after a node has been visited (that is, dereferenced), the next one to visit is the smallest node in its right subtree. So the code first `pop`s the current node (it's done), then checks whether it has a right child: if yes, run `push_left` on the right subtree too, effectively jumping to the leftmost descendant of the right subtree; if not, the stack top simply becomes one of its ancestors. This "pop, then look at the right subtree" logic is precisely the standard technique for simulating in-order recursion with a stack.

`operator==` is there to compare against `end()`. We define "a default-constructed iterator" as `end()`, and its stack is empty; so when a working iterator reaches the end (its stack has been popped empty), it equals a default-constructed `end()` on the "both stacks empty" condition. That is the basis for the loop-termination test.

Now let's wrap the tree in a thin shell providing `begin()` / `end()`, so that range-for can recognize it directly:

```cpp
template<typename T>
class BinaryTree {
public:
    using Node = TreeNode<T>;
    using iterator = InorderIterator<T>;

    void set_root(Node* r) { root_ = r; }
    iterator begin() { return iterator(root_); }
    iterator end()   { return iterator(); }    // Default-constructed = end()

private:
    Node* root_ = nullptr;
};
```

Using it is a single range-for, so clean you can hardly tell we hand-wrote anything:

```cpp
//      4
//     / \
//    2   6
//   / \ / \
//  1  3 5  7
BinaryTree<int> tree;
// ... build the tree ...
for (int v : tree) {
    std::cout << v << " ";   // 1 2 3 4 5 6 7
}
```

Let's write a complete, runnable version to verify. To spare ourselves the manual `new`/`delete` boilerplate, this time the tree nodes are owned by `std::unique_ptr` — also the more recommended style in production code:

```cpp
#include <iostream>
#include <memory>
#include <stack>

template<typename T>
struct TreeNode {
    T val;
    TreeNode* left = nullptr;
    TreeNode* right = nullptr;
    explicit TreeNode(T v) : val(v) {}
};

template<typename T>
class InorderIterator {
public:
    using Node = TreeNode<T>;
    InorderIterator() = default;
    explicit InorderIterator(Node* root) { push_left(root); }
    T& operator*() const { return stack_.top()->val; }
    InorderIterator& operator++() {
        Node* node = stack_.top();
        stack_.pop();
        if (node->right) push_left(node->right);
        return *this;
    }
    bool operator==(const InorderIterator& other) const {
        if (stack_.empty() && other.stack_.empty()) return true;
        if (stack_.empty() != other.stack_.empty()) return false;
        return stack_.top() == other.stack_.top();
    }
private:
    std::stack<Node*> stack_;
    void push_left(Node* node) {
        while (node) { stack_.push(node); node = node->left; }
    }
};

template<typename T>
class BinaryTree {
public:
    using Node = TreeNode<T>;
    using iterator = InorderIterator<T>;
    void set_root(Node* r) { root_ = r; }
    iterator begin() { return iterator(root_); }
    iterator end()   { return iterator(); }
private:
    Node* root_ = nullptr;
};

int main() {
    auto n1 = std::make_unique<TreeNode<int>>(1);
    auto n3 = std::make_unique<TreeNode<int>>(3);
    auto n5 = std::make_unique<TreeNode<int>>(5);
    auto n7 = std::make_unique<TreeNode<int>>(7);
    auto n2 = std::make_unique<TreeNode<int>>(2); n2->left = n1.get(); n2->right = n3.get();
    auto n6 = std::make_unique<TreeNode<int>>(6); n6->left = n5.get(); n6->right = n7.get();
    auto n4 = std::make_unique<TreeNode<int>>(4); n4->left = n2.get(); n4->right = n6.get();

    BinaryTree<int> tree;
    tree.set_root(n4.get());
    for (int v : tree) std::cout << v << " ";
    std::cout << "\n";
}
```

Compile and run:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra iterator_verify.cpp -o iterator_verify
$ ./iterator_verify
1 2 3 4 5 6 7
```

The in-order output is correct, and range-for accepts it. At this point you probably feel "this iterator has made it". But it doesn't end here — **the real trap is still ahead**.

## Let's verify first: can it really get into ranges

range-for works only because the compiler mechanically expands `for (int v : tree)` into `for (auto it = tree.begin(); it != tree.end(); ++it)`; it invokes exactly those member functions — `begin`, `end`, `operator++`, `operator*`, `operator!=` — recognizing syntax only, never checking concepts. In other words, **range-for is syntactic sugar, not a concept check**. Passing this syntax doesn't mean your iterator is a "qualified iterator".

C++20 introduced a concept-based iterator hierarchy (`std::input_iterator`, `std::forward_iterator`, ...), and the standard library's ranges adapters (`std::views::filter`, `std::views::transform`, and friends) are all constrained with concepts — if your iterator doesn't satisfy `std::input_iterator`, the adapters reject you, and the error messages routinely run to hundreds of lines, sending your blood pressure through the roof. Let's take the iterator above that "passes range-for" and see whether it actually satisfies the C++20 iterator concepts:

```cpp
#include <iterator>
#include <iostream>

// Assume InorderIterator / BinaryTree are the definitions from above
int main() {
    using It = InorderIterator<int>;
    std::cout << std::boolalpha;
    std::cout << "input_iterator:    " << std::input_iterator<It> << "\n";
    std::cout << "weakly_incrementable: " << std::weakly_incrementable<It> << "\n";
    std::cout << "indirectly_readable:  " << std::indirectly_readable<It> << "\n";
}
```

Compile and run:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra concept_check.cpp -o concept_check
$ ./concept_check
input_iterator:    false
weakly_incrementable: false
indirectly_readable:  true
```

`input_iterator` is `false`. In other words, this iterator that "gets into range-for" simply isn't an iterator in the eyes of C++20 ranges. `views::filter`, `views::transform`, `ranges::count_if` — none of them work. Where's the problem? Drill one level down: `weakly_incrementable` is also `false`, and `weakly_incrementable` is a subconcept of `input_iterator` — it blocked you first.

## The real trap: the missing postfix `operator++`

Let's keep drilling: what does the `weakly_incrementable` concept actually require? Looking up [std::weakly_incrementable](https://en.cppreference.com/w/cpp/iterator/weakly_incrementable) on cppreference (since C++20), among its requirements on a type `I`, besides `iter_difference_t<I>` having to be a signed integer-class type, there are two requirements about increment expressions — **both `++i` and `i++` must be valid expressions**. Note: it doesn't ask for only `++i` (prefix); `i++` (postfix) must exist too.

This is the easiest place to faceplant. Our iterator only defined the prefix `operator++()`, not the postfix `operator++(int)`, so it got filtered out right at the concept-check step. This isn't the standard library being deliberately difficult — the internal implementation of the ranges machinery (all that `it++`-style code) is written under the premise "the postfix must exist too", with no syntactic fallback. Add the postfix, and the whole concept goes through:

```cpp
void operator++(int) { ++(*this); }   // Postfix ++: forwards to the prefix, discards the return value
```

While we're at it, let's add it together with two other "standard kit" pieces of C++20 iterators and look at the whole thing. C++20 recommends declaring the iterator category through the nested `iterator_concept` (note: `iterator_concept`, not the old standard's `iterator_category`), together with the two associated types `value_type` and `difference_type`. The two old-standard aliases `pointer`/`reference` are actually omittable in modern code, but carrying them is harmless and keeps some old algorithms quiet:

```cpp
template<typename T>
class InorderIterator {
public:
    using Node = TreeNode<T>;
    using iterator_concept  = std::input_iterator_tag;  // C++20: use iterator_concept
    using iterator_category = std::input_iterator_tag;  // Compatibility with old algorithms
    using value_type        = T;
    using difference_type   = std::ptrdiff_t;
    using pointer           = T*;
    using reference         = T&;

    InorderIterator() = default;
    explicit InorderIterator(Node* root) { push_left(root); }

    reference operator*() const { return stack_.top()->val; }

    InorderIterator& operator++() {                       // Prefix
        Node* node = stack_.top();
        stack_.pop();
        if (node->right) push_left(node->right);
        return *this;
    }
    void operator++(int) { ++(*this); }                   // Postfix (the key part!)

    bool operator==(const InorderIterator& other) const {
        if (stack_.empty() && other.stack_.empty()) return true;
        if (stack_.empty() != other.stack_.empty()) return false;
        return stack_.top() == other.stack_.top();
    }

private:
    std::stack<Node*> stack_;
    void push_left(Node* node) {
        while (node) { stack_.push(node); node = node->left; }
    }
};
```

That's all it took — one extra `void operator++(int)`. Let's rerun the concept check, this time testing the ranges views along with it:

```cpp
#include <ranges>
#include <iostream>

int main() {
    using It = InorderIterator<int>;
    std::cout << std::boolalpha;
    std::cout << "input_iterator: " << std::input_iterator<It> << "\n";
    std::cout << "input_range:    " << std::ranges::input_range<BinaryTree<int>> << "\n";

    BinaryTree<int> tree;
    // ... build the tree ...

    std::cout << "filter even: ";
    for (int v : tree | std::views::filter([](int x) { return x % 2 == 0; })) {
        std::cout << v << " ";
    }
    std::cout << "\n";

    std::cout << "transform x10: ";
    for (int v : tree | std::views::transform([](int x) { return x * 10; })) {
        std::cout << v << " ";
    }
    std::cout << "\n";
}
```

Compile and run:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra iterator_full_verify.cpp -o iterator_full_verify
$ ./iterator_full_verify
input_iterator: true
input_range:    true
filter even: 2 4 6
transform x10: 10 20 30 40 50 60 70
```

This time everything passes. `std::input_iterator` is `true`, `std::ranges::input_range<BinaryTree<int>>` is also `true`, both `views::filter` and `views::transform` can be bolted directly on, and — this is exactly what we wanted at the start — **the entire pipeline is lazy**: neither filter nor transform materializes the tree into a vector; it computes as far as you've walked, and once you break, it stops advancing. With one postfix `++` added, our iterator graduated from "syntactically limps along" to "semantically fits into ranges".

::: warning Pitfall alert: don't write only the prefix ++
The C++20 iterator concepts (`weakly_incrementable`, `input_iterator`, `forward_iterator` — all of them) implicitly require the postfix `operator++(int)` to exist as well. An iterator that defines only the prefix `++` can pass range-for (because range-for uses only the prefix) and many old algorithms (they call the prefix directly), but **it cannot pass the ranges adapters** — `views::filter`, `views::transform`, `ranges::sort` all reject you, and the error messages are absurdly long on top. Your muscle memory for writing iterators should be: **provide the prefix and the postfix together**. The postfix is usually just the single line `void operator++(int) { ++(*this); }` — there's no reason to skimp on it.
:::

## One more check: why it isn't a forward_iterator

You may have noticed that we keep saying `input_iterator`, never `forward_iterator`. There's something worth calling out explicitly here: **this stack iterator can never be promoted to forward_iterator, no matter how diligently you add the postfix `++`**. Let's verify:

```sh
$ # Extending the concept_check from above
forward_iterator: false
forward_range:    false
```

Why? `forward_iterator` adds one very hard requirement on top of `input_iterator` — the **multi-pass guarantee**. It demands: if you copy an iterator, advance the copy, and then continue walking from the original, the sequence you see must be consistent with the copy's; in other words, multiple iterators starting from the same point, each walking independently to the end, must all yield the same complete sequence. In the standard library, `std::forward_list::iterator` and `std::vector::iterator` are forward or above — they point to definite positions, advance independently after copying without interfering with each other, and you can start over from the beginning at any time.

Our stack iterator can't do this. Its "position" is not uniquely determined by a pointer but by the entire contents of that `stack_`, and `stack_` is computed once at construction by pushing the whole left chain in; two iterators each hold an independent `stack_`, and after copying they pop separately — the sequences match up, but **you have no way to "restart" an iterator for a second pass** — to restart, you must `push_left(root)` again, which means reconstructing. The multi-pass clause "the original iterator is unaffected by advancing the copy" is, for the stack iterator, essentially impossible to satisfy without a side-effect-free rebuild. So its ceiling is exactly `input_iterator`: **single-pass, forward-only, use-and-discard**.

This is actually not a defect but a design choice. The semantics of in-order traversal — "maintaining the ancestor chain the whole way" — are single-pass by nature; for true multi-pass, you'd have to switch to a different iterator implementation (for example, storing an extra "in-order predecessor/successor" pointer in each node, like a threaded binary tree), or simply pack the tree into a contiguous container and use random-access iterators. **The iterator category (concept) is not "the higher the better" but "the closer it fits your data structure, the better"** — forcibly upgrading input to forward either can't be done, or costs extra storage.

## Bolting algorithms on: where the iterator truly pays off

With the concepts completed, the most satisfying part arrives: **every specialized function you previously wrote for this tree can now be deleted and replaced with standard library algorithms**. Let's look at a few of the most common scenarios.

Finding the first node satisfying a condition used to require its own recursion; now `std::find_if` bolts straight on, with the semantics "find the first element satisfying the predicate within a range of input iterators", stopping as soon as it finds one and returning `end()` otherwise:

```cpp
auto it = std::find_if(tree.begin(), tree.end(),
                       [](int v) { return v > 4; });
if (it != tree.end()) {
    std::cout << "first > 4: " << *it << "\n";   // first > 4: 5
}
```

Counting the nodes satisfying a condition used to need yet another recursion; now `std::count_if` bolts straight on — internally it just advances while counting, exactly the kind of work a single-pass input iterator can do:

```cpp
auto cnt = std::count_if(tree.begin(), tree.end(),
                         [](int v) { return v % 2 == 1; });
std::cout << "odd count: " << cnt << "\n";        // odd count: 4
```

Copying into a `std::vector` for further processing used to require flattening first; now one line of `std::copy`, and it genuinely "copies while walking" rather than materializing first and copying afterwards:

```cpp
std::vector<int> sorted_by_inorder;
std::copy(tree.begin(), tree.end(),
          std::back_inserter(sorted_by_inorder));
```

Feeding the whole tree's output into a ranges pipeline is the biggest dividend C++20 pays out. One `|` after another, composed like Unix pipes, lazy throughout, with zero intermediate containers:

```cpp
auto view = tree
    | std::views::filter([](int v) { return v % 2 == 0; })
    | std::views::transform([](int v) { return v * v; });

for (int v : view) {
    std::cout << v << " ";   // 4 16 36 (squares of the even values 2/4/6)
}
```

This is what the Iterator pattern is really after — **once your collection provides a pair of qualified iterators, the entire standard library algorithm collection plus the ranges adapter collection is yours for free**. No bespoke code for "filter", "transform", "find", "count" — all of it reused.

## Internal vs external iterators: two orientations

The external iterator (the kind we wrote above) hands the initiative of "advance", "dereference", and "equality" to the caller; the caller decides when to move forward and when to stop. The standard library takes this road exclusively, because it **aligns naturally with algorithms** — `std::find_if` must stop the moment it finds the target, and this "interrupt at any moment" control can only be handed to the caller by an external iterator.

But there is another road, called the **internal iterator**: the collection manages the traversal itself, and the caller merely supplies a "what to do once you have an element" callback; the collection applies that callback to every element. The most typical example is `forEach` in various languages:

```cpp
template<typename F>
void for_each_inorder(TreeNode* node, F&& fn) {
    if (!node) return;
    for_each_inorder(node->left, fn);
    fn(node->val);
    for_each_inorder(node->right, fn);
}

// Caller: no need to care how the traversal works, just write "what to do with each value"
for_each_inorder(root, [](int v) {
    if (v % 2 == 0) std::cout << v << " ";
});
```

The internal iterator's advantage is minimal caller code — the collection wraps up all the traversal details; the disadvantage lives exactly there too — **control sits in the collection's hands, and the caller cannot "stop midway"**. You want to "break at the first even number"? Sorry, `forEach` doesn't give you that ability; the callback gets invoked from start to finish, even if you stopped caring long ago. That's why the standard library firmly chose external iterators: **algorithms need interruption, need composition, need laziness, and only external iterators can provide these**.

Neither orientation is the more advanced one; they each have their use cases. For simple traversal and pure side-effect operations (printing, logging, emitting events), internal iterators are more comfortable to write; the moment "early termination", "multi-step composition", or "interfacing with the algorithm library" is involved, the external iterator is the only choice. The entire C++ ecosystem leans toward the latter, which is why this article only elaborated the modern form of the external iterator.

## Going further: writing the traversal "as if it were recursion" with coroutines

Our stack iterator has one rough edge: that `push_left` + `pop` + `check the right subtree` logic reads far less intuitively than directly writing a recursive in-order function. In fact, C++20 coroutines offer us a more elegant road — **write the recursive in-order traversal directly as a coroutine with a generator, which still exposes a "fetch the next element on demand" interface to the outside, essentially equivalent to an iterator**. The beauty of coroutines: you write ordinary recursive code, and the compiler turns it into a "pausable, resumable" state machine behind your back; each time you ask for a value, it resumes once, yields a value out, and suspends again.

Let's first write a minimal `Generator<T>` — internally it holds a coroutine handle, and externally it offers the ability to "fetch the next value":

```cpp
template<typename T>
class Generator {
public:
    struct promise_type {
        T current_value;
        Generator get_return_object() {
            return Generator{handle_type::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(T value) {
            current_value = std::move(value);   // Suspend here, handing the value to the caller
            return {};
        }
        void return_void() {}
        void unhandled_exception() { std::terminate(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    explicit Generator(handle_type h) : handle_(h) {}
    Generator(Generator&& other) noexcept
        : handle_(std::exchange(other.handle_, {})) {}
    Generator(const Generator&) = delete;
    Generator& operator=(Generator&&) = delete;
    ~Generator() { if (handle_) handle_.destroy(); }

    bool next() {                                // Advance one step, return whether a value remains
        if (!handle_ || handle_.done()) return false;
        handle_.resume();
        return !handle_.done();
    }
    T value() const { return handle_.promise().current_value; }

private:
    handle_type handle_;
};
```

This `promise_type` is the contract between the coroutine and the outside world; let's pick out a few key members. `initial_suspend` returns `suspend_always`, meaning the coroutine suspends immediately upon creation and doesn't run automatically — this guarantees "if you don't call `next()`, not a single line executes", which is precisely laziness. `yield_value` is the function invoked behind `co_yield`: on every `co_yield v`, the coroutine stores `v` into `current_value`, then suspends and hands control back to the caller; on the caller's next `next()`, the coroutine resumes from this point and keeps running. `final_suspend` also returns `suspend_always`, guaranteeing the coroutine isn't automatically destroyed after finishing, but kept around for us to `destroy()` in the destructor at the end — otherwise premature release of the coroutine frame is the classic use-after-free.

With this `Generator`, the in-order traversal can be written almost verbatim, identical to the recursive version in your head:

```cpp
template<typename T>
Generator<T> inorder_generator(TreeNode<T>* node) {
    if (!node) co_return;
    if (node->left) {
        Generator<T> left = inorder_generator(node->left);  // Recurse into the left subtree
        while (left.next()) co_yield left.value();          // Yield the left subtree's values one by one
    }
    co_yield node->val;                                      // Then yield the root
    if (node->right) {
        Generator<T> right = inorder_generator(node->right);
        while (right.next()) co_yield right.value();
    }
}
```

Look: this code has no stack, no `push_left`, no `pop` — just plain recursion line by line, except that the places where "a value should be handed out" are written as `co_yield`. The coroutine machinery generates, on your behalf, the full state machine of "pause here, resume from here later". Let's use it and see whether it truly produces the in-order sequence lazily:

```cpp
int main() {
    // ... build the same 1..7 tree ...
    auto gen = inorder_generator(root);
    while (gen.next()) {
        std::cout << gen.value() << " ";
    }
    std::cout << "\n";
}
```

Compile and run:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra coro_generator_verify.cpp -o coro_generator_verify
$ ./coro_generator_verify
1 2 3 4 5 6 7
```

The output is identical to the stack-based in-order iterator, and it's equally lazy — one `next()` call computes one step; don't call it and it doesn't budge. The real sweet spot of the coroutine road: **complex traversals (especially recursively defined ones — the many traversal orders of trees, graph DFS, on-demand infinite sequences) written with coroutines read far better than hand-rolled stacks**. The price is that you must understand the coroutine frame's lifetime, that whole `promise_type` contract, and the generator's own overhead (every `co_yield` is a suspend/resume). Whether to replace hand-written iterators with coroutines in production code depends on your readability-versus-overhead tradeoff — but the mere fact that "coroutines can be used to implement the Iterator pattern" deserves a slot in your toolbox.

::: tip Coroutine iterators vs concept iterators
A coroutine generator exposes a member-function API like `next()`/`value()`; unlike our earlier `operator*`/`operator++` set, it doesn't directly satisfy the `std::input_iterator` concept, so by default it can't be piped into the ranges adapters. To get it into ranges, you must wrap `Generator` with another layer of `begin()`/`end()`, returning a lightweight wrapper that satisfies `input_iterator` (mapping `next()` to `operator++` and `value()` to `operator*`). The standard library doesn't do this for you — C++23's `std::generator` is the official "works out of the box, satisfies the iterator concepts" generator. If your toolchain supports C++23, prefer `std::generator` and don't hand-roll `promise_type` yourself.
:::

## The costs of the Iterator pattern

At this point we have an iterator that pairs with range-for, with algorithms, and with ranges adapters, plus a bonus look at the coroutine road. But we can't look only at the shiny side — I owe you an honest account of the Iterator pattern's own costs, so you don't slap it on everywhere.

**First, the implementation cost is not low.** A "qualified" external iterator (especially one heading into ranges) requires getting that whole set right — `operator*`, prefix `++`, postfix `++`, `operator==`, associated type aliases — plus deciding clearly which concept level it stops at (input, forward, bidirectional, random_access); there are a great many edge conditions. Stack iterators, doubly-linked-list iterators, hash-table iterators — each kind has its own state-management pitfalls. If your collection has exactly one traversal need and only one or two call sites, writing a `for_each` member function directly is usually far more economical than fiddling with iterators.

**Second, iterator invalidation is C++'s classic minefield.** An iterator essentially holds "a reference to some position inside the collection"; once the collection is modified while you're iterating (a `std::vector` growing its capacity, a `std::map` insertion), previously obtained iterators can instantly turn into dangling pointers, and continuing to use them is undefined behavior. The standard library explicitly specifies "which operations invalidate which iterators" for every container, but the standard library has no reach over your custom collections — you must write the documentation and provide the guarantees yourself, and callers still have to read it themselves.

**Third, abstraction has a runtime cost, even though modern compilers can erase a lot of it.** A hand-written iterator satisfying the forward-or-above concepts can usually be inlined down to hand-written-loop speed; but input iterators (like our stack iterator here), with their stack state and their indirect jumps outside of vtables, often can't be fully erased. For performance-sensitive hot paths, "just write a dedicated loop" may well be that tiny bit faster than "generic iterator + algorithm". That's the price of abstraction; whether it's worth it is the profiler's call.

**Fourth, there is tension between concept level and performance.** To make an iterator satisfy a higher-level concept (forward, bidirectional, random_access), you often have to add extra information to the data structure (threading pointers, random-access indexes), and this storage overhead is real money. Don't upgrade mindlessly for "the higher the concept, the prettier" — our stack iterator earlier stays honestly at `input_iterator`, and that is exactly where it belongs.

## Summary

Let's straighten out the whole evolution path:

| Stage | Approach | Why it's still not enough |
|---|---|---|
| Traversal baked into the caller | Recursion + target container parameter | Traversal strategy and action welded together; every new requirement means a rewrite |
| Flatten first, then traverse | `flatten` into a vector, then range-for | Materializes a full copy, not lazy, can't handle infinite sequences |
| External iterator (stack) | `operator*`/`++`/`==` + `begin`/`end` | Passes range-for, but can't get into ranges |
| Concept kit completed | Postfix `++` + `iterator_concept` + associated types | **Good enough** (`input_iterator` passes, ranges adapters usable) |
| Coroutine generator | Recursive traversal written with `co_yield`, on-demand value fetch externally | Highly readable, naturally lazy, but requires understanding coroutine frame lifetimes |

Note down these key conclusions:

- **When writing an external iterator, the prefix and postfix `operator++` must be provided together**; otherwise `weakly_incrementable`/`input_iterator` doesn't hold and none of the ranges adapters are usable. Passing range-for does not mean passing the concepts.
- Since C++20, declare the iterator category with the nested `iterator_concept` (not the old `iterator_category`), together with `value_type`/`difference_type`; the old aliases may be kept for compatibility with old algorithms.
- **An iterator's concept level is not "the higher the better"** — it should fit the data structure's natural capabilities. The ceiling of a stack-based in-order iterator is exactly `input_iterator` (single-pass, no multi-pass); forcibly upgrading to forward costs extra storage.
- The standard library algorithms and ranges adapters are the Iterator pattern's real dividend — once a collection provides a pair of qualified iterators, `find_if`/`count_if`/`copy`/`views::filter`/`views::transform` are all reused for free, lazy throughout.
- External iterators (the standard library's school, control in the caller's hands) and internal iterators (the `forEach` school, control in the collection) — neither is the more advanced; they suit different scenarios: when interruption, composition, or interfacing with algorithms is needed, external is the only choice.
- Complex recursive traversals can be implemented with C++20 coroutine generators, with readability far above hand-rolled stacks; in production, if C++23 is supported, prefer `std::generator` — it satisfies the iterator concepts out of the box.

::: tip A companion compilable project
The examples in this section have a complete compilable project under `code/volumn_codes/vol4/design-patterns/Iterator/` in the repository (`.h` + main + `CMakeLists.txt`); `cmake -S . -B build && cmake --build build` reproduces the outputs above.
:::

## References

- [cppreference: Iterator library](https://en.cppreference.com/w/cpp/iterator) (since C++20)
- [cppreference: `std::weakly_incrementable`](https://en.cppreference.com/w/cpp/iterator/weakly_incrementable) (the concept requirement on the postfix `++`, since C++20)
- [cppreference: `std::input_iterator`](https://en.cppreference.com/w/cpp/iterator/input_iterator) (since C++20)
- [cppreference: Ranges library](https://en.cppreference.com/w/cpp/ranges) (`views::filter` / `views::transform`, since C++20)
- [cppreference: Coroutines](https://en.cppreference.com/w/cpp/language/coroutines) (`co_yield` / `promise_type`, since C++20)
- [cppreference: `std::generator`](https://en.cppreference.com/w/cpp/coroutine/generator) (C++23's ready-to-use coroutine iterator)
