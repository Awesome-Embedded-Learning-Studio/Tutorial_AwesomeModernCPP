---
chapter: 12
cpp_standard:
- 20
description: C++20 管道操作符 | 把 filter/transform 等视图适配器串成一条惰性管道,左结合、等价于嵌套函数调用,迭代时逐元素流过;view 不拥有数据、源悬空就废,自定义类型提供 begin/end 即可接入,物化进容器用 iterator-pair 构造。
difficulty: intermediate
order: 8
platform: host
prerequisites:
- 'C++20 范围库基础与视图'
reading_time_minutes: 14
related:
- 'C++20 范围库基础与视图'
- '指定初始化器'
tags:
- host
- cpp-modern
- intermediate
- Ranges
title: '管道操作与 Ranges 实战'
---
# 管道操作与 Ranges 实战

上一篇咱们看了视图(view)是惰性的、不拥有数据的轻量句柄。但单个视图只做一件事,真正顺手的是把好几个视图串成一条流水线,前一步的输出直接喂给下一步。Unix 管道 `cat data | grep pattern | sort` 就是这套思路,每个程序只干一件事,串起来就完成一整套活。C++20 把这套写法搬进了语言,靠的就是重载后的管道操作符 `|`。

这一篇咱们把管道的语义讲清楚(它就是嵌套函数调用的另一种写法),再用 ADC 采样处理和协议字节解析两个场景演示实战,最后把自定义类型怎么接进管道、以及 view 不拥有数据这条最容易踩的坑说明白。

## 管道 `|`:嵌套调用的另一种写法

先把语义讲准。下面两段代码是等价的:

```cpp
std::vector<int> data{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

// 管道写法:从上往下读,像句子
auto pipe = data
    | std::views::filter(is_even)
    | std::views::transform(times10);

// 嵌套调用写法:从里往外读,层级一多就难看
auto nested = std::views::transform(std::views::filter(data, is_even), times10);
```

管道 `|` 是左结合的,`a | f | g` 解析成 `(a | f) | g`,正好对应 `g(f(a))`。它本质就是函数调用,只不过写成横向流水线更好读。咱们跑一下确认两者结果一致:

<OnlineCompilerDemo allow-run
  title="管道写法与嵌套调用写法等价"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipe_basics.cpp"
  description="同一段 filter+transform 用管道和嵌套调用两种写法,输出完全相同,验证 | 左结合等价于函数嵌套。"
/>

```text
pipe:  20 40 60 80 100
nested:20 40 60 80 100
```

## 整条管道是惰性的

上一篇说过单个视图是惰性的,管道把这个特性保留下来:构建管道时一步都不执行,只有您去迭代结果时,数据才会逐个流过这条链。咱们用一个带计数器的 lambda 来证明。

```cpp
std::vector<int> data{1, 2, 3, 4, 5};
int filter_calls = 0, transform_calls = 0;

auto pipe = data
    | std::views::filter([&](int x){ ++filter_calls; return x % 2 == 0; })
    | std::views::transform([&](int x){ ++transform_calls; return x * 10; });
```

`pipe` 这一行执行完,两个计数器都还是 0。直到 `for (int x : pipe)` 真正开始迭代,filter 和 transform 才被调用。

<OnlineCompilerDemo allow-run
  title="惰性证明:构建时不执行,迭代时才跑"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipe_lazy_and_dangling.cpp"
  description="带计数器的 lambda 显示管道构建阶段 filter/transform 调用次数都是 0,只有 for 循环开始后才非零。"
/>

```text
[构建前]  filter=0 transform=0
[构建后,迭代前] filter=0 transform=0
[迭代后]  filter=5 transform=2 产出=2 个元素
```

注意 `filter` 跑了 5 次(5 个元素全过了一遍 predicate),`transform` 只跑了 2 次(只有 2 个元素通过了 filter)。整条管道是**单遍**的:一个元素从源头出发,顺着 filter、transform 一路流到底,中间不落地、不存中间 vector。这也是它比"先 `copy_if` 再 `transform`"省内存的原因。

## view 不拥有数据:这是最容易踩的坑

这条必须单独拎出来。`filter`、`transform` 这些视图**不拷贝、不持有源数据**,只存一个对源的引用。源容器还在,视图就有效;源一旦没了,视图就成了悬垂引用。

最常见的翻车场景,是函数里建一个局部 vector,然后把它的视图返回出去:

```cpp
auto make_dangling_view() {
    std::vector<int> local{1, 2, 3, 4, 5};   // 函数返回时销毁
    return local | std::views::filter([](int x){ return x > 2; });
}
```

这代码能编过(view 类型能推导出来),但拿到的视图指向的是已释放的 vector 内存,迭代它是**未定义行为**。同样的道理,把视图建立在临时量上也悬垂:

```cpp
auto bad = std::vector<int>{1, 2, 3}        // 临时量,这行结束就销毁
    | std::views::filter([](int x){ return x > 1; });
```

::: warning view 只是个引用,别让它比源活得长
管道返回的 view 不拥有数据,生命周期跟着源容器走。需要把结果长期保存,就用 `std::vector<float>(pipe.begin(), pipe.end())` 这种 iterator-pair 构造把它物化进一个真正持有数据的容器。这是 C++20 通用做法,笔者后面还会再提。
:::

## 实战:ADC 采样的多级处理

这套管道写法最自然的落点,是传感器数据的多级清洗。ADC 出来的原始采样,往往要丢掉越界噪声、转成电压、再套一条校准曲线。三步用三个 transform/filter,串成一条管道:

```cpp
struct AdcSample { std::uint16_t raw; };

std::vector<AdcSample> samples = fetch_samples();   // 一帧采样

auto pipeline = samples
    | std::views::filter([](const AdcSample& s){
        return s.raw >= 64 && s.raw <= 4000;       // 丢掉越界噪声
    })
    | std::views::transform([](const AdcSample& s){
        return s.raw * 3.3f / 4095.0f;             // 原始值 -> 电压
    })
    | std::views::transform([](float v){
        return 1.001f * v + 0.0002f * v * v;        // 二阶校准曲线
    });
```

每一步职责单一,加一步就往管道上接一行,调试时想跳过校准,注释掉那行 transform 就行。要长期持有结果(比如存进一个缓冲),就用 iterator-pair 物化:

```cpp
std::vector<float> kept(pipeline.begin(), pipeline.end());
```

<OnlineCompilerDemo allow-run
  title="ADC 多级管道:过滤 -> 转电压 -> 校准"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipe_adc.cpp"
  description="模拟一帧 ADC 采样(含越界噪声),管道三段处理后用 iterator-pair 物化进 vector。"
/>

```text
校准后电压: 0.8262 2.4212 1.6526 2.8249
物化进 vector 的样本数:4
```

8 个原始样本,4 个被越界过滤掉,剩下 4 个走完校准。

## 实战:协议字节流解析

另一类常见活是从字节流里拼出 16 位字。这里有个 C++20 的边界要先说清楚:原文常见的写法是用 `std::views::chunk(2)` 把字节两两分组,但 `chunk` 是 **C++23** 才进标准的,在 `-std=c++20` 下编不过(GCC 16 实测会报 `'chunk' is not a member of 'std::views'`)。C++20 里咱们换个不依赖 `chunk` 的写法,用 `iota` 生成下标再两两取:

```cpp
std::vector<std::uint8_t> bytes = receive_spi_data();   // 大端字节流

// 配对相邻字节,拼成 16 位字(C++20 友好,不用 chunk)
auto words = std::views::iota(std::size_t{0}, bytes.size() / 2)
    | std::views::transform([&](std::size_t i){
        std::uint16_t hi = bytes[i * 2];
        std::uint16_t lo = bytes[i * 2 + 1];
        return static_cast<std::uint16_t>((hi << 8) | lo);
    });

// 丢掉 0xFFFF 填充字
auto valid = words | std::views::filter([](std::uint16_t w){ return w != 0xFFFF; });
```

`iota` 生成的下标本身就是惰性的、不占内存,管道一接就把"取下标"和"拼字"串在了一起。

::: warning views::chunk / slide / stride 是 C++23
分组、滑窗这类适配器得开 `-std=c++23`。C++20 项目里要用,自己用 `iota + transform` 拼,或者升级到 C++23。另外想用一个适配器前,先确认它在您目标编译器上实现完整,GCC 10 才开始有 ranges,部分适配器是后续版本补齐的。
:::

## 自定义类型怎么接进管道

嵌入式里咱们常有自己的容器类(环形缓冲区、采样窗口、寄存器映射)。想让它们也能用 `data | views::filter(...)`,门槛比想象中低:**只要这个类型是一个 range,也就是提供 `begin()` 和 `end()`,它就能直接出现在管道左边**。管道内部会把它包成一个 `views::all`,拿到首尾迭代器。

下面这段定义一个 `IntWindow`,内部只是一段连续 int 的指针加长度:

```cpp
class IntWindow {
public:
    IntWindow(const int* p, std::size_t n) : p_(p), n_(n) {}
    const int* begin() const { return p_; }      // 这两个就够了
    const int* end()   const { return p_ + n_; }
    std::size_t size() const { return n_; }
private:
    const int* p_;
    std::size_t n_;
};

int raw[] = {10, 15, 20, 25, 30, 35, 40};
IntWindow window(raw, 7);

// 自定义类型直接接进管道
auto out = window
    | std::views::filter([](int x){ return x > 18; })
    | std::views::transform([](int x){ return x / 5; });
```

这种"给类型装上 begin/end"的写法,对绝大多数嵌入式场景够用了。如果还想做自己的适配器(像 `filter` 那样能写在 `|` 右边的东西),那就得实现 Range Adaptor Object,涉及更多模板,留到需要时再展开。

<OnlineCompilerDemo allow-run
  title="自定义类型接进管道 + 与手写循环对比"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipe_custom_and_perf.cpp"
  description="IntWindow 提供 begin/end 后直接进管道;管道写法与手写循环产出一致,且惰性单遍无中间 vector。"
/>

## 性能:管道真的不拖后腿

既然是惰性单遍、又只存轻量句柄,管道写法和手写循环相比开销到底怎样?笔者做一个 200 万元素的对照:老写法先 `copy_if` 建一个中间 vector、再 `transform` 累加;管道写法一条链到底直接累加。

```text
手写循环累加 = 3999997997450
管道写法累加 = 3999997997450
两者一致:是
手写循环:23133 us(20 次平均)
管道写法:12812 us(20 次平均)
管道更快:老写法建了中间 vector,惰性单遍省了分配和拷贝
```

两组结果完全一致,管道写法反而更快。原因是 `-O2` 下编译器把整条管道的 lambda 全部内联,数据单遍流过,而老写法额外建了中间 vector(一次分配加一次拷贝)。`-Wall -Wextra` 下干净编译,没有任何 warning。

当然这里有个前提:管道是单遍的、且数据量不大时编译器能看清全链。要是您中途非要把某一段物化成 vector 再继续,那笔分配的开销就回来了。所以笔者的判断标准很简单:**能一条管道走到底的,就别中途落地**;非要落地,物化一次够用就行,别每一步都存中间结果。

## 几条避坑要点

**别指望迭代同一管道会"缓存"**。前面惰性实验里,for 循环跑完一遍,filter 的计数器从 0 涨到 5;再跑第二遍,filter 还会再跑一遍,计数器继续涨。多数视图(`filter`、`transform`、`take` 这些)**不缓存结果**,每次迭代都重新求值。源稳定时多迭代几遍结果都一样(只是多算一遍),但像 `iota` 这类生成型视图或带内部状态的适配器,反复取 `begin/end` 时得留意语义。

**view 不能比源活得长**。这条前面已经强调,函数里返回局部 vector 的视图、把视图挂在临时量上,都是悬垂。需要长期持有就物化进容器。

**编译器要够新**。C++20 ranges 需要 GCC 10 起,本机 GCC 16.1.1 实测支持完整;`chunk`/`slide`/`stride` 这类适配器是 C++23 的,`-std=c++20` 下不可用。

**报错会很长**。管道全是模板,一个 lambda 返回类型对不上, GCC 能吐几十行约束失败。遇到这种先看最里层的约束报错,确认 range 的 `value_type` 和您 lambda 接收的参数类型对得上。

## 这一卷到这里

管道操作符加 Ranges,把"过滤、转换、收集"这种数据处理流水线写得跟自然语言一样顺,还保留了单遍惰性的性能。配合前面几篇讲的 `if constexpr`、变参模板、完美转发、CTAD、类型安全的 `any`/`variant`、指定初始化器,咱们手上的现代 C++ 工具箱已经能覆盖嵌入式开发里绝大多数场景:编译期分派、零开销抽象、类型安全的数据承载、自解释的配置、可组合的数据处理。

这一卷讲的是"语言给了什么"。后面要进入的是"用这些工具怎么组织工程",包括 RAII 资源管理、智能指针所有权、并发模型等。工具本身不复杂,难的是在真实项目里挑对工具、把它们摆到正确的位置。
