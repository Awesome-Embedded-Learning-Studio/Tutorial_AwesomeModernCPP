---
chapter: 12
cpp_standard:
- 20
description: C++20 Ranges 把「一对迭代器」抽象成 range,让算法直接吃整个容器;视图(view)在此之上做到惰性求值、引用源数据、拷贝 O(1),用管道把 filter/transform/take 串成一条不分配的流水线
difficulty: intermediate
order: 7
platform: host
prerequisites:
- '指定初始化器'
reading_time_minutes: 14
related:
- '管道操作与 Ranges 实战'
- '指定初始化器'
tags:
- host
- cpp-modern
- intermediate
- Ranges
title: 'C++20 Ranges:范围与视图'
---
# C++20 Ranges:范围与视图

处理一帧传感器数据,要做的事往往是一串:滤掉异常值、把原始码换算成工程量、取前几个发出去。老写法得开两个临时 `vector`,一遍 `copy_if` 一遍 `transform`,中间夹着几个 `back_inserter`,代码读起来像被切碎的清单。C++20 的 Ranges 给了一条更顺的路:把整条流程写成一条管道,中间不分配内存,逻辑一眼到底。这套东西的关键是**视图(view)**,它惰性、不持有数据、拷贝廉价,恰好是嵌入式里最想要的那种抽象。

这一篇咱们先把两个最容易混的概念分清楚——Range 和 View,把视图的三条核心特性逐条用实测坐实,最后接到一个温度数据流水线上。

## Range:可以迭代的玩意儿

C++20 给 Range 下的定义很朴素:**任何能提供一对迭代器(begin/end)的东西**。`std::vector`、`std::array`、原生数组,都是 Range。

最大的体感变化是算法不再逼您写一对 `begin()/end()`。原来:

```cpp
std::sort(vec.begin(), vec.end());
```

C++20 直接吃整个容器:

```cpp
std::ranges::sort(vec);   // 整个 range 丢进去
```

这只是表层糖衣,真功夫在 `<ranges>` 头里那套视图工厂。先把两个概念划清:

- **Range**:能迭代的统称,`vector`/`array`/原生数组都算,而且**持有自己的数据**。
- **View**:一种特殊的 Range,它**不持有数据**,只是对现有数据「换个角度」看一眼,而且**惰性求值**。

下面几节围绕 View 展开,这是全文的地基。

## 视图惰性:建的时候什么都不算

视图是「懒」的。您建一个 `filter` 视图,那一刻什么计算都没发生;直到开始迭代,谓词才被调用。咱们拿计数器插进谓词里证明:

```cpp
std::vector<int> data = {1, 2, 3, 4, 5};
int pred_calls = 0;

auto v = data | std::views::filter([&](int x) {
    ++pred_calls;
    return x > 2;
});
std::cout << "建视图后(还没遍历)谓词调用次数=" << pred_calls << "\n";

for (int x : v) {
    std::cout << "取到 " << x << ", 此刻谓词已调用 " << pred_calls << " 次\n";
}
```

<OnlineCompilerDemo allow-run
  title="视图惰性、引用语义、O(1) 拷贝"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_laziness.cpp"
  description="用计数谓词证明 filter 视图建好时零次调用、遍历中逐元素触发;改源容器后重新遍历看到新值;拷贝视图不复制底层元素。"
/>

运行结果:

```text
建视图后(还没遍历)谓词调用次数=0
取到 3, 此刻谓词已调用 3 次
取到 4, 此刻谓词已调用 4 次
取到 5, 此刻谓词已调用 5 次
改 data[2]=300 后重新遍历: 300 4 5
拷贝视图后 v2 首元素=300
```

`pred_calls` 一开始是 0,建视图本身没花一次谓词。开始迭代后,`filter` 为了找到头一个 `>2` 的元素,得扫过 `1`、`2`、`3`,所以第一次取到 `3` 时计数已经跳到 3。这就是惰性的证据:谓词只在真正要元素的时候才跑。

顺带一条同样关键的特性。上面把 `data[2]` 从 `3` 改成 `300` 之后,**重新遍历视图看到的是新值**。视图不拷数据,它只是引用源容器,源变了视图就跟着变。

## 视图不持有数据,拷贝是 O(1)

视图只是「看着」底层数据,不持有它们。所以拷贝一个视图,拷贝的是几个迭代器和谓词,底层元素一个都不复制。对于嵌入式,这条意味着您可以放心地把视图当参数到处传,不用担心隐式拷一大块 buffer。

这里有个反直觉的坑得提前讲。视图**引用**源数据,这话是字面的:它存的是指向源的指针/迭代器,不是值的快照。所以**源容器的生命周期得比视图长**。一旦源被销毁,视图就成了悬空引用。这个坑后面专门用一节演示,这里先记住结论。

## 常用视图工厂

`<ranges>` 提供了一批「视图工厂」,挑嵌入式里最常用的几个,各看一个最小例子。

```cpp
std::vector<int> data = {120, 45, 230, 67, 340, 89, 56, 180};

// filter:只保留落在 [50,300] 的读数
auto valid = data | std::views::filter([](int v){ return v >= 50 && v <= 300; });

// transform:12 位 ADC 原值转电压(mV 量级)
auto mv = std::views::transform(data, [](int adc){ return adc * 3300 / 4095; });

// take / drop:切数据帧的头部/尾部
auto seq = std::views::iota(0, 10);
auto first3 = seq | std::views::take(3);             // 0 1 2
auto rest    = std::views::iota(0, 10) | std::views::drop(3);   // 3..9
auto middle  = std::views::iota(0, 10) | std::views::drop(2) | std::views::take(4);  // 2 3 4 5

// iota:生成 ADC 通道编号 0..15,不占任何存储
auto adc_channels = std::views::iota(0, 16);
```

`iota` 这条值得停一下:它按需 `+1` 生成,既不分配也不存数,适合「我只要一段序号」的场景,比如枚举一组通道号、生成索引下标。

字符串解析还能用 `split` 按分隔符切。NMEA 报文、键值对这种「逗号或等号分隔」的协议,一行就能切成子 range:

```cpp
std::string raw = "sensor1=25,sensor2=30,sensor3=28";
for (auto sub : raw | std::views::split(',')) {
    std::string_view sv{sub.begin(), sub.end()};   // sub 不是 string,转成 string_view 用
    // [sensor1=25] [sensor2=30] [sensor3=28]
}
```

<OnlineCompilerDemo allow-run
  title="视图工厂:filter / transform / take / drop / iota / split"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_factories.cpp"
  description="六个最常用视图工厂各一个最小例子,涵盖过滤、映射、切片、生成序列、字符串切分。"
/>

运行结果:

```text
filter [50,300]: 120 230 67 89 56 180
transform->mV: 96 36 185 53 273 71 45 145
take 3: 0 1 2
drop 3: 3 4 5 6 7 8 9
drop 2 | take 4: 2 3 4 5
iota ADC 通道: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
split(','): [sensor1=25] [sensor2=30] [sensor3=28]
```

## 组合管道:把视图串起来

单个视图威力有限,串起来才显出 Ranges 的味道。用管道符 `|` 把多个视图连成一条链,整条链惰性求值,**迭代时数据一个一个流过去**(管道符的完整用法下一篇细讲,这里先建立直觉)。

```cpp
std::vector<int> readings = {120, 45, 230, 67, 340, 89, 56, 180};
int tf_calls = 0;

auto pipeline = readings
    | std::views::filter([](int v){ return v >= 50 && v <= 300; })
    | std::views::transform([&](int v){ ++tf_calls; return v * 3.3f / 4095; })
    | std::views::take(3);
```

这段读起来像一句话:「从 `readings` 里滤出有效值、换成电压、取前 3 个」。没有中间 `vector`,逻辑没被切碎。咱们拿计数器看看惰性到底懒到什么程度。

<OnlineCompilerDemo allow-run
  title="组合管道的惰性:take 在取够后掐断整条链"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_pipeline.cpp"
  description="整条 filter|transform|take 管道建好时 transform 零次调用;遍历取 3 个,transform 恰好调用 3 次,take 提前终止了上游。"
/>

运行结果:

```text
建好管道(没遍历) transform 调用次数=0
前 3 个有效读数的电压:
  0.096703
  0.185348
  0.053993
遍历完 transform 总调用次数=3(take 在取够 3 个后掐断了管道)
```

`transform` 总共被调了 3 次,正好等于 `take(3)` 的个数。这说明整条管道是**逐元素、按需**推进的:`take` 取够 3 个,上游的 `filter` 和 `transform` 就停了,后面那些元素压根没被碰过。8 个原始读数里,`340`、`56`、`180` 这些既没被 filter 判断、也没被 transform 算过。这就是惰性管道的核心价值:您只为您真要的结果付费。

## 嵌入式实战:温度数据流水线

把前面的东西拼到一个真实的嵌入式场景里。一组温度传感器回读,夹杂异常(掉线时 999、断路时 -200),要做的是:滤掉异常、摄氏转华氏、求平均发出去。

```cpp
std::vector<int> readings = {23, 999, 25, -200, 27, 22, 999, 26};

auto processed = readings
    | std::views::filter([](int t){ return t >= -50 && t <= 150; })
    | std::views::transform([](int t){ return t * 9.0 / 5.0 + 32.0; });
```

整条处理没有 `filtered`、`calibrated` 之类的中间容器,数据只被遍历一次,内存开销恒定。

<OnlineCompilerDemo allow-run
  title="温度传感器数据处理流水线"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_sensor_pipeline.cpp"
  description="模拟一帧带异常的温度读数,filter 滤掉异常、transform 转华氏、求平均,全程零临时容器。"
/>

运行结果:

```text
有效读数(F): 73.4 77.0 80.6 71.6 78.8
平均温度: 76.3 F
```

注意那些 `999`、`-200` 从头到尾没在任何中间 buffer 里出现过,它们只是被 `filter` 跳过了。换成老写法,这两个值至少会被先 `push_back` 进原始 `vector`,再在过滤时被丢掉。

## 避坑:视图的生命周期

视图不持有数据,这条优势翻过来就是最大的坑:**源容器没了,视图就悬空了**。最常见的写法是从函数里返回一个视图,但视图引用的容器是函数内的局部变量:

```cpp
// 反例:local 在函数返回时析构,返回的视图立刻悬空
auto make_bad_view() {
    std::vector<int> local = {1, 2, 3, 4, 5};
    return local | std::views::filter([](int x){ return x > 2; });
}
```

这是个 use-after-free。视图内部是个 `ref_view`,存着指向 `local` 的指针;函数一返回,`local` 析构,那块内存还给堆栈/堆,视图成了野指针。咱们用 AddressSanitizer 抓一下,编译命令是 `g++ -std=c++20 -DDANGLING -fsanitize=address ranges_dangling.cpp`。

<OnlineCompilerDemo allow-run
  title="视图悬空:默认演示正确写法,-DDANGLING 配合 ASan 复现"
  source-path="code/examples/vol4/vol2-modern-cpp17/ranges_dangling.cpp"
  description="默认模式演示数据源与视图同生命周期的正确写法;加 -DDANGLING 用 ASan 复现视图引用临时容器返回后悬空的 use-after-free。"
/>

ASan 的核心几句:

```text
ERROR: AddressSanitizer: stack-use-after-return on address 0x...
READ of size 8 ... in std::ranges::ref_view<...>::end() const
This frame has 4 object(s):
  [96, 120) 'local' (line 8) <== Memory access at offset 104 is inside this variable
```

报的是 `ref_view::end()` 在读已经析构的 `local`。正确写法是让数据源活得比视图久:把数据放进一个类成员,视图只引用这个成员;或者让数据源作为参数传进来。示例里 `SensorBuffer` 类就是把 `data_` 存成成员,`valid()` 返回的视图只要 `SensorBuffer` 对象还在就安全。

::: warning 别在视图还活着的时候动它的源
视图引用源,源的内容变了视图就跟着变,这通常没问题。但要注意:**改源的结构**(插入、删除、扩容导致迭代器失效)是另一回事。filter 这类视图还会缓存 `begin`,源一旦让缓存迭代器失效,视图行为就未定义。原则是:视图存活期间,只读不改它的源;要改,就先物化成容器。
:::

## 视图 vs 容器:什么时候用哪个

视图不是万能的。该用视图还是该落成容器,可以照这两条划:

- **用视图**:只读、一次性遍历、想零拷贝地组合操作,且数据源活得够久。
- **用容器**:需要修改数据、需要多次遍历同一份结果、数据源马上要销毁、确实需要持有数据。

视图不存数据,所以「多次遍历同一结果」这种需求,与其每次重新跑一遍管道,不如跑一次物化成容器。物化的手段是 `std::ranges::to<std::vector<int>>(...)`,不过它是 **C++23** 才进标准的,本篇讲的是 C++20;在 C++20 里可以老老实实遍历一遍视图往 `vector` 里塞。下一篇讲管道操作符时会再聊到 `ranges::to`。

最后一句关于类型:视图的类型是一长串模板嵌套(`filter_view<transform_view<ref_view<vector<int>>, ...>, ...>`),别手写,一律用 `auto`。

下篇咱们把管道操作符 `|` 的机制掰开,看它怎么把视图两两接起来,以及更多 Ranges 实战技巧。
