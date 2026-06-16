# faq.md — C++ 常见误解速查

> 给学习者和他们的 agent:C++ 里"看着对、其实错"的高频坑。
> 每条 = **误解 → 真相 → 最小示例**。这是个**持续补充**的活文档,种子条目如下。

## 语言机制

### `auto` 会保留 const / 引用性

**误解**:`auto x = something;` 保留 `something` 的 const 和引用。

**真相**:`auto` 走模板参数推导规则,**剥掉顶层 const 和引用**,得到的是副本。

```cpp
int v = 1;
const int& r = v;
auto a = r;          // a 是 int(副本),const 和 & 都没了
const auto& b = r;   // b 才是 const int&

for (auto x : vec) { /* 每个元素都拷贝! */ }
for (const auto& x : vec) { /* 不拷贝 */ }
```

要保留,用 `auto&` / `const auto&` / `decltype(auto)`。
参见:卷二·现代特性(vol2-modern-features)。

### `Widget w(Bar());` 是构造对象

**误解**:这行构造了一个 Widget。

**真相**:这是**最恼人解析(Most Vexing Parse)** —— 编译器把它读成"一个叫 `w`、接受(返回 `Bar` 的函数)、返回 `Widget` 的**函数声明**"。

```cpp
Widget w(Bar());     // 声明了一个函数,不是对象!
Widget w{Bar()};     // 这才是对象(花括号初始化)
Widget w((Bar()));   // 加括号也是对象
```

参见:卷一·基础(vol1-fundamentals)。

### `int x{3.14}` 能编译

**误解**:花括号初始化和圆括号一样,会隐式收窄。

**真相**:花括号初始化**禁止窄化**。

```cpp
int x(3.14);   // OK,隐式截断为 3
int y{3.14};   // 编译错误
```

## 资源与移动

### `std::move` 就是"移动"

**误解**:`std::move(x)` 把 x 的内容搬走了。

**真相**:`std::move` 只是个**转成右值引用的 cast**,本身什么都不搬。真正搬不搬,取决于类型有没有移动构造/赋值;没有就回退到拷贝。

```cpp
void sink(std::string&& s) {
    std::string local = s;           // 拷贝!s 在这里是左值
    std::string moved = std::move(s); // 这才真移动
}
```

两个连带坑:

- **命名的右值引用是左值**:参数 `T&& s`,用 `s` 时是左值,想再搬得再 `std::move(s)`。
- **对 `const` 对象 `std::move` → 拷贝**:`std::move` 得到 `const T&&`,移动构造要非 const,于是匹配拷贝,"移动"悄悄变拷贝。

参见:卷二·现代特性(vol2-modern-features)。

## 面向对象

### 基类析构不用 virtual

**误解**:只要不手动 delete,基类析构写不写 `virtual` 无所谓。

**真相**:通过基类指针/引用 `delete` 派生对象,若基类析构非 `virtual`,是**未定义行为**(典型后果:派生部分不析构 → 资源泄漏)。**有虚函数的类,析构就该 virtual。**

```cpp
struct Base { virtual void f(); /* 缺 virtual ~Base() */ };
struct Derived : Base { std::ofstream file; /* 析构关文件 */ };

Base* p = new Derived;
delete p;   // UB:Derived::~Derived 没被调用,file 没关
```

参见:卷一·基础(vol1-fundamentals)。

## 容器

### `push_back` 后,之前的引用/迭代器还有效

**误解**:我拿到的 vector 元素引用或迭代器,后面继续 `push_back` 也没事。

**真相**:`push_back` 触发扩容(容量变化)时,**所有引用和迭代器全部失效**。要跨修改持有,用下标或先 `reserve`。

```cpp
std::vector<int> v = {1, 2, 3};
int& first = v[0];
v.push_back(4);   // 可能扩容 → first 失效,再用是 UB
```

各操作的失效规则不同(如 `erase` 只失效被删及之后的),改动容器前先查规则。
参见:卷三·标准库(vol3-standard-library)。

## 未定义行为

### "我这里跑得好好的,所以代码没错"

**误解**:程序输出了预期结果,代码就是对的。

**真相**:**未定义行为(UB)** —— 有符号整数溢出、越界访问、未初始化读取、违反 ODR、数据竞争等 —— 在 `-O0` 下可能"碰巧"正常,在 `-O2` 下可能整个崩、或被编译器直接优化掉。**不能靠"能跑"证明正确**,要用工具(ASan / UBSan / TSan)+ 按标准理解行为。

```cpp
int a = INT_MAX;
int b = a + 1;   // 有符号溢出 = UB,不是"绕回"
```

参见:卷六·性能(vol6-performance)、卷七·工程(vol7-engineering)、卷五·并发的数据竞争部分(vol5-concurrency)。
