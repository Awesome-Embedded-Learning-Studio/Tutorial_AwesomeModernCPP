# stage2 · RingBuffer 施工讲义

> 老样子：你照着这份讲义手搓，黑盒测试由测试方（另一方）出。
> 位置：`include/tamcpp_ministl/ring_buffer.hpp`，接口草图在第 5 节，那是规格，测试按它写。

---

## 1. 这一站搓什么

固定容量的先进先出队列。场景想象一个黑匣子：飞机失事之后只关心坠毁前最后 30 秒的对话，所以记录器永远覆盖最老的数据，只留最近 N 条。嵌入式的循环日志、异常采样、示波器滚动窗口，全是同一个形状。

为什么不用现成的 Vector 做。因为出队要 `erase(begin())`，后面全家搬家，一次 O(n)。队列的两端都应该 O(1)，环形缓冲就是数组做到这件事的办法：不搬家，让「开头」绕圈跑。

本站主线语义（拍板）：**覆盖式**。`push` 永远成功，缓冲满时挤掉最老的元素。拒绝式（满则拒绝写入并报状态）留作变体练习，在主线跑通之后再做，改动量很小。

## 2. 核心机制：三件事

### 2.1 环形索引

数组是直的，队列是圈的。桥是一对只增不减的指针（其实是下标），叫 `read_pos` 和 `write_pos`。物理位置靠取模算出来：

```
物理下标 = 逻辑序号 % N
```

写入时写到 `write_pos % N`，然后 `write_pos` 加一。读出时读 `read_pos % N`，然后 `read_pos` 加一。指针自己永远往上走，绕圈这件事全部交给取模。数组开头和结尾在逻辑上被粘成一个圈，这是「环形」的全部秘密。

### 2.2 单调指针，永不回头

`read_pos` 和 `write_pos` 用 `std::uint64_t`，只增不减，永远不回绕到 0。直觉上会担心溢出，算一笔就放心了：`2^64` 大约是 `1.8 × 10^19`，就算每秒写入十亿次，也要写五百多年才会绕回来。这个设计换来的好处很大：空和满的判断变得极其干净（见第 4 节），不用维护任何额外状态。

### 2.3 位运算取模：`% N` 换成 `& (N-1)`

当 N 是 2 的幂时，`x % N` 和 `x & (N-1)` 恒等（x 非负）。位与是一条机器指令，除法在多数 CPU 上要几十个周期。

但先别急着手动换。有个实验值得做：N 是编译期常量（模板参数）的时候，写 `x % N` 编译器也会自动优化。动手看一眼：

```bash
g++ -std=c++23 -O2 -S -o- test.cpp | grep -A3 取模那行
```

两个小实验对比着做。`% 8`（2 的幂）会编译成 `and 7`。`% 6`（非 2 的幂）会编译成乘法加移位的组合（编译器用乘法逆元替代除法），不是一条指令，但也不是真除法。所以我们的立场是：

- 强制 N 为 2 的幂，用 `static_assert((N & (N - 1)) == 0)` 拦在编译期。
- 代码里直接写 `& (N-1)`。编译器就算替你优化了，写出来也是给读代码的人看：这里依赖 2 的幂。

真除法只在 N 是运行时变量时才会发生，我们的 N 是模板参数，享受编译期常量的全部特权。

## 3. 为什么复用而不重造

`RawBuffer<Sources>` 继续当圈地的工具，构造时圈 N 的地。`helper` 里的东西按需用。这一站的新概念包只有一个：环形机制本身。无堆的定容存储（栈上数组那种嵌入式风味）先不做，将来需要再说。

复用地基时会碰到一次「地基形状不匹配」，见第 6 节雷区清单，那是个好教学点，先自己碰一下再看。

## 4. 判空判满：三种解法，咱们选第三种

环形缓冲的经典雷区：`read_pos` 和 `write_pos` 相等的时候，是空还是满？转了一圈回到原点，两种状态长一个样。教科书上有三种解法：

1. **牺牲一格**。留一格不用，`write_pos + 1 == read_pos`（mod N）视为满。代价是容量少一格，回绕判断散落在代码里。
2. **计数器**。单独维护 `size_` 成员。直白，但每个操作多一次读写，而且多一份要保持同步的状态。
3. **单调索引**。指针只增不减、用 64 位永不回绕。空就是 `read_pos == write_pos`，满就是 `write_pos - read_pos == N`，两个整数减一下，完事。

咱们主线用第 3 种。它和 2.2 节是同一个决定的一体两面。解法 1 不是不好，它留给下一站 CircularDeque，那边的手艺（解环搬迁）跟它更配，到时候对照着看你会有感觉。

## 5. 接口草图（规格）

这就是黑盒测试的契约来源。命名跟 stage1 保持一个家族。

```cpp
template <typename Sources, std::size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "N must be a power of two");
    // 建议放在类内第一行，错误信息一眼可见

  public:
    RingBuffer();                        // 圈地 N（复用 RawBuffer 构造）
    ~RingBuffer();                       // 析构所有活元素（注意雷区 6.1）

    RingBuffer(const RingBuffer& other);
    RingBuffer& operator=(const RingBuffer& other);
    RingBuffer(RingBuffer&& other) noexcept;
    RingBuffer& operator=(RingBuffer&& other) noexcept;
    // 五件套照 stage1 的手艺，这一站当复习

    [[nodiscard]] bool empty() const;    // read_pos == write_pos
    [[nodiscard]] bool full() const;     // write_pos - read_pos == N
    [[nodiscard]] std::size_t size() const;  // write_pos - read_pos

    void push(Sources v);                // 满则挤掉最老的（覆盖式主线）
    void pop();                          // 出队，析构 front。空则 Check 崩（见下）
    Sources& front();                    // 空则 Check 崩
    const Sources& front() const;

    const Sources& visit_at(std::size_t logical) const;  // 逻辑序号取元素，越界 Check 崩
    Sources& visit_at(std::size_t logical);
};
```

三个设计决定，理由摆明：

- **pop 返回 void，不返回元素**。如果 pop 返回值，返回时的拷贝构造一旦抛出，元素已经从缓冲里删了，数据就没了。front 和 pop 分离，取值和出队两步走，每一步都出得起异常。标准库的 `std::queue::pop` 同款决定。
- **pop 空缓冲、front 空缓冲、visit_at 越界，全部 Check 崩**。按错误语义分家表，RingBuffer 是 Chromium 家的，前置条件违反就崩，不抛异常。测试会有对应的死法用例（`expect_check_death.sh` 同款流程）。
- **visit_at 的界是 size 不是 N**。空着的格子没有活着的对象，访问它们是未定义行为，Check 拦在前面。

## 6. 雷区清单

### 6.1 析构不是连续区间

stage1 的 `helper::DestroySources(begin, end)` 假设活元素是连续一段。环形布局下活元素绕圈，可能是两段。直接套会把绕回那半圈漏掉。怎么办自己定：按逻辑序号循环一遍，或者给 helper 加个环形变体。这是地基第一次伸腰，改法没有标准答案，测试只看结果（析构数目必须配平）。

### 6.2 覆盖发生时，最老元素的析构别漏

push 进满缓冲的顺序：先析构 `read_pos % N` 位置的最老元素，推 `read_pos`，再写入新元素，推 `write_pos`。顺序反了或者漏了析构，Census 普查员会当场把你揪出来。

### 6.3 拷贝的时候别把物理布局抄过去

拷贝构造应该按逻辑序号复制（从最老到最新依次 construct_at 到新缓冲的 0 号位起步），让新缓冲的 `read_pos` 从 0 开始。直接 memcpy 物理内存会连死格子一起抄，还会把对方的绕圈状态抄过来，然后两个对象的指针对不上。

### 6.4 clear 的语义

析构全部活元素，`read_pos` 和 `write_pos` 归零（或者归到同一个值），之后必须还能继续 push。这个跟 stage1 的 clear 是一个家族的要求。

## 7. 施工顺序（小步走）

1. 搭骨架。建 `stage2_ring_buffer/` 的目录结构，照 stage1 抄一份 CMakeLists，target 名换成 ring_buffer 系列。`ring_buffer.hpp` 先只放类定义和 static_assert，确认编译过。
2. int 定容版跑通。`RingBuffer<int, 8>`，push、pop、front、empty、full、size，先用 `% N` 写，main 里手推几个数验证绕回。这一步不看性能，只看逻辑对。
3. 换 `& (N-1)`，顺手做 2.3 节的汇编小实验。
4. 泛型化，五件套补齐，雷区三个逐个过。
5. 自检。自己写个临时 main，拿 Census 类（照 stage1 测试里那个抄）跑进出配平，绕回之后顺序不变，覆盖挤人正确。觉得稳了，叫测试方进场，黑盒测试会重新把这一切独立地再来一遍。

## 8. 验收预告（黑盒测试会盯什么）

- 绕回之后顺序不乱：push 超过 N 个，读出来的还是先进先出的序。
- 覆盖语义：连续 push N + 5 个，最老 5 个被挤掉，size 稳定在 N，front 是第 N + 1 个进去的。
- Census 配平：构造、析构、拷贝、移动的数目逐一核对，包括覆盖发生时被挤掉的那些。
- 空满边界：恰好在 N - 1、N、N + 1 时的行为。
- 死法用例：pop 空、front 空、visit_at 越界，三个都必须以 Check 崩收场。
- 编译期拦截：N 不是 2 的幂时 static_assert 必须开火。
