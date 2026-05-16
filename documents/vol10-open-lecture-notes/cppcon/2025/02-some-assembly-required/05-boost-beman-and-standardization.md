---
title: "Boost、Beman 与 C++ 标准化路径"
description: "CppCon 2025 演讲笔记 —— C++: Some Assembly Required by Matt Godbolt"
conference: cppcon
conference_year: 2025
talk_title: "C++: Some Assembly Required"
speaker: "Matt Godbolt"
video_bilibili: "https://www.bilibili.com/video/BV1ptCCBKEwW?p=2"
video_youtube: "https://www.youtube.com/watch?v=zoYT7R94S3c"
tags:
  - cpp-modern
  - host
  - intermediate
difficulty: intermediate
platform: host
cpp_standard: [17, 20]
chapter: 2
order: 5
---

# Boost：原来 C++ 标准库的"后花园"长这样

学 C++ 的时候很多人都有个困惑：标准库里那些东西到底是怎么来的？是某天委员会开个会，一群大佬拍脑袋说"我们加个 `shared_ptr` 进去吧"？还是有什么更系统化的流程？看完历史资料把这条线理清楚之后，结论让人印象深刻——原来几乎所有日常使用的组件，都来自同一个地方。

## 先把 STL 和标准库的关系捋清楚

很多人一直把"STL"和"C++ 标准库"混着说，毕竟平时写代码，`#include <vector>` 然后说"用了 STL"，谁也不会纠正你。但严格来讲，这是两回事，搞清楚这个后面看历史才不会乱。

STL 的全称是 "Standard Template Library"<RefLink :id="8" preview="Wikipedia: Standard Template Library, name origin and history" />——有趣的是，Stepanov 和 Lee 的首字母恰好也是 S 和 L，很多人把这个当作一个有趣的巧合<RefLink :id="9" preview="Stepanov interview, STL naming anecdote" />。这个库是 Alexander Stepanov 和 Meng Lee<RefLink :id="1" preview="Stepanov & Lee, The Standard Template Library, HP Labs, 1995" /> 在 HP 公司的时候搞出来的。Stepanov 现在虽然已经退休，但他当年做的事情可以说是定下了 C++ 的基调。STL 里面那些概念——迭代器、算法与容器分离、时间复杂度保证——这些东西放到 1994 年来看，简直是降维打击。后来这份提案在 1994 年 7 月的 ANSI/ISO 委员会会议上获得最终批准，委员会的回应被描述为"overwhelmingly favorable"<RefLink :id="10" preview="Wikipedia: History of the STL, committee approval" />。要知道那可是九十年代，C++ 标准化本身都还在早期阶段，能以这种压倒性优势通过，说明这东西确实做得漂亮。

但 STL 只是 Stepanov 他们的那个库。后来它被标准吸收了一部分，但不是全部。比如 SGI 的 STL 实现里早就有 `hash_map` 了<RefLink :id="8" preview="Wikipedia: STL, SGI implementation and hash_map history" />，但 C++98 标准里并没有收录，直到 C++11 才以 `unordered_map` 的形式进来。所以标准库的范围比 STL 大得多，STL 是其中最核心、最耀眼的那一块，但不是全部。

## 那标准库里其他东西哪来的？

`shared_ptr` 不是 STL，`tuple` 不是 STL，`regex` 不是 STL，`filesystem` 也不是 STL。它们是怎么进标准库的？答案就两个字：Boost。

第一次听到这个答案可能会觉得意外，因为很多教程提到 Boost 都是一笔带过，说"这是个第三方库，了解一下就行"。但翻一下 Boost 的历史就会发现，事情完全反过来了——不是 Boost 借了标准库的光，而是标准库从 Boost 那里汲取了四分之一个世纪的养分。

Boost 项目 1999 年首次正式发布<RefLink :id="2" preview="Beman Dawes, Boost Libraries, 1999" />，跟 C++ 标准化的进程几乎是同步的。它的定位之一——注意，**只是之一**——是作为高质量库的试验场：有人有了一个好想法，先在 Boost 里实现出来，让大家用、让大家骂、让大家提意见，等它被工业界充分验证了，再考虑往标准里推。但这个"试验场"的比喻有它的局限性——后面会详细说。

下面列一些我们天天在用、但可能没意识到是 Boost 出身的东西：`shared_ptr`/`weak_ptr` 来自 Boost.SmartPtr，`function`/`bind` 来自 Boost.Function 和 Boost.Bind，`tuple` 来自 Boost.Tuple，`regex` 来自 Boost.Regex，`array` 来自 Boost.Array，`unordered_map`/`unordered_set` 来自 Boost.Unordered，`chrono` 来自 Boost.Chrono，`filesystem` 来自 Boost.Filesystem。这些不是什么冷门组件，而是 C++ 程序员每天写代码都要碰到的东西。它们每一个都先在 Boost 里存活了少则三五年、多则十几年，被无数项目在真实环境里检验过，bug 修得差不多了，API 设计也磨得差不多了，然后才被"转正"。

## 动手验证一下：看看 Boost 和标准库的渊源

光说没用，我们跑点代码感受一下。本地环境是 Arch Linux WSL，GCC 16.1.1，Boost 1.91 通过 pacman 装的。

先看一个最经典的例子——`shared_ptr`。Boost 版本和标准库版本的接口几乎一模一样，这不是巧合，是因为标准库版本就是照着 Boost 版本抄的：

```cpp
// 文件: shared_ptr_compare.cpp
// 编译: g++ -std=c++20 shared_ptr_compare.cpp -o sp

#include <iostream>
#include <memory>       // 标准库的 shared_ptr
// #include <boost/shared_ptr.hpp>  // Boost 的 shared_ptr

int main() {
    // 标准库版本
    auto p1 = std::make_shared<int>(42);
    std::cout << "use_count: " << p1.use_count() << "\n";
    std::cout << "value: " << *p1 << "\n";

    // 如果你把上面 Boost 的头文件取消注释，
    // 下面这行就能编译，接口完全一样：
    // auto p2 = boost::make_shared<int>(42);
    // std::cout << "boost use_count: " << p2.use_count() << "\n";

    auto p3 = p1;  // 引用计数 +1
    std::cout << "after copy, use_count: " << p1.use_count() << "\n";

    return 0;
}
```

运行结果：

```
use_count: 1
value: 42
after copy, use_count: 2
```

这个例子本身没什么技术含量，但核心观点在于：`use_count()`、`make_shared`、拷贝语义——这些 API 设计不是委员会坐在会议室里想出来的，是 Boost 社区用了好几年、踩了无数坑之后沉淀下来的。标准化的过程更像是"追认"而不是"发明"。

再看一个更有意思的例子，`boost::filesystem` 和 `std::filesystem`。Boost 版本出现得早得多，C++17 才把文件系统库纳入标准。下面这个脚本对比一下两者的用法差异：

```cpp
// 文件: fs_compare.cpp
// 编译: g++ -std=c++20 fs_compare.cpp -o fs

#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

// 如果你用 Boost 版本，只需要改一行：
// #include <boost/filesystem.hpp>
// namespace fs = boost::filesystem;

int main() {
    fs::path p = "/tmp/test_dir";

    // 创建目录
    if (!fs::exists(p)) {
        fs::create_directories(p);
        std::cout << "created: " << p << "\n";
    }

    // 遍历目录
    for (const auto& entry : fs::directory_iterator(p)) {
        std::cout << "  " << entry.path().filename()
                  << " | size: " << entry.file_size() << "\n";
    }

    // 清理
    fs::remove_all(p);
    std::cout << "removed: " << p << "\n";

    return 0;
}
```

运行结果（GCC 16.1.1, `-std=c++20`）：

```
created: "/tmp/test_dir"
removed: "/tmp/test_dir"
```

::: details 为什么输出带引号？
`std::filesystem::path` 的 `operator<<` 会用双引号包裹路径输出，这是标准规定的行为。如果你不想带引号，可以改成 `std::cout << p.string() << "\n"`。
:::

你会发现，除了头文件和 namespace 不同，代码逻辑完全不用改。这就是 Boost 作为"试验场"的价值——它在标准库还没有文件系统支持的那些年里，给了 C++ 程序员一个统一的、跨平台的文件系统操作方案，等 C++17 终于把 `std::filesystem` 标准化的时候，API 已经非常成熟了，大家迁移起来几乎零成本。

## 但 Boost 不只是标准库的"预备队"

这里有一个常见的误解，觉得 Boost 里的东西最终目标都是进标准库，没进去的就是"失败品"。这个想法完全错了。Boost 里面有很多东西，压根就不适合进标准库，但它们在各自领域里极其强大。比如 Boost.Spirit 是一个基于组合子解析器的框架，可以让你用类似 EBNF 的语法定义解析规则，直接在 C++ 里写语法分析器。这东西太领域化了，标准库不可能收录，但如果要做文本解析，它比手写状态机好用得多。Boost.Python 是 C++ 和 Python 之间的互操作库，让你几乎无痛地暴露 C++ 接口给 Python 调用，这种跟具体语言绑定的东西放标准库里显然不合适。Boost.Compute 类似 OpenCL 的 GPGPU 计算库，跟硬件平台强相关，也不该进标准。Boost.Beast 基于 Boost.Asio 的 HTTP 和 WebSocket 库，现在做 C++ 后端的很多人在用。

所以 Boost 的真实定位是：它既是标准库的源泉之一，也是一个独立的高质量 C++ 库集合。有些东西"毕业"去了标准库，有些东西一直在 Boost 里发光发热，两者并不矛盾。

---

# 从 Boost 到 Beman：C++ 标准库的"传送带"是怎么转的

## "试验田"这个比喻到底错在哪

前面说到 Boost 的定位之一是"试验场"，这个说法在很多教程里被进一步简化成了"Boost 是 C++ 标准库的试验田"。但很多人把这句话理解成了"Boost 里的东西迟早会进标准"。这个理解问题很大，因为它完全忽略了"怎么进"和"什么时候进"这两个关键问题。

实际上，Boost 和 C++ 标准委员会之间的关系远没有"试验田"三个字暗示的那么简单直接。Boost 有自己的治理结构、自己的评审流程、自己的发布节奏，而 C++ 标准化走的是 ISO 流程，两套体系的目标并不完全一致。Boost 里有些库设计得非常通用、非常灵活，但恰恰因为太灵活了，标准化的时候反而需要做大量裁剪和调整，这个调整过程可能长达数年甚至更久。所以你看到 Boost 里很多库从提出到最终被标准采纳，中间隔了好几个 C++ 标准版本，这不是因为委员会效率低，而是因为两套体系的对接成本确实很高。

## Beman 项目：2024 年启动的那条"传送带"

2024 年 David Sankel 宣布了 Beman 项目<RefLink :id="4" preview="David Sankel, Beman Project, CppCon 2024" />。第一眼看可能会觉得"又来一个 Boost 替代品？"，但仔细看下去才发现完全不是那么回事。

Beman 的定位非常明确：它里面的每一个库，从立项的第一天起，目标就是进入 C++ 标准。这不是"先做一个好用的库，看看以后有没有机会标准化"，而是"我们就是要做一个能直接推到 WG21 去的提案，附带完整的参考实现"。可以把它理解成一条传送带——库在 Beman 里完成设计、实现、实战检验，然后带着一篇论文直接推上标准化的轨道。

这个定位意味着 Beman 在流程上做了大量简化。Boost 的评审流程很重，你要考虑和 Boost 其他几十个库的兼容性、要满足 Boost 的代码风格要求、要通过 Boost 社区的投票。而 Beman 说白了就是冲着标准化去的，开销相对低很多，不需要在"做一个通用库"和"做一个标准提案"之间做权衡，因为这两件事在 Beman 里就是同一件事。

之前很多人觉得"为什么不直接从 Boost 拿东西进标准"，原因其实很简单——Boost 的设计约束和标准的约束不一样，直接搬过来往往行不通，而改造一个已经在 Boost 生态里扎根的库，政治成本和技术成本都很高。Beman 相当于绕开了这个问题，从头开始就以"能进标准"为前提来设计。

## Beman 里现在有什么

目前 Beman 大约有 8 个活跃仓库<RefLink :id="4" preview="Beman Project, GitHub organization" />，其中有一个是示例库 `exemplar`，展示了一个 Beman 库应该怎么组织代码、怎么写文档、怎么配套提案。这个 `exemplar` 本身功能很简单，但它作为"模板"的价值很大。

几个实用方向的子项目值得关注。比如 `optional` 的扩展——C++23 终于给 `std::optional` 加上了 `transform` 和 `and_then`<RefLink :id="11" preview="cppreference: std::optional, C++23 monadic operations" />，而 Beman 的 Optional26 项目则在此基础上瞄准 C++26 做进一步的扩展。写代码的时候，每次遇到"可能没有值"的场景，都会在 `std::optional` 和裸指针之间纠结。用裸指针吧，`nullptr` 既可能表示"没有值"也可能表示"出错了"，语义混在一起，每次看到 `if (ptr != nullptr)` 都不太确定这个 null 到底是业务上的"没有"还是逻辑上的"错"。用 `std::optional` 吧，语义倒是清晰了，但链式操作非常痛苦。

举个具体的例子。假设我有一个从用户 ID 查用户信息、再从用户信息里提取邮箱的流程，用 C++23 之前的 `std::optional`，你得这么写：

```cpp
#include <optional>
#include <string>
#include <iostream>

struct UserInfo {
    std::string email;
};

// 模拟一个可能查不到用户的查询
std::optional<UserInfo> find_user(int user_id) {
    if (user_id == 42) {
        return UserInfo{.email = "alice@example.com"};
    }
    return std::nullopt;
}

// 从用户信息里提取邮箱，但邮箱可能为空
std::optional<std::string> extract_email(const UserInfo& user) {
    if (user.email.empty()) {
        return std::nullopt;
    }
    return user.email;
}

int main() {
    int input_id = 42;
    
    // 以前的写法：一层一层手动检查，嵌套 if，看着就累
    std::optional<std::string> result;
    auto user_opt = find_user(input_id);
    if (user_opt) {
        auto email_opt = extract_email(user_opt.value());
        if (email_opt) {
            result = email_opt.value();
        }
    }
    
    if (result) {
        std::cout << "邮箱: " << *result << "\n";
    } else {
        std::cout << "无法获取邮箱\n";
    }
    
    return 0;
}
```

你看这个嵌套，虽然只有两层就已经很烦了。实际业务代码里三四层嵌套很常见，每一层都要手动检查 `has_value()`，然后手动解包，然后再传给下一层。Rust 的 `Option::and_then` 在这方面做得很好，C++ 一直没有对应机制。

现在 Beman 的 `optional` 扩展就是补这个缺的。有了 `transform` 和 `and_then` 之后，同样的逻辑可以写成这样：

```cpp
#include <optional>
#include <string>
#include <iostream>

struct UserInfo {
    std::string email;
};

std::optional<UserInfo> find_user(int user_id) {
    if (user_id == 42) {
        return UserInfo{.email = "alice@example.com"};
    }
    return std::nullopt;
}

std::optional<std::string> extract_email(const UserInfo& user) {
    if (user.email.empty()) {
        return std::nullopt;
    }
    return user.email;
}

int main() {
    int input_id = 42;
    
    // 有了 and_then 之后，链式调用，清爽多了
    auto result = find_user(input_id)
        .and_then(extract_email);
    
    // transform 可以在不解包的情况下对值做变换
    auto upper_result = result.transform([](const std::string& email) {
        std::string upper = email;
        for (char& c : upper) c = std::toupper(c);
        return upper;
    });
    
    if (upper_result) {
        std::cout << "邮箱(大写): " << *upper_result << "\n";
    } else {
        std::cout << "无法获取邮箱\n";
    }
    
    return 0;
}
```

在 GCC 14 上跑一下，这段代码完全通过，不需要任何额外依赖。`and_then` 的语义是：如果当前 `optional` 有值，就把这个值传给给定的函数，函数返回一个新的 `optional`；如果当前没有值，直接返回空的 `optional`，函数根本不会被调用。`transform` 类似，但给定的函数返回的是普通值而不是 `optional`，`transform` 会自动把它包一层。之前 `std::optional` 一直有半成品的感觉，现在终于补上了最关键的链式调用能力。而且这个特性已经在 C++23 里正式标准化了，Beman 的 `optional` 项目更多是在做进一步的扩展和探索。

除了 `optional` 扩展，Beman 里还有 `scopes`（作用域守卫相关）、`tasks`（异步任务抽象）、`any_view`（类型擦除的视图）等子项目。光看名字就能感觉到，它们瞄准的都是日常开发中确实会遇到的痛点。

## 还有一条路：个人库直接进标准

聊到这儿可能会有个疑问：是不是所有进标准的东西都得先经过 Boost 或者 Beman 这样的组织？答案是不是的。C++ 社区里有一批特别硬核的人，自己写了一个库，然后自己（或者联合其他人）写提案，经历 WG21 的重重评审，最终把库推进了标准。这条路径比走 Boost 或 Beman 更难，因为一个人要同时搞定实现、文档、提案文本、答辩，但确实有人做到了。

几个最典型的例子：Eric Niebler 的 **range-v3**<RefLink :id="5" preview="Eric Niebler, range-v3, C++20 ranges reference" /> 库在 GitHub 上公开后，基本上就是 C++20 ranges 的参考实现，很多教程在 C++20 支持还不完善的时候都还在引用 range-v3 的文档。Victor Zverovich 的 **{fmt}**<RefLink :id="6" preview="Victor Zverovich, {fmt}, std::format reference implementation" /> 在 `std::format` 还没被广泛支持的时候，几乎是所有 C++ 程序员的格式化方案。后来 `fmt` 直接变成了 `std::format` 的参考实现，Victor 本人也是提案的主要推动者。现在 `std::format` 在 C++20 里已经是标准的一部分了<RefLink :id="13" preview="P0645R10: Text Formatting for C++20" />，但在生产环境里有时候还是直接用 `fmt`，因为它的编译速度和错误信息在某些场景下比标准库实现更好。Howard Hinnant 的 **date**<RefLink :id="7" preview="Howard Hinnant, date library, C++20 chrono extension" /> 填补了 C++ 处理日期的巨大空白——在 C++20 引入 `<chrono>` 的时间点扩展之前，要在 C++ 里处理日期，要么用 C 时代的 `tm` 结构体（那个东西的坑可以写一整篇），要么引入第三方库——最终也推动了 C++20 `<chrono>` 的日历和时区支持。

然后是 `std::span`（C++20）和 `std::mdspan`（C++23）<RefLink :id="12" preview="cppreference: std::mdspan, C++23 multi-dimensional view" />。`span` 在现代 C++ 代码里几乎无处不在——只要有"一段连续内存的视图"这个需求，`span` 就比裸指针+长度好用得多。把函数签名从 `void process(uint8_t* data, size_t size)` 改成 `void process(std::span<uint8_t> data)` 之后，调用方的代码可读性提升了一个档次，而且再也不会出现"指针传对了但长度传错了"这种低级 bug。

```cpp
#include <span>
#include <vector>
#include <cstdint>
#include <iostream>

// 以前这么写，调用方必须保证 data 和 len 匹配，编译器帮不了你
// void process(uint8_t* data, size_t len);

// 现在这样写，span 自带长度信息，而且可以隐式从 vector、array、C 数组转换
void process(std::span<const uint8_t> data) {
    std::cout << "收到 " << data.size() << " 字节数据\n";
    for (size_t i = 0; i < data.size(); ++i) {
        std::cout << static_cast<int>(data[i]) << " ";
    }
    std::cout << "\n";
}

int main() {
    std::vector<uint8_t> vec = {1, 2, 3, 4, 5};
    
    // vector 直接传，完美
    process(vec);
    
    // 取子范围也方便
    process(std::span<uint8_t>(vec).subspan(1, 3));
    
    // C 数组也行
    uint8_t arr[] = {10, 20, 30};
    process(arr);
    
    return 0;
}
```

`mdspan` 解决的是多维数组视图的问题。C++ 里处理多维数组一直是个痛点——原生多维数组大小必须编译期确定，`vector<vector<T>>` 又有内存不连续的性能问题。`mdspan` 提供了一个多维的、非拥有的视图，而且它的布局映射是可定制的，这意味着可以用它来视图化行主序的 C 数组、列主序的 Fortran 数组、甚至自定义步长的图像缓冲区。这个库背后有一个相当庞大的联盟在推动，因为高性能计算领域对多维数组视图的需求太迫切了。

## 回头看整个图景

到这里这条链路就理清楚了。C++ 新特性进入标准，大致有三条路径：第一条是 Boost 路径，历史悠久但流程重，适合那些需要长时间打磨的、通用的基础设施；第二条是 Beman 路径，2024 年新启动的，专门为标准化设计的轻量流程，目标是成为一条高效的传送带；第三条是个人英雄路径，作者自己写库、自己推提案，难度最大但历史上成功案例不少。这三条路径不是互斥的，Beman 本身就有很多 Boost 的核心参与者，它更像是对 Boost 理念的一种补充而不是竞争，而那些个人库的作者很多也同时是 Boost 或 Beman 的贡献者。

C++ 标准化看起来像个黑箱——提案从哪来、怎么评审、为什么有些东西很快进标准有些等了十年，完全看不懂。但回头看其实没那么神秘，就是一群人通过不同的组织形式，在持续地把实战中验证过的设计往标准里推。搞明白这个之后，再看 C++26、C++29 的提案列表，感觉就完全不一样了——能看出哪些是 Beman 传送带上来的、哪些是个人库作者推的、哪些还在早期探索阶段，而不是面对一堆提案编号发懵。

<ReferenceCard title="参考文献">
  <ReferenceItem
    :id="1"
    author="Alexander Stepanov & Meng Lee"
    title="The Standard Template Library"
    publisher="HP Laboratories Technical Report 95-11"
    :year="1995"
    chapter="original STL proposal; algorithms + iterators + containers"
    url="https://www.stepanovpapers.com/"
  />
  <ReferenceItem
    :id="2"
    author="Beman Dawes et al."
    title="Boost C++ Libraries"
    publisher="boost.org"
    :year="1999"
    chapter="peer-reviewed, open-source C++ library collection; incubator for C++ standards"
    url="https://www.boost.org/"
  />
  <ReferenceItem
    :id="3"
    author="Beman Dawes"
    title="Boost Founder"
    publisher="Boost"
    :year="1999"
    chapter="passed away December 1, 2020; co-founder of Boost; pioneered library-driven C++ standardization; voting member of ISO C++ Standards Committee for 28 years"
    url="https://www.boost.org/users/people/beman_dawes.html"
  />
  <ReferenceItem
    :id="4"
    author="David Sankel"
    title="The Beman Project: A New Path for C++ Standardization"
    publisher="CppCon"
    :year="2024"
    chapter="libraries designed from day one for C++ standard proposals"
    url="https://github.com/beman-project"
  />
  <ReferenceItem
    :id="5"
    author="Eric Niebler"
    title="range-v3"
    publisher="GitHub"
    :year="2015"
    chapter="reference implementation for C++20 ranges; basis for standardization"
    url="https://github.com/ericniebler/range-v3"
  />
  <ReferenceItem
    :id="6"
    author="Victor Zverovich"
    title="{fmt}: A Modern C++ String Formatting Library"
    publisher="GitHub"
    :year="2012"
    chapter="reference implementation for C++20 std::format"
    url="https://github.com/fmtlib/fmt"
  />
  <ReferenceItem
    :id="7"
    author="Howard Hinnant"
    title="date: A C++ Library for Date and Time"
    publisher="GitHub"
    :year="2015"
    chapter="basis for C++20 chrono calendar and time zone extensions"
    url="https://github.com/HowardHinnant/date"
  />
  <ReferenceItem
    :id="8"
    author="Wikipedia contributors"
    title="Standard Template Library"
    publisher="Wikipedia"
    :year="2002"
    chapter="name origin: Standard Template Library; designed by Stepanov and Lee at HP Labs; SGI STL hash_map omitted from C++98"
    url="https://en.wikipedia.org/wiki/Standard_Template_Library"
  />
  <ReferenceItem
    :id="9"
    author="Alexander Stepanov"
    title="Interview by LoRusso"
    publisher="stepanovpapers.com"
    :year="1995"
    chapter="STL naming anecdote; Stepanov/Lee initials coincidence"
    url="https://www.stepanovpapers.com/LoRusso_Interview.htm"
  />
  <ReferenceItem
    :id="10"
    author="Wikipedia contributors"
    title="History of the Standard Template Library"
    publisher="Wikipedia"
    :year="2006"
    chapter="November 1993 presentation; July 1994 final approval; 'overwhelmingly favorable' committee response"
    url="https://en.wikipedia.org/wiki/History_of_the_Standard_Template_Library"
  />
  <ReferenceItem
    :id="11"
    author="cppreference.com"
    title="std::optional"
    publisher="cppreference.com"
    :year="2023"
    chapter="C++23 monadic operations: transform, and_then, or_else"
    url="https://en.cppreference.com/w/cpp/utility/optional"
  />
  <ReferenceItem
    :id="12"
    author="cppreference.com"
    title="std::mdspan"
    publisher="cppreference.com"
    :year="2023"
    chapter="C++23 multidimensional array view; customizable layout mapping"
    url="https://en.cppreference.com/w/cpp/container/mdspan"
  />
  <ReferenceItem
    :id="13"
    author="Victor Zverovich"
    title="P0645R10: Text Formatting"
    publisher="WG21 / ISO C++ Committee"
    :year="2019"
    chapter="std::format proposal for C++20; based on {fmt} library"
    url="https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0645r10.html"
  />
</ReferenceCard>
