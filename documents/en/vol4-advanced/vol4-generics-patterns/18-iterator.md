---
title: 'Iterator Pattern: From Manual Traversal to C++20 Ranges'
description: Starting from the most primitive approach of "writing iteration logic
  directly into the caller," we will progressively derive an iterator that works with
  range-for loops, algorithms, and ranges adapters, while effortlessly clearing the
  hidden hurdle of C++20 iterator concepts.
chapter: 11
order: 18
tags:
- host
- cpp-modern
- intermediate
- 迭代器模式
difficulty: intermediate
platform: host
cpp_standard:
- 11
- 17
- 20
reading_time_minutes: 22
related:
- 单例模式:从注释约束到 Meyer's Singleton
prerequisites:
- 'Chapter 6: 类与对象'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/18-iterator.md
  source_hash: c96fcb06dae43aeffc2c69bfdaddd606331fa31c7c610c432b1d9354f445e227
  translated_at: '2026-06-24T01:03:42.724614+00:00'
  engine: anthropic
  token_count: 5446
---
# Iterator Pattern: From Manual Traversal to C++20 Ranges for Generic Element Sequences

## What problem are we actually solving?

Let's skip the formal definition for a moment. Consider a common scenario: you have a binary tree, and you want to print it in-order, or copy its elements into a `std::vector` for sorting and deduplication. What is the most intuitive way to write this? Most likely, it looks like this—write a recursive function directly, passing the target container as an argument:

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

It works, and it's not hard to write. But things aren't that simple. Let's change the requirement: instead of copying to a `vector`, I want to iterate and filter out even numbers on the fly; or change it again: I want to stop at the first value equal to 5 during an in-order traversal; or yet another: I want to feed it to `std::count_if` to count nodes satisfying a predicate. With every new requirement, you have to write a new recursive function for this tree, copying and pasting the logic of "traversing this tree" over and over again, when the only real difference is "what to do after getting a value." **The traversal strategy and the action performed after traversal are tightly coupled.**

Even more painful is replacement. One day you decide "in-order isn't enough, I want pre-order, post-order, and level-order," and you will find that every order requires writing a completely separate set of `xxx_collect`, `xxx_find`, and `xxx_count`. The caller receives a specific function name, not a "traversable object"—it cannot use generic tools to process this tree.

The iterator pattern solves exactly this entanglement. Its core idea can be summed up in one sentence: **decouple "how to traverse a collection" from the collection itself and the caller, encapsulating it into an independent "iterator" object. The caller only relies on the unified interface provided by the iterator (dereference, advance, equality check), regardless of whether it's traversing an array, linked list, tree, or database cursor.** In fact, you use this pattern every day without realizing it: `std::vector::iterator`, `std::map::iterator`, range-for loops, `std::sort`/`std::find`/`std::transform`—they are all backed by the iterator pattern. The reason the standard library can apply a single set of algorithms to dozens of containers is precisely because of this abstraction.

However, "writing an iterator" in C++ has a commonly overlooked pitfall: **you think you wrote it correctly and it works with range-for, but it doesn't actually satisfy C++20 iterator concepts, so ranges adapters and concept constraints are completely unusable.** Next, we will proceed step-by-step, starting with the dumbest approach, seeing why each step isn't enough, and finally forcing out a truly ranges-compatible iterator.

## Step 1: The most primitive approach — putting traversal logic in the caller (the anti-pattern)

Let's take the previous approach a step further. Suppose you want to traverse a binary tree but don't want to recurse until you puke every time; you might just flatten the tree into a `std::vector` and iterate over that:

```cpp
std::vector<int> flatten(const BinaryTree& tree) {
    std::vector<int> out;
    inorder_collect(tree.root(), out);
    return out;
}

for (int v : flatten(tree)) {  // 先整棵树压平,再逐个读
    if (v == target) break;
}
```

This "flatten first, traverse later" pattern is extremely common in production code. It was once the standard way to handle tree-like data structures. It works, and it looks clean enough. However, the cost is well hidden: **to traverse just a few elements, you materialize the entire tree into a `std::vector`**. If the tree has millions of nodes, and you only want to find the first element that meets a condition before breaking, the allocation, copying, and destruction of the remaining millions of elements are all wasted effort. Even worse, if the tree itself is generated on demand (e.g., pruning in a search tree, infinite sequences), you can't "calculate all elements first" at all—this approach completely kills the possibility of "laziness."

The root of the problem is: **the traversal logic is not decoupled from the "materialization logic."** The caller receives not an iterator that can "fetch the next element on command," but a chunk of memory where everything has already been calculated. What we need is a "calculate only when I ask, save resources when I don't" mechanism—advancing on demand and fetching values on demand. This is the core contract of an iterator.

## Step 2: External Iterator—Using an Object to Remember "Where We Are"

The external iterator is the approach favored by the standard library: **encapsulate the traversal state in a separate object that knows "who to return next," while the caller is responsible for driving it forward**. For an in-order traversal of a binary tree, a classic implementation uses a stack to remember the "chain of ancestors not yet fully visited."

Let's first build the tree node and the iterator, and then explain how it moves:

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

    InorderIterator() = default;              // 默认构造出来的就是 end()
    explicit InorderIterator(Node* root) {
        push_left(root);                       // 一路把左子树压栈,停在最左叶
    }

    T& operator*() const { return stack_.top()->val; }

    InorderIterator& operator++() {
        Node* node = stack_.top();
        stack_.pop();                           // 访问完当前节点,弹出
        if (node->right) push_left(node->right); // 转向右子树,继续压左链
        return *this;
    }

    bool operator==(const InorderIterator& other) const {
        // 两个都空(都到 end)就算相等;否则比较栈顶指针
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

This short snippet represents the classic iterator pattern. Let's break down the key lines. What does the `push_left(root)` call during construction actually do? The rule for in-order traversal is "Left, Root, Right", so the first node visited in any subtree must be its leftmost descendant. Starting from `root`, we execute `stack_.push(node); node = node->left;` until we hit `nullptr`. At this point, the node at the top of the stack is the smallest node in the entire tree—that is, the starting point of the in-order traversal. This step completes the task of "finding the starting point."

How does `operator++` advance the iterator? In in-order traversal, after a node has been visited (dereferenced), the next node to visit is the smallest one in its right subtree. Therefore, the code first `pop`s the current node (it has been processed) and then checks if it has a right child. If it does, it runs `push_left` on the right subtree, effectively jumping to the leftmost descendant of the right subtree. If not, the top of the stack naturally becomes one of its ancestors. This logic of "pop then check right subtree" is the standard technique for simulating recursive in-order traversal using a stack.

`operator==` is used for comparison with `end()`. We define a "default-constructed iterator" as `end()`, which has an empty stack. Consequently, when a working iterator reaches the end (its stack has been popped empty), it becomes equal to a default-constructed `end()` based on the condition that "both stacks are empty." This serves as the basis for the loop termination condition.

Now, let's wrap the tree with a thin shell to provide `begin()` and `end()`, allowing range-based for loops to work with it directly:

```cpp
template<typename T>
class BinaryTree {
public:
    using Node = TreeNode<T>;
    using iterator = InorderIterator<T>;

    void set_root(Node* r) { root_ = r; }
    iterator begin() { return iterator(root_); }
    iterator end()   { return iterator(); }    // 默认构造 = end()

private:
    Node* root_ = nullptr;
};
```

Using it is just a range-for loop, so clean that it's almost impossible to tell we wrote anything by hand:

```cpp
//      4
//     / \
//    2   6
//   / \ / \
//  1  3 5  7
BinaryTree<int> tree;
// ... 建树 ...
for (int v : tree) {
    std::cout << v << " ";   // 1 2 3 4 5 6 7
}
```

Let's write a complete, runnable version to verify this. To eliminate the boilerplate of manual `new`/`delete`, we will manage the tree nodes using `std::unique_ptr` this time, which is the recommended approach for production code:

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

Let's compile and run it:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra iterator_verify.cpp -o iterator_verify
$ ./iterator_verify
1 2 3 4 5 6 7
```

The inorder output is correct, and range-for accepts it. At this point, you might think, "the iterator is done." But we're not finished yet—**the real trap lies ahead**.

## Let's verify first: can it actually enter ranges?

Just because range-for works doesn't mean much. The compiler mechanically expands `for (int v : tree)` into `for (auto it = tree.begin(); it != tree.end(); ++it)`. It simply calls the member functions `begin`, `end`, `operator++`, `operator*`, and `operator!=`. It only checks the syntax, not the concepts. In other words, **range-for is syntactic sugar, not a concept check**. Just because the syntax passes doesn't prove your iterator is a "valid iterator."

C++20 introduced a concept-based iterator hierarchy (`std::input_iterator`, `std::forward_iterator`, etc.). Standard library ranges adapters (like `std::views::filter` and `std::views::transform`) are constrained by concepts. If your iterator doesn't satisfy `std::input_iterator`, the adapters will reject it, often with error messages spanning hundreds of lines, which is incredibly frustrating. Let's take the iterator above—the one that "works with range-for"—and see if it actually satisfies the C++20 iterator concepts:

```cpp
#include <iterator>
#include <iostream>

// 假设 InorderIterator / BinaryTree 就用上面的定义
int main() {
    using It = InorderIterator<int>;
    std::cout << std::boolalpha;
    std::cout << "input_iterator:    " << std::input_iterator<It> << "\n";
    std::cout << "weakly_incrementable: " << std::weakly_incrementable<It> << "\n";
    std::cout << "indirectly_readable:  " << std::indirectly_readable<It> << "\n";
}
```

Let's compile and run it:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra concept_check.cpp -o concept_check
$ ./concept_check
input_iterator:    false
weakly_incrementable: false
indirectly_readable:  true
```

`input_iterator` evaluates to `false`. This means that our iterator, which works in range-based for loops, is not considered a valid iterator by C++20 Ranges. We cannot use `views::filter`, `views::transform`, or `ranges::count_if` with it. Where is the problem? Digging one level deeper, `weakly_incrementable` is also `false`. Since `weakly_incrementable` is a subconcept of `input_iterator`, it blocks us right there.

## The Real Pitfall: Missing the Postfix `operator++`

Let's dig deeper to see what the `weakly_incrementable` concept actually requires. Checking cppreference for [std::weakly_incrementable](https://en.cppreference.com/w/cpp/iterator/weakly_incrementable) (since C++20), the requirements for a type `I` include `iter_difference_t<I>` being a signed integer-like type, but there are also two requirements regarding increment expressions—**both `++i` and `i++` must be valid expressions**. Note: it requires not just `++i` (prefix), but `i++` (postfix) as well.

This is where it is easiest to trip up. Our iterator only implemented the prefix `operator++()`, but not the postfix `operator++(int)`, so it failed the concept check. The standard library isn't being intentionally difficult—the internal implementation of the ranges system (code using various `it++` forms) is written with the premise that "postfix must exist," and it does not fall back to other syntaxes. Once we add the postfix operator, the concept is satisfied:

```cpp
void operator++(int) { ++(*this); }   // 后缀 ++:转发给前缀,丢掉返回值
```

Let's take this opportunity to complete the picture by adding two other C++20 iterator "staples." C++20 recommends declaring the iterator category via the nested `iterator_concept` (note that it is `iterator_concept`, not the legacy `iterator_category`), along with the two associated types `value_type` and `difference_type`. The legacy aliases `pointer` and `reference` are actually optional in modern code, but including them is harmless and keeps some older algorithms quiet:

```cpp
template<typename T>
class InorderIterator {
public:
    using Node = TreeNode<T>;
    using iterator_concept  = std::input_iterator_tag;  // C++20:用 iterator_concept
    using iterator_category = std::input_iterator_tag;  // 兼容老算法
    using value_type        = T;
    using difference_type   = std::ptrdiff_t;
    using pointer           = T*;
    using reference         = T&;

    InorderIterator() = default;
    explicit InorderIterator(Node* root) { push_left(root); }

    reference operator*() const { return stack_.top()->val; }

    InorderIterator& operator++() {                       // 前缀
        Node* node = stack_.top();
        stack_.pop();
        if (node->right) push_left(node->right);
        return *this;
    }
    void operator++(int) { ++(*this); }                   // 后缀(关键!)

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

That's just one extra `void operator++(int)`. Let's run the concept check again, this time testing the ranges views as well:

```cpp
#include <ranges>
#include <iostream>

int main() {
    using It = InorderIterator<int>;
    std::cout << std::boolalpha;
    std::cout << "input_iterator: " << std::input_iterator<It> << "\n";
    std::cout << "input_range:    " << std::ranges::input_range<BinaryTree<int>> << "\n";

    BinaryTree<int> tree;
    // ... 建树 ...

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

Let's compile and run it:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra iterator_full_verify.cpp -o iterator_full_verify
$ ./iterator_full_verify
input_iterator: true
input_range:    true
filter even: 2 4 6
transform x10: 10 20 30 40 50 60 70
```

It all works now. `std::input_iterator` is `true`, `std::ranges::input_range<BinaryTree<int>>` is also `true`, and `views::filter` and `views::transform` can be applied directly. Moreover—and this is exactly what we wanted from the start—**the entire pipeline is lazy**. Neither `filter` nor `transform` materialize the tree into a `vector`; computation happens on demand, and if we `break`, iteration stops. By adding the postfix `++`, our iterator is upgraded from "syntactically functional" to "semantically compatible with ranges".

::: warning Pitfall Alert: Don't Just Write Prefix ++
The C++20 iterator concepts (`weakly_incrementable`, `input_iterator`, `forward_iterator`, etc.) implicitly require the existence of the postfix `operator++(int)`. An iterator with only a prefix `++` will work with range-for (since range-for only uses prefix) and many legacy algorithms (which call prefix directly), but it **will not pass ranges adapters**—`views::filter`, `views::transform`, and `ranges::sort` will all reject it, with ridiculously long error messages. The muscle memory for writing iterators should be:**provide both prefix and postfix**. The postfix is usually just a one-liner: `void operator++(int) { ++(*this); }`. There is no reason to skip it.
:::

## Let's Verify Again: Why It Isn't `forward_iterator`

You may have noticed that we have been talking about `input_iterator`, not `forward_iterator`. There is a specific point worth highlighting here:**this stack iterator will never be promoted to `forward_iterator`, no matter how diligently you add the postfix `++`**. Let's verify this:

```sh
$ # 接着上面的 concept_check 扩展
forward_iterator: false
forward_range:    false
```

Why? Because `forward_iterator` imposes a much stricter requirement than `input_iterator`—the **multi-pass guarantee**. It requires that if you copy an iterator and advance the copy, the sequence you observe from the original iterator must remain consistent with the copy. In other words, multiple iterators starting from the same point must each be able to traverse the complete sequence independently. In the standard library, `std::forward_list::iterator` and `std::vector::iterator` are all forward iterators or higher—they point to a definite position. After copying, they advance independently without interference, and you can restart the traversal from the beginning at any time.

Our stack iterator cannot do this. Its "position" is not uniquely determined by a single pointer, but by the entire contents of `stack_`, which is computed by pushing the entire left chain into the stack at construction time. Two iterators have independent `stack_` instances. After copying, they each `pop`. Although the sequences match, **you cannot "restart" and traverse a second time from one iterator**—to restart, you would need to `push_left(root)` again, effectively reconstructing it. The multi-pass guarantee rule that "the original iterator is unaffected by the advancement of the copy" is fundamentally impossible for a stack iterator to achieve without side effects. Therefore, its ceiling is `input_iterator`: **single-pass, forward-only, and disposable.**

This is actually not a defect, but a design choice. The semantics of in-order traversal, which "requires maintaining a chain of ancestors along the way," are inherently single-pass. To truly support multi-pass, you would need a different iterator implementation (for example, storing an "in-order predecessor/successor" pointer in each node, like in a threaded binary tree), or simply flattening the tree into a contiguous container and using a random access iterator. **The hierarchy of iterator concepts is not "the higher the better," but rather "the better it fits your data structure."** Forcing an input iterator to become a forward iterator is either impossible or requires extra storage costs.

## Plugging in Algorithms: The Real Value of Iterators

Once we have satisfied the concept, the best part of this iterator arrives: **you can now delete all those custom functions you wrote for this tree and replace them with standard library algorithms.** Let's look at a few common scenarios.

To find the first node that meets a condition, you used to have to write a separate recursive function. Now, you can just apply `std::find_if`. The semantics are "find the first element in the input iterator range that satisfies the predicate." It stops when found, or returns `end()` if not found:

```cpp
auto it = std::find_if(tree.begin(), tree.end(),
                       [](int v) { return v > 4; });
if (it != tree.end()) {
    std::cout << "first > 4: " << *it << "\n";   // first > 4: 5
}
```

To count the number of nodes satisfying a condition, we previously had to write another recursive function. Now, we can directly apply `std::count_if`. Internally, it advances while counting, which is exactly what a single-pass input iterator can do:

```cpp
auto cnt = std::count_if(tree.begin(), tree.end(),
                         [](int v) { return v % 2 == 1; });
std::cout << "odd count: " << cnt << "\n";        // odd count: 4
```

We copy to a `std::vector` for further processing. Previously, we had to flatten it first; now, one line of `std::copy` does the job, and it truly copies "on the fly" rather than materializing first and then copying:

```cpp
std::vector<int> sorted_by_inorder;
std::copy(tree.begin(), tree.end(),
          std::back_inserter(sorted_by_inorder));
```

Feeding the entire tree output into a ranges pipeline is the biggest benefit C++20 brings. We chain them one `|` after another, composing them like Unix pipes, and the whole process is lazy and zero intermediate container:

```cpp
auto view = tree
    | std::views::filter([](int v) { return v % 2 == 0; })
    | std::views::transform([](int v) { return v * v; });

for (int v : view) {
    std::cout << v << " ";   // 4 16 36(偶数 2/4/6 平方)
}
```

This is the true goal of the iterator pattern—**as long as your collection provides a pair of valid iterators, the entire Standard Library algorithm suite and ranges adapter library are yours for free**. There is no need to write specialized code for "filtering," "transforming," "searching," or "counting"; simply reuse everything.

## Internal vs. External Iterators: Two Approaches

External iterators (the kind we wrote above) hand the initiative for "advancing," "fetching values," and "checking equality" to the caller. The caller decides when to advance and when to stop. The Standard Library follows this path exclusively because it **aligns naturally with algorithms**—`std::find_if` needs to stop immediately upon finding a target. This kind of "interruptible" control can only be provided to the caller via an external iterator.

However, there is another path, known as an **internal iterator**: the collection manages the traversal itself, and the caller only provides a callback for "what to do with the element." The collection then applies this callback to each element. The most typical example is the `forEach` found in various languages:

```cpp
template<typename F>
void for_each_inorder(TreeNode* node, F&& fn) {
    if (!node) return;
    for_each_inorder(node->left, fn);
    fn(node->val);
    for_each_inorder(node->right, fn);
}

// 调用方:不用管怎么遍历,只写「拿到一个值干什么」
for_each_inorder(root, [](int v) {
    if (v % 2 == 0) std::cout << v << " ";
});
```

The main advantage of internal iterators is that the caller code is extremely concise, as the collection handles all the traversal details. However, the disadvantage stems from this exact trait: **control lies with the collection, so the caller cannot "stop halfway"**. Want to "break on finding the first even number"? Sorry, `forEach` doesn't give you that ability; you have to let the callback be invoked from start to finish, even if you stopped caring long ago. This is why the standard library firmly chose external iterators: **algorithms need interruption, composition, and laziness, and only external iterators can provide these**.

Neither approach is inherently superior; they simply have different use cases. For simple traversals and pure side-effect operations (printing, logging, firing events), internal iterators are more convenient. But once "early termination," "multi-step composition," or "interfacing with algorithm libraries" are involved, external iterators are the only choice. The entire C++ ecosystem leans toward the latter, so this article focuses exclusively on the modern implementation of external iterators.

## Going Further: Using Coroutines to Make Traversal "Look Like Recursion"

Our stack-based iterator has a somewhat unappealing characteristic: the logic involving `push_left` + `pop` + `check right subtree` is far less intuitive to read than a direct recursive inorder function. Actually, C++20 coroutines offer a more elegant path—**using a generator to write the recursive inorder traversal directly as a coroutine. It still exposes a "get next element on demand" interface, which is essentially equivalent to an iterator**. The beauty of coroutines lies here: you write normal recursive code, and the compiler transforms it behind the scenes into a "suspendable and resumable" state machine. Every time you need a value, it resumes, yields a value, and then suspends.

Let's first write a minimal `Generator<T>`—it holds a coroutine handle internally and provides the ability to "get the next value":

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
            current_value = std::move(value);   // 暂停在这里,把值交给调用方
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

    bool next() {                                // 推进一步,返回是否还有值
        if (!handle_ || handle_.done()) return false;
        handle_.resume();
        return !handle_.done();
    }
    T value() const { return handle_.promise().current_value; }

private:
    handle_type handle_;
};
```

This `promise_type` acts as the interface contract between the coroutine and the outside world. Let's examine a few key members. `initial_suspend` returns `suspend_always`, meaning the coroutine suspends immediately upon creation and does not execute automatically. This guarantees that "without a call to `next()`, it executes nothing," which is true laziness. `yield_value` is the function invoked behind the scenes by `co_yield`: each time `co_yield v` is called, the coroutine stores `v` in `current_value`, suspends, and yields control back to the caller. When the caller invokes `next()` again, the coroutine resumes execution from this point. `final_suspend` also returns `suspend_always`, ensuring the coroutine frame is not automatically destroyed after completion. This allows us to manually call `destroy()` in the destructor later; otherwise, the coroutine frame would be released prematurely, resulting in a classic use-after-free bug.

With this `Generator`, the inorder traversal can be written almost verbatim, exactly like the recursive version you have in mind:

```cpp
template<typename T>
Generator<T> inorder_generator(TreeNode<T>* node) {
    if (!node) co_return;
    if (node->left) {
        Generator<T> left = inorder_generator(node->left);  // 递归左子树
        while (left.next()) co_yield left.value();          // 把左子树的值逐个 yield 出去
    }
    co_yield node->val;                                      // 再 yield 根
    if (node->right) {
        Generator<T> right = inorder_generator(node->right);
        while (right.next()) co_yield right.value();
    }
}
```

Look, this code has no stack, no `push_left`, and no `pop`. It is just recursion, line by line, except where we need to hand back a value, we write `co_yield`. The coroutine mechanism generates the entire state machine for "pause here and resume here later" for us. Let's use it to see if it really produces an in-order sequence lazily:

```cpp
int main() {
    // ... 同样的 1..7 建树 ...
    auto gen = inorder_generator(root);
    while (gen.next()) {
        std::cout << gen.value() << " ";
    }
    std::cout << "\n";
}
```

Let's compile and run it:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra coro_generator_verify.cpp -o coro_generator_verify
$ ./coro_generator_verify
1 2 3 4 5 6 7
```

The output is identical to the in-order stack iterator, and it is just as lazy—it only calculates when you call `next()`, and stays idle otherwise. The real sweet spot for coroutines lies here: **for complex traversals (especially recursively defined ones like various tree traversals, graph DFS, or infinite sequences expanded on demand), writing them with coroutines yields far better readability than hand-rolling a stack.** The cost is that you must understand the lifecycle of the coroutine frame, the contract around `promise_type`, and the overhead of the generator itself (every `co_yield` is a suspend/resume). Whether to use coroutines to replace hand-written iterators in production code depends on your trade-off between readability and overhead—but the fact that "coroutines can be used to implement the iterator pattern" is worth keeping in your toolbox.

::: tip Difference between coroutine iterators and concept iterators
Coroutine generators expose a member function API like `next()`/`value()`. Unlike our previous `operator*`/`operator++` setup, they don't directly satisfy the `std::input_iterator` concept, so they can't be used with ranges adapters out of the box. To make them work with ranges, you need to wrap `Generator` with `begin()`/`end()`, returning a lightweight wrapper that satisfies `input_iterator` (mapping `next()` to `operator++` and `value()` to `operator*`). The standard library doesn't do this for you; C++23's `std::generator` is the official "batteries-included, concept-satisfying" generator—if your toolchain supports C++23, prioritize `std::generator` instead of hand-rolling `promise_type`.
:::

## The Cost of the Iterator Pattern

At this point, we have an iterator that works with range-for, algorithms, and ranges adapters, and we've also seen the coroutine approach. But we can't just look at the bright side—I need to be honest about the costs of the iterator pattern so you don't apply it blindly.

**First, the implementation cost is not low.** A "qualified" external iterator (especially one that needs to work with ranges) requires getting the whole set of `operator*`, prefix `++`, postfix `++`, `operator==`, and associated type aliases correct. You also have to think clearly about which concept layer it stops at (input, forward, bidirectional, random_access), and there are many edge cases. Stack iterators, doubly linked list iterators, hash table iterators—each has its own state management pitfalls. If your collection has only one traversal need and one or two call sites, writing a `for_each` member function is often more cost-effective than wrestling with iterators.

**Second, iterator invalidation is a classic minefield in C++.** Iterators essentially hold a "reference to a position inside the collection." Once the collection is modified during iteration (e.g., `std::vector` reallocation, `std::map` insertion), previously obtained iterators can instantly become dangling pointers; continuing to use them is undefined behavior. The standard library explicitly specifies "which operations invalidate which iterators" for every container, but the standard library can't manage your custom collections—you have to document it yourself, guarantee it yourself, and the caller has to read it.

**Third, abstraction has runtime costs, although modern compilers can eliminate much of it.** A hand-written iterator that satisfies forward or higher concepts can usually be inlined to be as fast as a hand-written loop. However, input iterators (like our stack iterator) have stack state and indirect jumps outside the virtual table, which often can't be completely eliminated. For performance-sensitive scenarios on hot paths, "writing a specialized loop directly" might be slightly faster than "using a generic iterator + algorithms." This is the cost of abstraction; whether it's worth it is for the profiler to decide.

**Fourth, there is tension between concept hierarchy and performance.** To make an iterator satisfy a higher-level concept (forward, bidirectional, random_access), you often have to add extra information to the data structure (threaded pointers, random access indices). These storage costs are real money. Don't upgrade mindlessly just for the sake of "higher concepts are prettier"—our stack iterator should honestly stay at `input_iterator`, which is where it belongs.

## Summary

Let's review the entire evolution path:

| Stage | Approach | Why it wasn't enough |
|---|---|---|
| Write traversal in caller | Recursion + target container param | Traversal strategy and action are welded together; rewrite on requirement change |
| Flatten then traverse | `flatten` to vector then range-for | Materializes a full copy, not lazy, can't handle infinite sequences |
| External iterator (stack) | `operator*`/`++`/`==` + `begin`/`end` | Works with range-for, but can't enter ranges |
| Complete concept support | Postfix `++` + `iterator_concept` + associated types | **Sufficient** (`input_iterator` works, ranges adapters usable) |
| Coroutine generator | `co_yield` recursive traversal, fetch on demand | High readability, naturally lazy, but requires understanding coroutine frame lifecycle |

Keep these key conclusions in mind:

- **When writing external iterators, you must provide both prefix and postfix `operator++`**, otherwise `weakly_incrementable`/`input_iterator` is not satisfied, and ranges adapters won't work. Passing range-for doesn't mean the concept is qualified.
- Starting with C++20, use the nested `iterator_concept` (not the old `iterator_category`) to declare the iterator kind, along with `value_type`/`difference_type`; old aliases can be kept for compatibility with older algorithms.
- **The iterator concept hierarchy is not "the higher the better"**; it should fit the natural ability of the data structure. The ceiling for an in-order stack iterator is `input_iterator` (single-pass, not multi-pass); forcing it to forward incurs extra storage costs.
- Standard library algorithms and ranges adapters are the real dividend of the iterator pattern—as long as the collection provides a pair of qualified iterators, `find_if`/`count_if`/`copy`/`views::filter`/`views::transform` are all freely reusable, and fully lazy.
- External iterators (the standard library style, control in caller) and internal iterators (the `forEach` style, control in collection) aren't about who is more advanced, but about different use cases: if you need interruption, composition, or interfacing with algorithms, you must choose external.
- Complex recursive traversals can be implemented using C++20 coroutine generators, with readability far superior to hand-rolled stacks; in production, if C++23 is supported, prioritize `std::generator`, which satisfies iterator concepts out of the box.

::: tip Companion Compilable Project
The examples for this section are in the repository `code/volumn_codes/vol4/design-patterns/Iterator/` as a complete compilable project (`.h` + main + `CMakeLists.txt`). Run `cmake -S . -B build && cmake --build build` to see the outputs shown above.
:::

## References

- [cppreference: Iterator library](https://en.cppreference.com/w/cpp/iterator) (Since C++20)
- [cppreference: `std::weakly_incrementable`](https://en.cppreference.com/w/cpp/iterator/weakly_incrementable) (Concept requirements for postfix `++`, since C++20)
- [cppreference: `std::input_iterator`](https://en.cppreference.com/w/cpp/iterator/input_iterator) (Since C++20)
- [cppreference: Ranges library](https://en.cppreference.com/w/cpp/ranges) (`views::filter` / `views::transform`, since C++20)
- [cppreference: Coroutines](https://en.cppreference.com/w/cpp/language/coroutines) (`co_yield` / `promise_type`, since C++20)
- [cppreference: `std::generator`](https://en.cppreference.com/w/cpp/coroutine/generator) (Coroutine iterator that works out of the box in C++23)
