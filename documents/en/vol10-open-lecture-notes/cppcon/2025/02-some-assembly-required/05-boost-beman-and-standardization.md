---
title: Boost, Beman, and the Path to C++ Standardization
description: 'CppCon 2025 Talk Notes — C++: Some Assembly Required by Matt Godbolt'
conference: cppcon
conference_year: 2025
talk_title: 'C++: Some Assembly Required'
speaker: Matt Godbolt
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
tags:
- cpp-modern
- host
- intermediate
difficulty: intermediate
platform: host
cpp_standard:
- 17
- 20
chapter: 2
order: 5
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/05-boost-beman-and-standardization.md
  source_hash: 3d5624c64346be62c1181dc160e1db7e559cdb3fad0de459d4e53e4323112c56
  translated_at: '2026-05-20T04:39:13.047092+00:00'
  engine: anthropic
  token_count: 4113
---
# Boost: Inside the C++ Standard Library's "Back Garden"

When learning C++, many people share a common confusion: where exactly do the things in the standard library come from? Did the committee just sit in a room one day and have a bunch of big names decree "let's throw in a `shared_ptr`"? Or is there a more systematic process? After digging through the historical records and piecing together the timeline, the conclusion is striking—almost all the components we use daily come from the same place.

## Clearing Up the Relationship Between STL and the Standard Library

Many people use "STL" and "C++ standard library" interchangeably. After all, in day-to-day coding, when you write `#include <vector>` and say "I used STL," nobody is going to correct you. But strictly speaking, these are two different things, and getting this straight is essential for making sense of the history.

STL stands for "Standard Template Library"<RefLink :id="8" preview="Wikipedia: Standard Template Library, name origin and history" />—interestingly, the initials of Stepanov and Lee happen to be S and L as well, a coincidence many find amusing<RefLink :id="9" preview="Stepanov interview, STL naming anecdote" />. This library was created by Alexander Stepanov and Meng Lee<RefLink :id="1" preview="Stepanov & Lee, The Standard Template Library, HP Labs, 1995" /> while they were at HP. Stepanov has since retired, but the work he did back then essentially set the tone for C++. The concepts inside STL—separating iterators, algorithms, and containers, along with time complexity guarantees—seen from the perspective of 1994, were simply generations ahead. The proposal was ultimately approved at the ANSI/ISO committee meeting in July 1994, and the committee's response was described as "overwhelmingly favorable"<RefLink :id="10" preview="Wikipedia: History of the STL, committee approval" />. Keep in mind this was the nineties, when C++ standardization itself was still in its early stages. Passing by such an overwhelming margin shows just how brilliantly executed it was.

But STL was simply Stepanov and Lee's library. Later, parts of it were absorbed into the standard, but not all of it. For example, SGI's STL implementation already had `hash_map`<RefLink :id="8" preview="Wikipedia: STL, SGI implementation and hash_map history" />, but the C++98 standard didn't include it, and it only entered the standard in C++11 in the form of `unordered_map`. So the standard library's scope is much broader than STL. STL is the most core, most dazzling piece, but it's not everything.

## So Where Did Everything Else in the Standard Library Come From?

`shared_ptr` is not STL, `tuple` is not STL, `regex` is not STL, and `filesystem` is not STL either. How did they get into the standard library? The answer comes down to two words: Boost.

Hearing this answer for the first time might be surprising, because many tutorials mention Boost only in passing, saying "it's a third-party library, just be aware of it." But if you look into Boost's history, you'll find the situation is completely reversed—it's not that Boost basked in the standard library's glory, but rather that the standard library drew nourishment from Boost for a quarter of a century.

The Boost project was first officially released in 1999<RefLink :id="2" preview="Beman Dawes, Boost Libraries, 1999" />, almost in lockstep with the C++ standardization process. One of its roles—and note, **only one** of them—was to serve as a testing ground for high-quality libraries: someone has a good idea, implements it in Boost, lets people use it, complain about it, and offer feedback. Once it's been thoroughly validated by industry, then they consider pushing it into the standard. But this "testing ground" metaphor has its limitations—we'll get into that in detail later.

Below are some things we use every day but might not realize originated in Boost: `shared_ptr`/`weak_ptr` came from Boost.SmartPtr, `function`/`bind` came from Boost.Function and Boost.Bind, `tuple` came from Boost.Tuple, `regex` came from Boost.Regex, `array` came from Boost.Array, `unordered_map`/`unordered_set` came from Boost.Unordered, `chrono` came from Boost.Chrono, and `filesystem` came from Boost.Filesystem. These aren't obscure components; they're things C++ programmers touch every single day. Each of them survived in Boost for anywhere from three to five years on the short end, to over a decade on the long end. They were tested by countless projects in real-world environments, bugs were mostly ironed out, API designs were polished, and only then were they "graduated."

## Let's Verify This: Tracing the Roots of Boost and the Standard Library

Talking is cheap, so let's run some code to get a feel for it. The local environment is Arch Linux WSL, GCC 16.1.1, with Boost 1.91 installed via pacman.

First, let's look at the most classic example—`shared_ptr`. The Boost version and the standard library version have nearly identical interfaces. This is no coincidence; the standard library version was directly modeled after the Boost version:

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

Output:

```text
use_count: 1
value: 42
after copy, use_count: 2
```

There's nothing technically impressive about this example itself, but the core point is this: the API designs for `use_count()`, `make_shared`, and copy semantics—these weren't dreamed up by a committee sitting in a conference room. They were distilled from years of use by the Boost community and countless pitfalls along the way. The standardization process was more like "retroactive recognition" than "invention."

Let's look at a more interesting example: `boost::filesystem` and `std::filesystem`. The Boost version appeared much earlier; it wasn't until C++17 that the filesystem library was brought into the standard. The following script compares the usage differences between the two:

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

Output (GCC 16.1.1, `-std=c++20`):

```text
created: "/tmp/test_dir"
removed: "/tmp/test_dir"
```

::: details Why does the output have quotes?
`std::filesystem::path`'s `operator<<` wraps path output in double quotes, which is mandated by the standard. If you don't want the quotes, you can change it to `std::cout << p.string() << "\n"`.
:::

You'll notice that apart from the different headers and namespaces, the code logic doesn't need to change at all. This is the value of Boost as a "testing ground"—in those years when the standard library had no filesystem support, it gave C++ programmers a unified, cross-platform solution for filesystem operations. By the time C++17 finally standardized `std::filesystem`, the API was already very mature, making migration almost zero-cost.

## But Boost Isn't Just the Standard Library's "Farm Team"

There's a common misconception here that everything in Boost ultimately aims to enter the standard library, and anything that hasn't is a "failure." This idea is completely wrong. There are many things in Boost that are fundamentally unsuitable for the standard library, yet are incredibly powerful in their respective domains. For example, Boost.Spirit is a combinator-based parser framework that lets you define parsing rules using EBNF-like syntax, writing parsers directly in C++. This is far too domain-specific for the standard library to adopt, but if you need to do text parsing, it's much more pleasant than hand-writing state machines. Boost.Python is an interoperability library between C++ and Python that lets you expose C++ interfaces to Python almost painlessly; something tied to a specific language like this clearly doesn't belong in the standard library. Boost.Compute is a GPGPU computing library similar to OpenCL, tightly coupled to hardware platforms, so it shouldn't be in the standard either. Boost.Beast is an HTTP and WebSocket library built on top of Boost.Asio, and many people building C++ backends use it today.

So Boost's true positioning is this: it is both one of the wellsprings of the standard library and an independent, high-quality C++ library collection. Some things "graduate" into the standard library, while others keep shining within Boost. The two are not contradictory.

---

# From Boost to Beman: How the C++ Standard Library's "Conveyor Belt" Works

## What Exactly Is Wrong With the "Testing Ground" Metaphor

Earlier we mentioned that one of Boost's roles is as a "testing ground," a description that many tutorials have further simplified to "Boost is the testing ground for the C++ standard library." But many people interpret this as "everything in Boost will eventually enter the standard." This understanding is deeply flawed because it completely ignores the two critical questions of "how does it enter" and "when does it enter."

In reality, the relationship between Boost and the C++ standard committee is far less simple and direct than the three words "testing ground" imply. Boost has its own governance structure, its own review process, and its own release cadence, while C++ standardization follows the ISO process. The goals of these two systems don't perfectly align. Some libraries in Boost are designed to be very generic and flexible, but precisely because they're so flexible, they actually require extensive trimming and adjustment during standardization—a process that can take years or even longer. So when you see many Boost libraries taking several C++ standard versions from proposal to final adoption, it's not because the committee is inefficient, but because the integration cost between the two systems is genuinely high.

## The Beman Project: The "Conveyor Belt" Launched in 2024

In 2024, David Sankel announced the Beman project<RefLink :id="4" preview="David Sankel, Beman Project, CppCon 2024" />. At first glance, you might think "here comes another Boost alternative?" but looking closer reveals that it's nothing of the sort.

Beman's positioning is extremely clear: every library in it, from day one of its inception, has the goal of entering the C++ standard. This isn't "let's build a useful library first and see if there's a chance to standardize it later," but rather "we are going to build a proposal that can be pushed directly to WG21, complete with a reference implementation." You can think of it as a conveyor belt—libraries complete their design, implementation, and real-world validation within Beman, and then are pushed straight onto the standardization track with an accompanying paper.

This positioning means Beman has significantly streamlined its processes. Boost's review process is quite heavy: you have to consider compatibility with dozens of other Boost libraries, meet Boost's code style requirements, and pass community votes. Beman, frankly, is aimed squarely at standardization. The overhead is much lower, and there's no need to balance "building a general-purpose library" against "making a standard proposal," because in Beman, these two things are one and the same.

Many people previously wondered "why not just take things directly from Boost into the standard?" The reason is actually simple—Boost's design constraints and the standard's constraints are different, and directly porting things over often doesn't work. Retrofitting a library that's already deeply rooted in the Boost ecosystem carries high political and technical costs. Beman essentially sidesteps this problem by designing from scratch with "being standardizable" as a prerequisite.

## What's in Beman Right Now

Currently, Beman has about eight active repositories<RefLink :id="4" preview="Beman Project, GitHub organization" />, one of which is an example library called `exemplar` that demonstrates how a Beman library should organize its code, write documentation, and package an accompanying proposal. This `exemplar` is functionally simple, but its value as a "template" is significant.

Several practical subprojects are worth watching. For example, there's an extension to `optional`—C++23 finally added `transform` and `and_then` to `std::optional`<RefLink :id="11" preview="cppreference: std::optional, C++23 monadic operations" />, and Beman's Optional26 project aims to build further extensions on top of this for C++26. When writing code, every time you encounter a "might not have a value" scenario, you wrestle between `std::optional` and raw pointers. If you use a raw pointer, `nullptr` can mean either "no value" or "an error occurred," and the semantics get muddled. Every time you see `if (ptr != nullptr)`, you're never quite sure if this null is a business-logic "absent" or a logical "error." If you use `std::optional`, the semantics are clear, but chaining operations is incredibly painful.

Let's look at a concrete example. Suppose we have a workflow where we look up user info from a user ID, then extract the email from that user info. Using `std::optional` prior to C++23, you'd have to write it like this:

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

Look at this nesting—even with just two levels, it's already annoying. In real business code, three or four levels of nesting are common, and at each level you manually check `has_value()`, manually unwrap the value, and then pass it to the next layer. Rust's `Option::and_then` does a great job here, and C++ has long lacked a corresponding mechanism.

Now, Beman's `optional` extension is here to fill that gap. With `transform` and `and_then`, the same logic can be written like this:

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

Running this on GCC 14, the code passes completely with no extra dependencies. The semantics of `and_then` are: if the current `optional` has a value, pass that value to the given function, which returns a new `optional`; if there's no value, directly return an empty `optional`, and the function is never called. `transform` is similar, but the given function returns a plain value instead of an `optional`, and `transform` automatically wraps it. `std::optional` always felt half-finished before, and now it's finally been equipped with the most critical chaining capability. Moreover, this feature has already been formally standardized in C++23. Beman's `optional` project is more about further extension and exploration.

Beyond the `optional` extension, Beman also has subprojects like `scopes` (related to scope guards), `tasks` (async task abstractions), and `any_view` (type-erased views). Just from the names, you can tell they're aimed at genuine pain points encountered in day-to-day development.

## There's Another Path: Individual Libraries Going Straight Into the Standard

At this point, you might wonder: does everything that enters the standard have to go through an organization like Boost or Beman first? The answer is no. There's a group of particularly hardcore people in the C++ community who wrote a library themselves, then wrote (or co-wrote) a proposal, went through the rigorous WG21 review process, and ultimately pushed their library into the standard. This path is harder than going through Boost or Beman, because one person has to handle the implementation, documentation, proposal text, and defense all at once—but people have indeed done it.

A few quintessential examples: Eric Niebler's **range-v3**<RefLink :id="5" preview="Eric Niebler, range-v3, C++20 ranges reference" />, after being published on GitHub, essentially served as the reference implementation for C++20 ranges. Many tutorials were still citing range-v3's documentation when C++20 support wasn't yet mature. Victor Zverovich's **{fmt}**<RefLink :id="6" preview="Victor Zverovich, {fmt}, std::format reference implementation" /> was practically every C++ programmer's formatting solution back when `std::format` wasn't yet widely supported. Later, `fmt` directly became the reference implementation for `std::format`, with Victor himself as the proposal's primary driver. Now that `std::format` is part of the standard in C++20<RefLink :id="13" preview="P0645R10: Text Formatting for C++20" />, people in production environments sometimes still use `fmt` directly because its compilation speed and error messages are better than the standard library implementation in certain scenarios. Howard Hinnant's **date**<RefLink :id="7" preview="Howard Hinnant, date library, C++20 chrono extension" /> filled a massive gap in C++ date handling—before C++20 introduced the time point extensions to `<chrono>`, handling dates in C++ meant either using the C-era `tm` struct (whose pitfalls could fill an entire article) or pulling in a third-party library. This ultimately drove the calendar and time zone support in C++20's `<chrono>`.

Then there's `std::span` (C++20) and `std::mdspan` (C++23)<RefLink :id="12" preview="cppreference: std::mdspan, C++23 multi-dimensional view" />. `span` is practically ubiquitous in modern C++ code—whenever you have a need for "a view over a contiguous block of memory," `span` is far more pleasant to use than a raw pointer plus a length. Changing a function signature from `void process(uint8_t* data, size_t size)` to `void process(std::span<uint8_t> data)` dramatically improves the readability of the caller's code, and you never again see those silly bugs where "the pointer was passed correctly but the length was wrong."

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

`mdspan` solves the problem of multi-dimensional array views. Handling multi-dimensional arrays in C++ has always been a pain point—native multi-dimensional arrays require compile-time sizes, and `vector<vector<T>>` has performance issues due to non-contiguous memory. `mdspan` provides a multi-dimensional, non-owning view, and its layout mapping is customizable, meaning you can use it to view row-major C arrays, column-major Fortran arrays, or even image buffers with custom strides. A fairly large consortium is behind this library because the high-performance computing community's need for multi-dimensional array views is incredibly urgent.

## Looking Back at the Big Picture

By this point, the pipeline is clear. New C++ features enter the standard through roughly three paths. The first is the Boost path—historically established but process-heavy, suited for general-purpose infrastructure that needs extended polishing. The second is the Beman path—newly launched in 2024, a lightweight process designed specifically for standardization, aiming to be an efficient conveyor belt. The third is the individual hero path, where the author writes the library and pushes the proposal themselves—hardest of all, but with no shortage of historical success stories. These three paths aren't mutually exclusive. Beman itself has many core Boost participants, and it's more of a complement to Boost's philosophy than a competitor. Meanwhile, many of those individual library authors are also contributors to Boost or Beman.

C++ standardization can look like a black box—where proposals come from, how they're reviewed, why some things enter the standard quickly while others wait ten years—it all seems incomprehensible. But looking back, it's not really that mysterious. It's just a group of people, through different organizational forms, continuously pushing battle-tested designs into the standard. Once you understand this, looking at the proposal lists for C++26 and C++29 feels completely different. You can spot which ones came off the Beman conveyor belt, which ones were pushed by individual library authors, and which ones are still in early exploration—instead of just staring blankly at a bunch of proposal numbers.

<ReferenceCard title="References">
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
