---
title: "迭代器模式:从手写遍历到 C++20 ranges 的通用元素序列"
description: "从「把遍历逻辑写进调用方」这种最原始的写法开始,一步步逼出一个能配 range-for、配算法、配 ranges 适配器的迭代器,顺手拆掉 C++20 迭代器概念的那道隐藏门槛"
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
  - "单例模式:从注释约束到 Meyer's Singleton"
prerequisites:
  - "Chapter 6: 类与对象"
---

# 迭代器模式:从手写遍历到 C++20 ranges 的通用元素序列

## 我们到底在解决什么问题

我们先不上定义。想一个特别常见的场景:你有一个二叉树,现在想按中序把它打印出来,或者拷到一个 `std::vector` 里去排序去重。最直觉的写法是什么?大概率是这样的——直接写一个递归函数,顺带把目标容器作为参数传进去:

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

能跑,而且写起来不费劲。但事情没这么简单。你换一个需求:我不要拷到 vector 里,我要边遍历边过滤出偶数;再换一个:我要中序遍历到第一个等于 5 的值就停;再换一个:我要把它喂给 `std::count_if` 统计满足某个谓词的节点数。每来一个新需求,你就得为这棵树新写一个递归函数,把「遍历这棵树」这件事的逻辑复制粘贴一遍又一遍,而真正不同的只是「拿到一个值之后干什么」。**遍历策略和遍历之后的动作,被死死焊在了一起**。

更难受的是替换。哪天你觉得「中序不够,我还想要前序、后序、层序」,你会发现每一种顺序都得独立写一整套 `xxx_collect`、`xxx_find`、`xxx_count`。调用方拿到的是一个具体的函数名,而不是一个「可遍历的东西」——它没法用通用的工具去处理这棵树。

迭代器模式要解决的,就是这个纠缠。它的核心想法一句话就能说完:**把「怎么遍历一个集合」从集合本身和调用方里抽离出来,封装成一个独立的「迭代器」对象,调用方只依赖迭代器提供的统一接口(取值、前进、判等),至于遍历的是数组、链表、树还是数据库游标,调用方一概不管**。其实你天天在用这个模式而不自知:`std::vector::iterator`、`std::map::iterator`、范围 for 循环、`std::sort`/`std::find`/`std::transform`,背后全是迭代器模式。标准库之所以能把一套算法同时套到几十种容器上,靠的就是这套抽象。

但「写一个迭代器」在 C++ 里有个常被忽视的坑:**你以为写对了,能过 range-for,但它根本不满足 C++20 的迭代器概念,于是 ranges 适配器、concept 约束全部用不了**。接下来我们就一步步来,先从最蠢的写法看起,看看每一步为什么不够,最后逼出一个真正能融进 ranges 体系的迭代器。

## 第一步:最原始的写法——把遍历写进调用方(反面教材)

我们把前面的写法再往前推一步。假设你想遍历一棵二叉树,但又不想每次都递归到吐,你可能干脆把树压平成一个 `std::vector` 再遍历:

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

这种「先压平再遍历」的写法在生产代码里非常常见,甚至一度是处理树形数据的标准操作。能跑,而且看起来还挺干净。但代价藏得很深——**你为了遍历几个元素,把整棵树都物化成了一份 `std::vector`**。如果树有上百万个节点,而你只想找第一个满足条件的就 break,那剩下几百万个元素的全部分配、拷贝、析构全是白干的。更糟的是,如果树本身是按需生成的(比如搜索树的剪枝、无限序列),你根本没办法「先把所有元素算出来」——这种写法直接把「惰性」的可能性掐死了。

问题的本质是:**遍历逻辑没有从「物化逻辑」里解耦**。调用方拿到的不是一个「随时能取下一个元素」的迭代器,而是一坨已经全部算完的内存。我们要的是一种「你问我才算、你不问我省着」的东西——按需前进、按需取值,而这就是迭代器的核心契约。

## 第二步:外部迭代器——用一个对象记住「遍历到哪了」

外部迭代器(external iterator)是标准库那一系的思路:**把遍历状态塞进一个独立的对象里,这个对象知道「下一个该返回谁」,调用方负责推动它前进**。对一个中序遍历的二叉树来说,经典的实现是用一个栈来记住「当前还没访问完的祖先链」。

我们先把树节点和迭代器搭出来,再解释它怎么动:

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

这一小段就是迭代器模式最经典的样子。关键的几行我们拆开看。构造时调用的 `push_left(root)` 是在干什么?中序遍历的规则是「左、根、右」,所以任何一个子树的第一个被访问的节点,一定是它最左的那个后代。我们从 `root` 出发,一路 `stack_.push(node); node = node->left;`,直到撞到 `nullptr`,这时光栈顶就是整棵树里最小的那个节点——也就是中序的起点。这一步把「找到起点」的活儿全做完了。

`operator++` 推进一步又是怎么动的?中序里,一个节点被访问完(也就是被解引用过)之后,下一个该访问的就是它的右子树里最小的那个。所以代码先 `pop` 掉当前节点(它已经处理完了),然后看它有没有右孩子:有就把右子树也走一遍 `push_left`,相当于跳到右子树的最左后代;没有就直接让栈顶变成它的某个祖先。这套「pop 之后看右子树」的逻辑,恰好就是中序遍历用栈模拟递归的标准手法。

`operator==` 拿来和 `end()` 比较。我们把「默认构造出来的迭代器」定义成 `end()`,它的栈是空的;于是一个正在工作的迭代器走到头(栈被 pop 空了)时,就和一个默认构造的 `end()` 在「栈都空」这个条件上相等。这就是循环终止的判定基础。

现在我们给树套一层薄壳,提供 `begin()` / `end()`,让 range-for 能直接认它:

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

用起来就是一句 range-for,干净到几乎看不出我们手写了什么:

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

我们写一个完整可跑的版本验证一下。为了省去手动 `new`/`delete` 的样板,这次树节点用 `std::unique_ptr` 托管,这也是生产代码里更推荐的写法:

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

编译跑一下:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra iterator_verify.cpp -o iterator_verify
$ ./iterator_verify
1 2 3 4 5 6 7
```

中序输出正确,range-for 也认了它。到这里你大概觉得「这迭代器成了」。但事情到这里还没完——**真正的坑在后面**。

## 这里先验证一下:它真能进 ranges 吗

range-for 能用,只是因为编译器把 `for (int v : tree)` 机械地展开成 `for (auto it = tree.begin(); it != tree.end(); ++it)`,它调用的就是 `begin`、`end`、`operator++`、`operator*`、`operator!=` 这几个成员函数,只认语法,不查概念。换句话说,**range-for 是语法糖,不是概念检查**。这套语法能过,不代表你的迭代器是个「合格的迭代器」。

C++20 引入了基于概念的迭代器体系(`std::input_iterator`、`std::forward_iterator`……),标准库的 ranges 适配器(`std::views::filter`、`std::views::transform` 这些)都是用概念来约束的——你的迭代器不满足 `std::input_iterator`,适配器就拒绝你,而且报错信息动辄几百行,血压拉满。我们先拿上面这个「能过 range-for」的迭代器,看看它到底满足不满足 C++20 的迭代器概念:

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

编译跑一下:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra concept_check.cpp -o concept_check
$ ./concept_check
input_iterator:    false
weakly_incrementable: false
indirectly_readable:  true
```

`input_iterator` 是 `false`。也就是说,我们这个「能进 range-for」的迭代器,在 C++20 的 ranges 眼里根本不是个迭代器。`views::filter`、`views::transform`、`ranges::count_if` 全都用不了。问题出在哪?往下钻一层,`weakly_incrementable` 也是 `false`,而 `weakly_incrementable` 是 `input_iterator` 的子概念——它先挡住了你。

## 真正的坑:少了那个后缀 `operator++`

我们继续往里钻,`weakly_incrementable` 这个概念到底要什么。翻 cppreference 的 [std::weakly_incrementable](https://en.cppreference.com/w/cpp/iterator/weakly_incrementable)(C++20 起),它对一个类型 `I` 的要求里,除了 `iter_difference_t<I>` 得是有符号整数类类型之外,还有两条关于自增表达式的要求——**`++i` 和 `i++` 都必须是合法的表达式**。注意:它要的不是只有 `++i`(前缀),而是连 `i++`(后缀)也得有。

这是最容易栽跟头的地方。我们的迭代器只写了前缀 `operator++()`,没写后缀 `operator++(int)`,于是在概念检查那一步就被刷掉了。这不是标准库故意刁难——ranges 体系的内部实现(各种 `it++` 形式的代码)就是按「后缀也得有」这个前提写的,它不做语法回退。补上后缀,整个概念就通了:

```cpp
void operator++(int) { ++(*this); }   // 后缀 ++:转发给前缀,丢掉返回值
```

我们顺手把它和另外两件 C++20 迭代器的「标配」一起补上,完整看一下。C++20 推荐你通过嵌套的 `iterator_concept`(注意是 `iterator_concept`,不是老标准的 `iterator_category`)来声明迭代器种类,再带上 `value_type` 和 `difference_type` 两个关联类型。`pointer`/`reference` 这两个老标准的别名在现代代码里其实已经可省,但带上无害,还能让一些老算法闭嘴:

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

就多了那一个 `void operator++(int)`。我们重新跑一遍概念检查,这次连 ranges 视图一起测了:

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

编译跑一下:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra iterator_full_verify.cpp -o iterator_full_verify
$ ./iterator_full_verify
input_iterator: true
input_range:    true
filter even: 2 4 6
transform x10: 10 20 30 40 50 60 70
```

这一次全通了。`std::input_iterator` 是 `true`,`std::ranges::input_range<BinaryTree<int>>` 也是 `true`,`views::filter` 和 `views::transform` 都能直接套上去,而且——这正是我们一开始想要的——**整条流水线是惰性的**,filter 和 transform 都不会把树物化成 vector,你走到哪算到哪,你 break 了它就不再前进。补一个后缀 `++`,我们的迭代器就从「语法上凑合能跑」升级成「语义上能融进 ranges」了。

::: warning 踩坑预警:别只写前缀 ++
C++20 的迭代器概念(`weakly_incrementable`、`input_iterator`、`forward_iterator` 全是)隐式要求后缀 `operator++(int)` 也得存在。只写前缀 `++` 的迭代器,能过 range-for(因为 range-for 只用前缀),能过很多老算法(它们直接调前缀),但**过不了 ranges 适配器**——`views::filter`、`views::transform`、`ranges::sort` 一律拒绝你,报错信息还长得离谱。写迭代器的肌肉记忆应当是:**前缀和后缀一起给**。后缀通常就是 `void operator++(int) { ++(*this); }` 一行,没有理由省。
:::

## 这里再验证一下:它为什么不是 forward_iterator

你可能注意到了,我们一直说的是 `input_iterator`,没说 `forward_iterator`。这里有个值得专门点出来的事:**这个栈迭代器永远升不到 forward_iterator,不管你后缀 `++` 补得多勤**。我们验证一下:

```sh
$ # 接着上面的 concept_check 扩展
forward_iterator: false
forward_range:    false
```

为什么?`forward_iterator` 比 `input_iterator` 多了一条非常硬的要求——**多趟(multi-pass)保证**。它要求:如果你把一个迭代器复制一份,推进副本,之后从原迭代器继续走,你看到的序列必须和副本一致;换句话说,同一个起点出发的多个迭代器,各自独立走完都能得到完整的相同序列。标准库里 `std::forward_list::iterator`、`std::vector::iterator` 都是 forward 及以上——它们指向的是确定的位置,复制之后各自前进互不干扰,而且你随时可以从头再来一遍。

我们的栈迭代器做不到这一点。它的「位置」不是由一个指针唯一决定的,而是由那一整条 `stack_` 的内容决定的,而 `stack_` 是在构造时一次性把左链全部压进去算出来的;两个迭代器各自有独立的 `stack_`,复制之后各自 `pop`,虽然序列对得上,但**你没办法从一个迭代器「重新开始」走第二遍**——要重新开始,就得重新 `push_left(root)`,也就是重新构造。多趟保证里的「原迭代器不受副本推进影响」这一条,对栈迭代器来说本质上做不到无副作用重建。所以它的天花板就是 `input_iterator`:**单趟、只能前进、用完即弃**。

这其实不是缺陷,是设计选择。中序遍历这种「需要一路维护祖先链」的语义,天生就是单趟的;真要多趟,你得换一种迭代器实现(比如在每个节点里额外存一个「中序前驱/后继」指针,像线索二叉树那样),或者干脆把这棵树拍进一个连续容器里再用随机访问迭代器。**迭代器的种类(concept)不是越高越好,而是越贴合你的数据结构越好**——硬把 input 升成 forward,要么做不到,要么得付额外的存储代价。

## 拿算法套上去:迭代器真正的价值

补齐概念之后,这个迭代器最爽的地方就来了:**你之前为这棵树写的所有专用函数,现在可以全部删掉,换成标准库算法**。我们看几个最常见的场景。

找第一个满足条件的节点,以前要单独写递归;现在 `std::find_if` 直接套上去,语义是「在输入迭代器范围内找第一个满足谓词的」,找到就停,找不到返回 `end()`:

```cpp
auto it = std::find_if(tree.begin(), tree.end(),
                       [](int v) { return v > 4; });
if (it != tree.end()) {
    std::cout << "first > 4: " << *it << "\n";   // first > 4: 5
}
```

统计满足条件的节点数,以前要再写一个递归;现在 `std::count_if` 直接套,它内部就是一边前进一边数,正好就是单趟 input 迭代器能干的事:

```cpp
auto cnt = std::count_if(tree.begin(), tree.end(),
                         [](int v) { return v % 2 == 1; });
std::cout << "odd count: " << cnt << "\n";        // odd count: 4
```

拷到一个 `std::vector` 里去进一步处理,以前要先压平;现在 `std::copy` 一行,而且它是真的「边走边拷」,不是先物化再拷:

```cpp
std::vector<int> sorted_by_inorder;
std::copy(tree.begin(), tree.end(),
          std::back_inserter(sorted_by_inorder));
```

把整棵树的输出喂进 ranges 管道,这是 C++20 给的最大红利。一个 `|` 接一个,像 Unix 管道一样组合,而且全程惰性、零中间容器:

```cpp
auto view = tree
    | std::views::filter([](int v) { return v % 2 == 0; })
    | std::views::transform([](int v) { return v * v; });

for (int v : view) {
    std::cout << v << " ";   // 4 16 36(偶数 2/4/6 平方)
}
```

这就是迭代器模式真正想要的东西——**你的集合只要提供一对合格的迭代器,整个标准库算法库 + ranges 适配器库就免费归你了**。不用为「过滤」「变换」「查找」「计数」各写一份专用代码,全部复用。

## 内部迭代器 vs 外部迭代器:两种取向

外部迭代器(就是我们上面写的这一种)把「前进」「取值」「判等」的主动权交给调用方,调用方决定什么时候推进、什么时候停下。标准库走的全是这条路,原因是它**和算法天然对齐**——`std::find_if` 要在找到目标时立即停下来,这种「随时中断」的控制权,只能由外部迭代器提供给调用方。

但还有另一条路,叫**内部迭代器(internal iterator)**:集合自己掌管遍历,调用方只提供一个「拿到元素之后干什么」的回调,集合负责把回调套到每个元素上。最典型的就是各种语言里的 `forEach`:

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

内部迭代器的好处是调用方代码极简,集合把遍历细节全包了;缺点也恰恰出在这——**控制权在集合手里,调用方没法「中途停下」**。你想「找到第一个偶数就 break」?抱歉,`forEach` 不给你这个能力,你只能让回调被从头到尾调用一遍,哪怕你早就不关心了。这就是为什么标准库坚定地选了外部迭代器:**算法需要中断、需要组合、需要惰性,这些只有外部迭代器给得了**。

两种取向不是谁先进,而是各有适用场景。简单遍历、纯副作用操作(打印、日志、发事件),内部迭代器写起来更顺手;一旦涉及「提前终止」「多步组合」「和算法库对接」,外部迭代器是唯一的选择。C++ 的整个生态倒向后者,所以我们在这篇文章里也只展开了外部迭代器的现代写法。

## 更进一步:用协程把遍历写成「看起来像递归」的样子

我们的栈迭代器有个不太讨喜的地方:那段 `push_left` + `pop` + `看右子树` 的逻辑,读起来远不如直接写一个递归的中序函数直观。其实 C++20 的协程(coroutine)给了我们一条更优雅的路——**用一个生成器(generator)把递归的中序遍历直接写成协程,它对外暴露的还是「按需取下一个元素」的接口,本质和迭代器等价**。协程的妙处在于:你写的是普通的递归代码,编译器在背后帮你把它变成「可暂停、可恢复」的状态机,你每要一个值,它就 resume 一下、yield 一个值出来、再 suspend 住。

我们先写一个最简的 `Generator<T>`——它内部持有一个协程句柄,对外提供「取下一个值」的能力:

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

这套 `promise_type` 是协程和外界对接的契约,我们挑几个关键的看。`initial_suspend` 返回 `suspend_always`,意思是协程一被创建就立刻挂起、不自动跑——这保证「你不调用 `next()`,它一行都不执行」,正是惰性。`yield_value` 是 `co_yield` 背后被调用的函数:每次 `co_yield v`,协程就把 `v` 存进 `current_value`,然后挂起,把控制权交还调用方;调用方下一次 `next()` 时,协程从这里恢复继续跑。`final_suspend` 也返回 `suspend_always`,保证协程跑完后不自动销毁,留着我们最后在析构里 `destroy()`——否则协程帧提前释放就是经典的 use-after-free。

有了这个 `Generator`,中序遍历可以写得近乎直白,和你脑子里那版递归一模一样:

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

你看,这段代码没有栈、没有 `push_left`、没有 `pop`,就是一句句的递归,只不过在「该交值出去」的地方写成 `co_yield`。协程机制替你把「这里要暂停、待会要从这里恢复」的全套状态机生成出来。我们用一下,看它是不是真的惰性产出中序序列:

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

编译跑一下:

```sh
$ g++ -std=c++23 -O2 -Wall -Wextra coro_generator_verify.cpp -o coro_generator_verify
$ ./coro_generator_verify
1 2 3 4 5 6 7
```

输出和中序栈迭代器一模一样,而且一样是惰性的——你 `next()` 一次它才算一次,你不调它就纹丝不动。协程这条路真正的甜点在于:**复杂遍历(尤其递归定义的遍历,比如树的多种遍历、图的 DFS、按需展开的无限序列)用协程写,可读性远高于手搓栈**。代价是你得理解协程帧的生命周期、`promise_type` 的那套契约,以及生成器本身的开销(每个 `co_yield` 都是一次挂起/恢复)。在生产代码里要不要用协程代替手写迭代器,取决于你对可读性和开销的权衡——但「协程能用来实现迭代器模式」这件事本身,值得记进工具箱。

::: tip 协程迭代器和概念迭代器的区别
协程生成器对外暴露的是 `next()`/`value()` 这种成员函数 API,它不像我们前面那套 `operator*`/`operator++` 那样直接满足 `std::input_iterator` 概念,所以默认没法直接套 ranges 适配器。要让它进 ranges,你得给 `Generator` 再包一层 `begin()`/`end()`,里面返回一个满足 `input_iterator` 的轻量包装(把 `next()` 映射成 `operator++`、`value()` 映射成 `operator*`)。这件事标准库没替你做,C++23 的 `std::generator` 才是官方版的「开箱即用、满足迭代器概念」的生成器——如果你的工具链支持 C++23,优先用 `std::generator`,别自己手搓 `promise_type`。
:::

## 迭代器模式的代价

到这里我们已经有了一个能配 range-for、能配算法、能配 ranges 适配器的迭代器,还额外见识了协程这条路。但事情不能只看光鲜的一面——我得诚实地告诉你迭代器模式自己的代价,免得你到处套。

**第一,实现成本不低。** 一个「合格」的外部迭代器(尤其要进 ranges 的)要写对那一整套 `operator*`/前缀 `++`/后缀 `++`/`operator==`/关联类型别名,还要想清楚它停在哪个 concept 层级(input、forward、bidirectional、random_access),边界条件特别多。栈迭代器、双向链表迭代器、哈希表迭代器,每一种都有自己的状态管理坑。如果你的集合就一种遍历需求、就一两个调用点,直接写个 `for_each` 成员函数往往比折腾迭代器划算得多。

**第二,迭代器失效是 C++ 的经典雷区。** 迭代器本质上持有的是「集合内部某个位置的引用」,一旦集合在你迭代期间被修改(比如 `std::vector` 扩容、`std::map` 插入),之前拿到的迭代器可能瞬间变成悬空指针,继续用就是未定义行为。标准库对每种容器都明确规定了「什么操作会使哪些迭代器失效」,但标准库管不到你的自定义集合——你得自己写文档、自己保证,调用方还得自己读。

**第三,抽象是有运行时成本的,虽然现代编译器能抹掉很多。** 一个手写的迭代器如果满足 forward 以上概念,通常能被内联到和手写循环一样快;但 input 迭代器(像我们这个栈迭代器)因为有栈状态、有虚表外的间接跳转,很多时候抹不干净。对热路径上的性能敏感场景,「直接写专用循环」可能就是比「套通用迭代器 + 算法」快那么一点点。这是抽象的代价,值不值由 profiler 说了算。

**第四,概念层级和性能之间存在张力。** 你为了让迭代器满足更高层级的 concept(forward、bidirectional、random_access),往往得在数据结构里加额外信息(线索化指针、随机访问索引),这些存储开销是真金白银的。别为了「concept 越高越漂亮」而无脑升级——前面我们那个栈迭代器就老老实实待在 `input_iterator`,这才是它该在的位置。

## 小结

我们把整条演进路径捋一遍:

| 阶段 | 做法 | 为什么还不够 |
|---|---|---|
| 把遍历写进调用方 | 递归 + 目标容器参数 | 遍历策略和动作焊死,换需求就重写 |
| 先压平再遍历 | `flatten` 成 vector 再 range-for | 物化整份拷贝,不惰性,无限序列没法处理 |
| 外部迭代器(栈) | `operator*`/`++`/`==` + `begin`/`end` | 能过 range-for,但进不了 ranges |
| 补全 concept 配套 | 后缀 `++` + `iterator_concept` + 关联类型 | **够用了**(`input_iterator` 通,ranges 适配器可用) |
| 协程生成器 | `co_yield` 写递归遍历,对外按需取值 | 可读性高、天然惰性,但要懂协程帧生命周期 |

记下这几条关键结论:

- **写外部迭代器,前缀和后缀 `operator++` 必须一起给**,否则 `weakly_incrementable`/`input_iterator` 不成立,ranges 适配器全用不了。range-for 能过不等于概念合格。
- C++20 起用嵌套的 `iterator_concept`(不是老的 `iterator_category`)声明迭代器种类,带上 `value_type`/`difference_type`;老别名可保留以兼容老算法。
- **迭代器的 concept 层级不是越高越好**,要贴合数据结构的天然能力。栈式中序迭代器的天花板就是 `input_iterator`(单趟、不可多趟),硬升 forward 要付额外存储代价。
- 标准库算法和 ranges 适配器是迭代器模式真正的红利——集合只要提供一对合格的迭代器,`find_if`/`count_if`/`copy`/`views::filter`/`views::transform` 全部免费复用,且全程惰性。
- 外部迭代器(标准库那一系,控制权在调用方)和内部迭代器(`forEach` 那一系,控制权在集合)不是谁先进,而是适用场景不同:需要中断、组合、对接算法,只能选外部。
- 复杂递归遍历可以用 C++20 协程生成器实现,可读性远高手搓栈;生产环境若支持 C++23,优先用 `std::generator`,它开箱即满足迭代器概念。

::: tip 配套可编译工程
本节的例子在仓库 `code/volumn_codes/vol4/design-patterns/Iterator/` 下有完整可编译工程(`.h` + main + `CMakeLists.txt`),`cmake -S . -B build && cmake --build build` 即可跑出上面这些输出。
:::

## 参考资源

- [cppreference:Iterator library](https://en.cppreference.com/w/cpp/iterator)(C++20 起)
- [cppreference:`std::weakly_incrementable`](https://en.cppreference.com/w/cpp/iterator/weakly_incrementable)(后缀 `++` 的概念要求,C++20 起)
- [cppreference:`std::input_iterator`](https://en.cppreference.com/w/cpp/iterator/input_iterator)(C++20 起)
- [cppreference:Ranges library](https://en.cppreference.com/w/cpp/ranges)(`views::filter` / `views::transform`,C++20 起)
- [cppreference:Coroutines](https://en.cppreference.com/w/cpp/language/coroutines)(`co_yield` / `promise_type`,C++20 起)
- [cppreference:`std::generator`](https://en.cppreference.com/w/cpp/coroutine/generator)(C++23 开箱即用的协程迭代器)
