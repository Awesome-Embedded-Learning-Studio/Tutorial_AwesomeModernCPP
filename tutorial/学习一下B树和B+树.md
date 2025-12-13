# 学习一下B树和B+树

## 前言

​	抱歉这里鸽了5天，笔者最近忙于秋招找工作，连一些项目都被暂时的搁置了。这里笔者再干一个需要手搓B+树的题目中遇到了，虽然知道这个题大致需要使用B+树解决问题，但是发现自己好像对B/B+树实际上不够熟悉，这里笔者整理一下笔记放到下面，供各位参考和批评。

## 可以在哪里看到他们呢？

​	在数据库和文件系统中，B树（Balanced Tree）和B+树是核心的数据结构。它们通过多路平衡搜索，提供高效的查找、插入和范围查询能力。换而言之，在比较注重关系类型的查询的时候，我们很容易联想到使用B/B+树来进行关系的构建。

​	以文件系统为例子，我们可以非常自然的想到，我们的目录节点是一个非叶子节点（我指代的是下面还有孩子节点，对于二叉树，这个个数非常固定的就是2，这个无可厚非），下面存储若干的子节点（也不一定是叶子节点），所以我们的节点立马也就变成了这样的结构：

```cpp
    struct Node {
        bool isLeaf;
        std::vector<T> keys;	// T是一个给定的类型 
        std::vector<Node*> children;
        Node* next;
        Node(bool leaf) : isLeaf(leaf), next(nullptr) {}
    };
```

​	因此，在一个存在层次（使用目录的）的文件系统中，我们实际上就是查找由Node构成的B（+）树的结构。

```
[charliechen@Charliechen CChatRoom]$ tree .
.
├── client
│   ├── client_loader.cpp
│   └── CMakeLists.txt
├── CMakeLists.txt
├── library
│   ├── CMakeLists.txt
│   ├── simple_logger
│   │   ├── simple_logger.cpp
│   │   └── simple_logger.h
│   └── sockets_operation
│       ├── passive_client_socket.hpp
│       ├── socket_error.hpp
│       ├── sockets_common.hpp
│       ├── sockets_driver_impl.h
│       ├── sockets_driver_impl_linux.cpp
│       ├── sockets_driver_impl_linux.h
│       ├── sockets_operation.cpp
│       └── sockets_operation.h
├── server
│   └── CMakeLists.txt
└── test
    ├── CMakeLists.txt
    ├── test_client.cpp
    └── test_sockets.cpp
```

​	B树（B+树）这样的数据结构。下面我们来看看概念。

## B树（Balanced Tree）

B树是一种**多路平衡搜索树**，用于在磁盘或内存中高效存储大量数据。它保证**树的高度为对数级别**，因此查找、插入、删除操作的时间复杂度均为 $O(\log n)$。

​	我们要求，一个B树的特点必须是：

1. **多路节点**：一个节点可以有多个子节点，而不仅限于二叉。
2. **关键字有序**：节点内关键字按升序排列。
3. **所有叶子节点在同一层**：保证平衡。
4. **节点关键字和子树数约束**：
   - 阶数 M 的 B 树，每个节点关键字数在 $[⌈M/2⌉-1, M-1]$。**这样的约束是为了保证**
   - 子树数在 $[⌈M/2⌉, M]$。
5. **查找过程**：
   - 在节点关键字中查找目标值或确定进入哪颗子树。
   - 递归查找，直到叶子节点或找到目标。

#### 几个经典的操作

- **查找**：类似二分查找，沿着多路节点向下查找。
- **插入**：
  - 找到合适叶子节点。
  - 插入关键字。
  - 节点满时分裂，可能递归向上分裂。
- **删除**：
  - 删除关键字。
  - 节点关键字数不足时，向兄弟节点借或合并。

------

## B+树

#### 特点

​	B+树实际上就是B树的一点改进，由于非叶子节点不存储信息，直接查叶子节点就好了。

1. **非叶子节点只存索引**：不存储具体数据，节省空间。
2. **叶子节点存储所有数据**：并按照关键字顺序形成链表。
3. **查找路径固定**：所有查找操作最终都在叶子节点完成。
4. **范围查询高效**：叶子链表支持顺序扫描，方便扫描范围数据。
5. **多路平衡**：和B树类似，保持高度平衡。

| 特性         | B树                   | B+树                           |
| ------------ | --------------------- | ------------------------------ |
| 数据存储位置 | 非叶子节点 + 叶子节点 | 仅叶子节点                     |
| 非叶子节点   | 存索引和数据          | 仅存索引                       |
| 叶子节点     | 无链表                | 叶子链表支持顺序遍历           |
| 查找路径     | 命中可提前结束        | 必须到叶子节点                 |
| 范围查询     | 中序遍历              | 叶子链表顺序扫描               |
| 磁盘IO       | 查找路径短            | 内部节点更小，扇出大，总体IO少 |
| 应用场景     | 小型索引              | 数据库索引、文件系统           |

​	一个潜在的模板代码

```cpp
#include <iostream>
#include <vector>
#include <algorithm>

template <typename T, int M = 4>
class BPlusTree {
    struct Node {
        bool isLeaf;
        std::vector<T> keys;
        std::vector<Node*> children;
        Node* next;
        Node(bool leaf) : isLeaf(leaf), next(nullptr) {}
    };
    Node* root;

    std::pair<T, Node*> split(Node* node) {
        int mid = node->keys.size() / 2;
        Node* right = new Node(node->isLeaf);
        right->keys.assign(node->keys.begin() + mid, node->keys.end());
        node->keys.resize(mid);
        if (!node->isLeaf) {
            right->children.assign(node->children.begin() + mid + 1, node->children.end());
            node->children.resize(mid + 1);
        }
        if (node->isLeaf) {
            right->next = node->next;
            node->next = right;
        }
        return { right->keys[0], right };
    }

    std::pair<T, Node*> insert(Node* node, const T& key) {
        if (node->isLeaf) {
            auto it = std::upper_bound(node->keys.begin(), node->keys.end(), key);
            node->keys.insert(it, key);
        } else {
            int i = 0;
            while (i < node->keys.size() && key >= node->keys[i]) i++;
            auto ret = insert(node->children[i], key);
            if (ret.second) {
                node->keys.insert(node->keys.begin() + i, ret.first);
                node->children.insert(node->children.begin() + i + 1, ret.second);
            }
        }
        if (node->keys.size() >= M) return split(node);
        return { T(), nullptr };
    }

public:
    BPlusTree() { root = new Node(true); }

    void insert(const T& key) {
        auto ret = insert(root, key);
        if (ret.second) {
            Node* newRoot = new Node(false);
            newRoot->keys.push_back(ret.first);
            newRoot->children.push_back(root);
            newRoot->children.push_back(ret.second);
            root = newRoot;
        }
    }

    bool search(const T& key) {
        Node* cur = root;
        while (!cur->isLeaf) {
            int i = 0;
            while (i < cur->keys.size() && key >= cur->keys[i]) i++;
            cur = cur->children[i];
        }
        return std::find(cur->keys.begin(), cur->keys.end(), key) != cur->keys.end();
    }

    void printAll() {
        Node* cur = root;
        while (!cur->isLeaf) cur = cur->children[0];
        while (cur) {
            for (auto k : cur->keys) std::cout << k << " ";
            cur = cur->next;
        }
        std::cout << std::endl;
    }
};

int main() {
    BPlusTree<int,4> tree;
    for(int x: {10,20,5,15,25,30,1}) tree.insert(x);
    std::cout << "查找15: " << tree.search(15) << std::endl;
    std::cout << "查找7: " << tree.search(7) << std::endl;
    std::cout << "顺序遍历: ";
    tree.printAll();
}

```

