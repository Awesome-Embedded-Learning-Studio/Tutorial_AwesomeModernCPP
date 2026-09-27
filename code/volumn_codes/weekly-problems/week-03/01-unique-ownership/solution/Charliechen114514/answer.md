**思路**:

能编译的是第 2、3 条:`auto q = std::move(p);` 和 `int* raw = p.get();`。过不了的两条,编译器给的拒绝理由都值得认识一下。

`auto q = p;` 在 GCC 上的报错是「use of deleted function 'std::unique_ptr<_Tp, _Dp>::unique_ptr(const std::unique_ptr<_Tp, _Dp>&)'」——注意措辞:**deleted**。拷贝构造不是「没写」,是被显式删除了。这是 `unique_ptr` 的设计核心:它承诺所有权独占,而拷贝会产生第二个持有者,直接和承诺冲突,所以标准库把拷贝构造声明成 deleted。为什么非删不可,配着第 4 条看最清楚。

`std::shared_ptr<int> s = p;` 的报错(GCC:「conversion from 'std::unique_ptr<int, ...>' to non-scalar type 'std::shared_ptr<int>' requested」)是同一件事的另一面:`shared_ptr` 确实有一个从 `unique_ptr` 转换的构造函数,但它的参数是**右值引用**——它只接受持有者明确表示要交出的 `unique_ptr`。`p` 是左值:没有 `std::move`,就没有「同意移动」的表态,这个构造函数就不会被选中。想转,写 `std::move(p)`,转完 `p` 就交出去了。所有权转移必须显式写出来,不能靠隐式转换悄悄发生。

`p.get()` 能编过,恰恰是它的设计用途:裸指针借出来看看(`*raw`、传给 C 接口)没问题,所有权纹丝不动。借出方要守的规矩是:不许对它 `delete`,指针指向的内存归 `unique_ptr` 管。

**排错**:

最常见的误选是把第 4 条当能编——直觉里「`shared_ptr` 什么指针都能装」。它确实什么裸指针都能装,但装 `unique_ptr` 只收右值,理由见上。

另一个方向的误会:以为 `get()` 出来的裸指针能接管内存。能编译不等于该这么用——`delete raw` 之后 `unique_ptr` 析构时再来一次 `delete`,double free。
