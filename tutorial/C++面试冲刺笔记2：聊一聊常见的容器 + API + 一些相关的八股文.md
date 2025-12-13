# C++面试冲刺笔记2：聊一聊常见的容器 + API + 一些相关的八股文

## 前言

​	笔者这里整理了一些常见的C++容器，他们的API和一些相关的八股文，放在这里的理由实际上非常的简单，是因为笔者当时刷Leetcode Hot100的时候，凡是涉及到同hashmap，一些并查集的题目的时候，会对标准库已经拥有的API实际上并不熟悉，导致很多时候我还要去现查API。这非常的耽误时间，笔者在这里就决定整理一份笔记备忘，之后看自己的笔记进行复盘了。

# 🌟容器概览

​	C++给我们的轮子非常非常的多：

| 类别                     | 容器（头文件）                                               | 说明                                 |
| ------------------------ | ------------------------------------------------------------ | ------------------------------------ |
| **序列容器**             | `vector`, `deque`, `list`, `forward_list`, `array`, `string` | 提供线性顺序的存储结构               |
| **关联容器（有序）**     | `set`, `multiset`, `map`, `multimap`                         | 基于红黑树，实现自动排序的键值存储   |
| **无序关联容器（哈希）** | `unordered_set`, `unordered_multiset`, `unordered_map`, `unordered_multimap` | 使用哈希表实现的快速查找结构         |
| **容器适配器**           | `stack`, `queue`, `priority_queue`                           | 基于序列容器封装，提供特定的接口行为 |

## 1. 序列容器

### `std::vector`

- **函数分类**：
  - 构造、赋值、析构
  - 随机访问： `at()`, `operator[]`, `front()`, `back()`
  - 迭代器： `begin()`, `end()`, 逆向版本等
  - 容量： `size()`, `capacity()`, `empty()`, `reserve()`, `shrink_to_fit()`
  - 修改： `push_back()`, `pop_back()`, `insert()`, `erase()`, `clear()`, `swap()`

### `std::deque`

- 类似 vector，但支持前端插入删除。（也就是push_front和pop_front，为了让队列正向流动，我们习惯搭配的是push_back和pop_front让队列流起来）
- 方法分类与 vector 基本相同

### `std::list`, `std::forward_list`

- `list` 是双向链表，`forward_list` 是单向链表。
- 支持快速任意位置插入删除：`push_front()`, `push_back()`, `insert()`, `erase()` 等

### `std::array`

- 固定大小的数组，支持 `at()`, `operator[]`, `fill()`, 迭代器、`swap()` 等

### `std::string`

- 专门的动态字符序列，成员：
  - 构造/赋值/析构
  - 字符访问： `at()`, `operator[]`, `front()`, `back()`, `c_str()`, `data()`
  - 迭代器： `begin()`, `end()`, `rbegin()`, `rend()`, 常量版本
  - 容量： `size()`, `length()`, `max_size()`, `capacity()`, `reserve()`, `shrink_to_fit()`, `empty()`
  - 修改： `clear()`, `insert()`, `erase()`, `push_back()`, `pop_back()`, `append()`, `operator+=()`, `replace()`, `resize()`, `swap()`
  - 查找： `find()`, `rfind()`, `find_first_of()`, `find_last_of()`, `find_first_not_of()`, `find_last_not_of()`, `compare()`

------

## 2. 关联容器（有序）

容器类型：`set`, `multiset`, `map`, `multimap`

- **公共成员**：构造/赋值/析构、迭代器、容量 (`size()`, `empty()`, `max_size()`)、交换 `swap()`、获取 `get_allocator()`。
- **修改操作**：`insert()`, `emplace()`, `emplace_hint()`, `erase()`, `clear()`。
- **查找/范围**：`find()`, `count()`, `lower_bound()`, `upper_bound()`, `equal_range()`。
- **观察者**：`key_comp()`, `value_comp()`（仅 `map`/`set`）
- 额外：`map` 有 `operator[]` 和 `at()`。

------

## 3. 无序关联容器

类型：`unordered_set`, `unordered_multiset`, `unordered_map`, `unordered_multimap`

- 继承关联容器接口，但通过哈希实现。
- 增加：`reserve()`, `bucket_*()`, `load_factor()`, `max_load_factor()`, `rehash()`, `hash_function()`, `key_eq()`。
- `unordered_map` 支持 `operator[]`, `at()`

------

## 4. 容器适配器

### `std::stack`

- 后进先出。接口： `push()`, `pop()`, `top()`, `empty()`, `size()`

### `std::queue`

- 先进先出。接口： `push()`, `pop()`, `front()`, `back()`, `empty()`, `size()` 。

### `std::priority_queue`

- 基于堆的优先队列。接口： `push()`, `pop()`, `top()`, `empty()`, `size()` 。

------

## 🔧 使用建议

- **需要快速随机访问** 和尾部操作时，优先使用 `vector`。
- **频繁中间插入删除**，且不关心随机访问，使用 `list` 或 `forward_list`。
- **有序查找**，使用 `map` 或 `set`。
- **对性能敏感、需快速哈希查询**，推荐 `unordered_map` / `unordered_set`。
- **特定的逻辑顺序**，选择容器适配器如 `stack`/`queue` 或 `priority_queue`

## 一些example usage

### 从deque说起

​	这样的原因很简单，因为笔者认为，做过项目的朋友应当对vector和list是非常熟练了，没有必要自己折磨自己还做重复工，剩下的部分大家可能没有那么熟练，因此，咱们就从deque慢慢聊。

​	关于deque，我们知道他是一个双端队列，支持两端的操作。

> 关于两者的区别和优缺点，笔者不打算在这里聊，因为有点多。

```cpp
void iterate_print(const std::deque<int>& deque) {
	for (const auto& each : deque) {
		std::cout << each << " ";
	}
	std::cout << std::endl;
}

void exercise1() {
	std::deque<int> test;

	test.push_back(10);
	test.push_front(5);
	test.push_back(15);

	std::cout << "front: " << test.front() << " back:" << test.back() << "\n";

	iterate_print(test);
}
```

​	deque的前后端插入如你所见，就是这样的简单，您可以拷贝这个实例自己运行做一下，可以删除一些元素，对了，记得删除之前最好按照自己的习惯判一下空，Leetcode的报错信息几乎从来没说过人话。

```cpp
void exercise2() {
	// initialize_list init
	std::deque<int> result = { 1, 2, 3, 6, 7 };
	// find the 3 first
	auto it = std::find_if(result.begin(), result.end(), [](const int& val) -> bool {
		return val == 6;
	});

	if (it == result.end())
		throw std::invalid_argument("Can not find 6");

	result.insert(it, 5);
	result.emplace(it, 4);

	iterate_print(result);
}
```

​	insert和emplace都是插入，但是我们知道，后者的效率一般会比前者稍微高一些，这是因为后者往往属于就地构造，而不需要额外的拷贝（大对象这招很管用）。需要注意的是，我们插入元素的时候，实际上会向给定的元素前插，因此这是我们需要注意的，之后插入的时候不要搞错了地方。

​	当然，删除的时候，就是使用erase()，注意的是我们传递的是迭代器，这个事情很简单，直接对容器的begin上偏移的index就完事了。

### 再说说unordered_map

​	unordered_map是这样的一个简单的，无序的键值对容器，这里也就提示你，遍历的时候拿到的是键值对，这个容器很好用，特别是人家要求你`O(1)`时间查找的时候，`unordered_map`就要派上用场。下面这个小demo就是一个unordered_map的常用API的使用的一个小小的样例。

```cpp
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
static const char* TEXT = "To be, or not to be, that is the question:"
                          "Whether 'tis nobler in the mind to suffer"
                          "The slings and arrows of outrageous fortune,"
                          "And to take arms against a sea of troubles"
                          "And by opposing end them. To die—to sleep,"
                          "No more; and by a sleep to say we end"
                          "The heart-ache and the thousand natural shocks"
                          "That flesh is heir to: 'tis a consummation"
                          "Devoutly to be wished. To die, to sleep;"
                          "To sleep, perchance to dream—ay, there's the rub:";

void get_splity_words(std::vector<std::string>& result) {

	std::string str = TEXT;
	str.erase(std::remove_if(str.begin(), str.end(), [](const char ch) -> bool {
		return ::ispunct(ch);
	}), str.end()); // clean datas

	for (auto& ch : str) {
		ch = ::tolower(ch);
	}

	std::istringstream iss(str);
	std::string each_piece;
	while (iss >> each_piece) {
		result.emplace_back(each_piece);
	}
}

int main() {

	std::vector<std::string> result;
	get_splity_words(result);

	std::unordered_map<std::string, int> countMap;

	for (const auto& each : result) {
		countMap[each]++;
	}

	std::cout << "to: " << countMap.at("to") << std::endl;

	for (const auto& pair : countMap) {
		std::cout << pair.first << ": " << pair.second << std::endl;
	}
}
```

> 补充一下，一些朋友需要分割字符串，如果是自然分割的字符串，您完全使用istringstream就OK，如果是自定义分割，那也很简单，直接咱们走起一个`while(getline(istringstream_obj, cont_string, spilter))`就行，其中，istringstream_obj是一个经典的输入流，cont_string是一个承接结果的字符串容器，spliter是我们要求的分割符。

### unordered_set

​	unordered_set本身是一个无序的并查集，set本身最大的工作是去重和检测去重。或者我们说：

- **去重**：将 vector 放入 `unordered_set` 自动删除重复元素，再转回 vector。
- **存在判定**：遍历序列时调用 `find()`/`count()` 判断元素是否已见。
- **滑动窗口**：在窗口中维护 `unordered_set`，用 O(1) 时间检查重复或限制窗口。
- **双指针去重**：如“两数之和”，将已见左侧元素存入 `unordered_set`，右侧遍历时查找 complement。
- **图算法 BFS/DFS**：追踪“已访问节点”，避免重复入队。
- **唯一字符检查**：检查字符串是否所有字符各不相同（ASCII 字符可简单实现）。

```cpp
#include <algorithm>
#include <cctype>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

static const char* TEXT = "To be, or not to be, that is the question:"
                          "Whether 'tis nobler in the mind to suffer"
                          "The slings and arrows of outrageous fortune,"
                          "And to take arms against a sea of troubles"
                          "And by opposing end them. To die—to sleep,"
                          "No more; and by a sleep to say we end"
                          "The heart-ache and the thousand natural shocks"
                          "That flesh is heir to: 'tis a consummation"
                          "Devoutly to be wished. To die, to sleep;"
                          "To sleep, perchance to dream—ay, there's the rub:";

void get_splity_text(std::vector<std::string>& result) {
	std::string tr = TEXT;

	// remove punctual
	tr.erase(std::remove_if(tr.begin(), tr.end(), [](const char ch) -> bool {
		         return ::ispunct(ch);
	         }),
	         tr.end());

	for (auto& ch : tr) {
		ch = ::tolower(ch);
	}

	std::istringstream ss(tr);
	std::string container;
	while (ss >> container) {
		result.push_back(container);
	}
}

int main() {
	std::unordered_set<std::string> sets;
	std::vector<std::string> result;
	get_splity_text(result);

	for (const auto& each : result) {
		if (sets.insert(each).second) {
			std::cout << "Insert the " << each << std::endl;
		} else {
			std::cout << "Already has the " << each << std::endl;
		}
	}

	std::cout << "There are " << sets.size() << " elements";
	for (const auto& each : sets) {
		std::cout << each << ": " << sets.count(each) << std::endl;
	}
}
```

​	剩下的两个我认为可以直接不说了，实际上这里的multi就是允许重复登记的意思，很简单的。使用count接口就能查到重复登记了多少。还是很简单的事情。

# 一些底层八股文

### 1.1 `std::set<Key>`

- **底层实现**：红黑树（self‑balancing binary search tree），是一种确保 **平衡高度** 的二叉搜索树结构 
- **属性**：
  - 键唯一（duplicate keys 不可插入），自动排序（通常为升序）。
  - 提供有序遍历（`begin()`/`end()`）、范围查找（`lower_bound` / `upper_bound`）等功能 。
- **时间复杂度**：搜索、插入、删除均为 **O(log N)**，最坏情况也有此保障（红黑树属性） 。
- **适用场景**：需要维护有序唯一元素，如输出排序结果、查找前驱/后继、范围统计等。

------

### 1.2 `std::map<Key, T>`

- **底层实现**：与 `std::set` 相同，也基于红黑树实现 
- **属性**：
  - 存储键值对（key→value），key 唯一且自动有序。
  - 支持遍历（`for(auto& [k,v]: map)`）、范围查找、排序遍历等。
- **与 `set` 的区别**：
  - `set` 只存 key，存储元素本身；`map` 存储 key 和关联 value。
  - `map` 支持访问 value（如 `map[key]`、`at()` 等），但 key 不能修改 
- **时间复杂度**：插入/查找/删除均为 **O(log N)**。

------

### 1.3 `set/map` 和 `unordered_set/unordered_map` 的对比

| 特性         | `set` / `map`                          | `unordered_set` / `unordered_map` |
| ------------ | -------------------------------------- | --------------------------------- |
| 底层结构     | 红黑树（自平衡BST）                    | 哈希表                            |
| 元素存储     | 有序（自动排序）                       | 无序                              |
| 唯一性       | `set`:唯一 key；`map`:唯一 key + value | 唯一 key（value 可重复在 map 内） |
| 常数平均时间 | O(log N)                               | O(1)（平均）                      |
| 最坏情况时间 | O(log N)                               | O(N)（哈希冲突）                  |
| 内存占用     | 稍高（指针、颜色标记等）               | 较高（桶 + 链表/数组）            |
| 适用场景     | 需要排序、范围查询、前驱/后继等        | 只需快速查询、不需要顺序          |

- `unordered_*` 平均速度快很多，但最坏情况性能无法保证，而且不支持有序操作
- 在题目对“排序”“序”有要求时，应选 `set/map`；一旦只需查找或去重，则更推荐 `unordered_set/map` 

------

### 1.4 面试常见问法

- **底层比较**：为什么选择红黑树而不是其它树？（红黑树插入/删除旋转次数少于 AVL，标准库广泛使用） 
- **复杂度差异**：`set/map` 的 O(log N) 与哈希表的 O(1) 平均差异及应用判断标准。
- **空间与内存效率**：红黑树 vs 哈希表的存储结构、缓存友好性差异。
- **什么时候用有序 vs 无序容器？** 根据功能需求选择：如前驱/后继查询、范围查找、排序输出等需要有序，则选 `set/map`；否则尽量用 `unordered_*`，尤其在求解算法题中常见的查重、判重场景 。
- **`map` 代替 `set` 的必要性**：当需要存储关联值时使用 `map`，反之则用 `set` 更轻量。

------

## ✅ 小结

- **`set`**：无需关联值，只需唯一、有序元素，支持范围查找和前后邻居操作。
- **`map`**：需键→值映射，支持通过 key 快速检索和遍历。
- **二者底层均为红黑树，时间复杂度稳定为 O(log N)。**
- **与哈希容器相比**：选择排序逻辑 vs 查询速度、空间差异与应用场景决定了用哪种。

如果你想进一步了解红黑树旋转机制，或将这些容器应用到具体算法题（比如滑动窗口中使用多重 `multiset` 平衡中位数），都可以深入聊！