---
title: "mini STL 实战(一):RawBuffer,只管容量,不管对象"
description: "手搓容器库的地基:一个崩溃式检查函数、一个析构与搬迁的辅助头,和一块只管容量、不管对象生命周期的裸内存。对着 Chromium 的 vector_buffer.h 照镜子,测试里除了常规断言,还有一个'必须当场死'的死法用例。"
chapter: 1
order: 1
tags:
  - host
  - cpp-modern
  - intermediate
  - 容器
  - 内存管理
difficulty: intermediate
platform: host
reading_time_minutes: 12
cpp_standard: [17, 20, 23]
prerequisites:
  - "mini STL 导论:为什么值得亲手搓一遍容器"
related:
  - "mini STL 实战(二):Vector,扩容与搬迁"
---

# mini STL 实战(一):RawBuffer,只管容量,不管对象

这一篇的配套代码在 `code/volumn_codes/vol8-labs/ministl/stage1_rawbuf_vector/`,四个头文件:两个辅助、一个工具、一个主角。全部代码和测试输出都能在您的机器上原样复现,构建命令在文末。

`std::vector` 把"申请内存"和"构造对象"捆在一起卖。方便,但捆着卖的东西没法单独买——后面咱们要做的环形缓冲、哈希表,都需要"先圈一块地,对象什么时候生、什么时候死,听咱们安排"的自由度。

好消息是，Chromium 的工程师 2017 年也为同样的需求动过手,产物是 `base/containers/vector_buffer.h`,179 行,文件开头第一句自我介绍:"Unlike std::vector, VectorBuffer never constructs or destructs its arguments"。这篇咱们搓的就是它的教学版（您问为什么不是1：1，额，搓不动。。。）

## debug::Check:崩溃式检查

动工之前,咱们先把检查工具备好。库里的越界、违约,处理办法是当场崩溃:

```cpp
static inline void Check(bool IsExpectedTrue, const char* msg,
                         std::source_location loc = std::source_location::current()) {
    if (IsExpectedTrue) {
        return; // 哈,就是对的?无事发生~
    }

    std::fprintf(stderr, "[TAMCPP Check Crash]%s-%d(%s): %s\n", loc.file_name(), loc.line(),
                 loc.function_name(), msg);
    std::abort();
}
```

这是跟着 Chromium 的 `CHECK` 学的:浏览器内核宁可崩,也不带着坏数据继续跑。这个哲学是开发的时候，永远不要延迟暴露问题。这也是笔者当时写崩溃实验室的时候，写下的一个讨论的回答——不要在开发的时候拖延问题。

这里咱们用函数实现,`std::source_location`(C++20)能把调用处的文件、行号、函数名白送进来;宏时代要靠 `__FILE__`/`__LINE__` 自己拼,现在您不需要再拼了。它在后面会真的派上用场:本篇的验收里就有一个"必须以 Check Crash 收场"的死法用例。

配套咱们还有个两行的 `DISABLE_COPY` 宏,把拷贝构造和拷贝赋值一起删掉,省得每个类手写两行 `= delete`。宏故意不带末尾分号,逼使用处写成分号结尾的完整语句。

## DestroySources 与 Relocate:对象的死与搬

`memory_helper.hpp` 只有两个函数,却是整个库最值得咱们逐行读的代码。头一个是"让一段对象死":

```cpp
template <typename Sources> inline void DestroySources(Sources* begin, Sources* end) {
    if constexpr (!std::is_trivially_destructible_v<Sources>) {
        for (Sources* index = begin; index < end; index++) {
            index->~Sources();
        }
    }
}
```

`index->~Sources()` 这个写法,是咱们在显式调用析构函数——placement new 的镜像操作。placement new(写法 `new (p) Sources(...)`)把对象"生"在给定的内存上,只构造、不申请内存;显式析构把对象从内存里"取走",内存本身不动。

一进一出,配对使用,这就是"内存和对象是两回事"在代码层面的样子,咱们后面每一件容器都要用这对操作。前面的 `if constexpr` 在编译期裁掉分支:`int` 这类平凡可析构类型,函数体是空的,连循环都不存在:对平凡类型逐个调析构本来就没必要,语义上这个分支就该消失。

第二个是"搬家",三档:

```cpp
template <typename Sources> inline void Relocate(Sources* from, Sources* from_end, Sources* to) {
    // 我们放掉那些干净的空东西,因为没必要
    if (from == from_end) {
        return;
    }
    if constexpr (std::is_trivially_copyable_v<Sources>) {
        std::memcpy(to, from, sizeof(Sources) * (from_end - from));
    } else {
        for (Sources* p = from; p != from_end; ++p, ++to) {
            if constexpr (std::is_move_constructible_v<Sources>) {
                new (to) Sources(std::move(*p));
            } else {
                new (to) Sources(*p);
            }
            p->~Sources();
        }
    }
}
```

平凡可拷贝的类型,咱们直接整块 `memcpy` 搬走。能移动构造的,就一个一个来:在新位置构造一个,老位置马上析构一个,严格交替,不许先把全部搬完再统一杀。连移动构造都不行的类型,只好老老实实用拷贝兜底。测试里有个 `CopyOnly` 类型把移动构造删掉,专门验第三档确实存在。

开头那个提前返回,挡的是笔者踩过的一个暗坑。空数组第一次扩容,`from` 和 `from_end` 是两个空指针,`memcpy` 长度是 0,看起来"零长度等于啥也没干"。但 C 标准要求 `memcpy` 的两个指针都有效,传空指针本身就是未定义行为,和长度无关。

UB 的三副面孔这里都可能出现:可能崩、可能不崩、也可能"成功"地什么都没干——哪种都不保证,您别指望它讲道理。这种所有测试都通过、毛病却藏在暗处的坑,只有 AddressSanitizer / UndefinedBehaviorSanitizer 抓得住,所以本库的 CMake 默认给所有目标挂双探测器,想裸跑性能再 `-DTAMCPP_MINISTL_SANITIZE=OFF`。

## RawBuffer 本体

主角这就登场,咱们给它的成员只有两个:一个指针,一个容量。

```cpp
template <typename Sources> struct RawBuffer {
    RawBuffer() = default;
    explicit RawBuffer(std::size_t capacity) : capacity_(capacity) {
        // 乘法先验溢出:回绕会骗过 operator new,圈的地比要的小,后面全是越界
        debug::Check(capacity <= std::numeric_limits<std::size_t>::max() / sizeof(Sources),
                     "capacity * sizeof(Sources) overflows");
        raw_buffer_begin_ =
            static_cast<Sources*>(::operator new(capacity * sizeof(Sources)));
    }
    // ...
```

内存来源是 `::operator new`。为什么不用另外两个候选?咱们挨个看。`new Sources[n]` 看着方便,但它会顺手把 n 个对象全部构造出来,这和"不管对象"的定位直接冲突,不能用。`malloc` 倒是够裸,可它只保证基本对齐;哪天您往里装一个自定义对齐的类型,它就不够用了。`::operator new` 正合适:拿到手的,就是一块大小对、对齐对、内容未定义的内存。析构那边对称地用 `::operator delete`,只把内存还回去。至于把对象杀干净,那是上层容器的义务,不归这块缓冲管。Chromium 那边选的是 `malloc`,乘法上配了溢出检查(`vector_buffer.h:50-54`)。咱们这边的乘法检查同样不能省:上面那段 `Check` 真的触发过一次:拿 `SIZE_MAX / 4 + 1` 个 `int` 去构造,当场收到:

```text
[TAMCPP Check Crash]include/tamcpp_ministl/raw_buffer.hpp-32(...): capacity * sizeof(Sources) overflows
```

移动那一对负责把产权移交干净:指针和容量接过来,对面置空。移动赋值的开头判了一下自赋值。为什么要判?`MySelf = std::move(MySelf)` 这种事,您的用户只会来问您为什么炸了,不会承认自己写过这行。判了就返回,当无事发生。

访问走双轨。`visit_at` 带 `Check`,越界当场死;`operator[]` 不检查,信任调用方。这个分工也是照着 Chromium 的:`vector_buffer.h` 的 `operator[]` 挂着 `CHECK_LT`(:79)。std 那边正好相反,`operator[]` 从不检查。咱们两边都留着,上层容器爱用哪个用哪个。

## 验收

咱们写的测试是黑盒的:只看头文件签名和本篇承诺的行为,不读实现。`tests/test_raw_buffer.cpp` 的主体是一个计数类型。它每构造一个对象,全局计数就加一;每析构一个,就减一。有了它,验证就简单了:搬 5 个对象过去,计数必须还是 5。搬迁路上漏掉一次析构,计数会多 1;对同一个对象析构两次,计数会少 1。不管哪种,断言当场失败。三档搬迁各配一组这样的用例。另外,哪一档该走哪条路径,测试里用 `static_assert` 在编译期固定了下来,防止换个编译器就悄悄换了档。跑起来:

```text
$ cmake --build build && ./build/tests/test_raw_buffer
RAW BUFFER ALL GREEN
```

有意思的是另一个用例。`tests/test_raw_buffer_bounds.cpp` 只有一行正经代码:`buf.visit_at(4)`,capacity 恰好 4,访问第 5 格。它的正确结局是崩溃,打印出"运行到这了"反而算失败。这给测试框架出了个难题。ctest 见到信号打死的子进程,一律判负,也不看输出正则。所以中间垫了一个裁判脚本 `expect_check_death.sh`,由它来验证两件事:崩溃报告在场,程序也真的死了。只打印报告、自己不死的假检查,骗不过它,咱们要的就是这层保险。

```text
$ ctest --output-on-failure
100% tests passed out of 4
```

四个用例跑完:两个接口黑盒,两个越界死亡用例,都按预期收了场。地基到此打好,下一篇咱们就在这上面立 Vector。

## 构建与复现

```bash
cd code/volumn_codes/vol8-labs/ministl/stage1_rawbuf_vector
cmake -B build . && cmake --build build
(cd build && ctest --output-on-failure)   # 4/4
./build/example/raw_buffer_smoke          # relocated[5] = 25 / SMOKE GREEN
```

## 参考资源

- 配套代码:`code/volumn_codes/vol8-labs/ministl/stage1_rawbuf_vector/`
- Chromium `base/containers/vector_buffer.h`(本篇镜子,179 行)
- [cppreference:placement new](https://en.cppreference.com/w/cpp/language/new)
- [cppreference:`std::source_location`](https://en.cppreference.com/w/cpp/utility/source_location)
- [自定义分配器与 PMR:自己管内存](../../vol3-standard-library/containers/13-custom-allocators.md)(vol3,内存管理的概念层)
