---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 Talk Notes — C++: Some Assembly Required by Matt Godbolt'
difficulty: intermediate
order: 5
platform: host
reading_time_minutes: 20
speaker: Matt Godbolt
tags:
- cpp-modern
- host
- intermediate
talk_title: 'C++: Some Assembly Required'
title: Boost, Beman, and the Path to C++ Standardization
video_bilibili: https://www.bilibili.com/video/BV1ptCCBKEwW?p=2
video_youtube: https://www.youtube.com/watch?v=zoYT7R94S3c
translation:
  source: documents/vol10-open-lecture-notes/cppcon/2025/02-some-assembly-required/05-boost-beman-and-standardization.md
  source_hash: 44f3bf88b72a14b5055e10785b3bdad49cbc74b8d226e3f7a0b21fbd2fb7312a
  translated_at: '2026-06-16T03:51:43.848759+00:00'
  engine: anthropic
  token_count: 4105
---
# Boost: This is What the C++ Standard Library's "Backyard" Looks Like

When learning C++, many people have a confusion: where exactly do the things in the standard library come from? Did the committee just have a meeting one day where a bunch of big shots said, "Let's add ``shared_ptr``"? Or is there a more systematic process? After going through historical materials and clarifying this thread, the conclusion is striking—it turns out that almost all components we use daily come from the same place.

## First, Let's Clarify the Relationship Between STL and the Standard Library

Many people use "STL" and "C++ Standard Library" interchangeably. After all, in daily coding, we ``#include <vector>`` and say "we used STL", and no one will correct you. But strictly speaking, these are two different things, and understanding this distinction prevents confusion when looking at history later.

The full name of STL is "Standard Template Library"<RefLink :id="8" preview="Wikipedia: Standard Template Library, name origin and history" />—interestingly, the initials of Stepanov and Lee are also S and L, which many people treat as an interesting coincidence<RefLink :id="9" preview="Stepanov interview, STL naming anecdote" />. This library was developed by Alexander Stepanov and Meng Lee<RefLink :id="1" preview="Stepanov & Lee, The Standard Template Library, HP Labs, 1995" /> while at HP. Although Stepanov is now retired, what he did back then set the tone for C++. The concepts inside STL—iterators, separation of algorithms from containers, complexity guarantees—looking at 1994, these were simply ahead of their time. Later, this proposal received final approval at the ANSI/ISO committee meeting in July 1994, and the committee's response was described as "overwhelmingly favorable"<RefLink :id="10" preview="Wikipedia: History of the STL, committee approval" />. You have to realize that was the nineties; C++ standardization itself was still in its early stages. To pass by such a landslide shows that this thing was indeed done beautifully.

But STL is just that library of Stepanov and theirs. Later, parts of it were absorbed into the standard, but not all. For example, SGI's STL implementation had ``hash_map`` early on<RefLink :id="8" preview="Wikipedia: STL, SGI implementation and hash_map history" />, but the C++98 standard didn't include it until C++11 brought it in as ``unordered_map``. So the scope of the standard library is much larger than STL. STL is the most core and dazzling part of it, but not the whole thing.

## So Where Do the Other Things in the Standard Library Come From?

``shared_ptr`` is not STL, ``tuple`` is not STL, ``regex`` is not STL, and ``filesystem`` is not STL. How did they get into the standard library? The answer is two words: Boost.

Hearing this answer for the first time might be surprising, because many tutorials mention Boost only in passing, saying "this is a third-party library, just know it exists." But looking at Boost's history reveals the complete opposite—it's not that Boost borrowed from the standard library's fame, but rather the standard library drew nourishment from Boost for a quarter of a century.

The Boost project was first officially released in 1999<RefLink :id="2" preview="Beman Dawes, Boost Libraries, 1999" />, almost in sync with the C++ standardization process. One of its positions—note, **only one of them**—is to serve as a testing ground for high-quality libraries: someone has a good idea, implements it in Boost first, lets everyone use it, criticize it, and offer suggestions. Once it's fully validated by industry, they consider pushing it into the standard. But this "testing ground" metaphor has its limitations—we'll elaborate on that later.

Below are some things we use every day but might not realize originated from Boost: ``shared_ptr``/``weak_ptr`` come from Boost.SmartPtr, ``function``/``bind`` come from Boost.Function and Boost.Bind, ``tuple`` comes from Boost.Tuple, ``regex`` comes from Boost.Regex, ``array`` comes from Boost.Array, ``unordered_map``/``unordered_set`` come from Boost.Unordered, ``chrono`` comes from Boost.Chrono, and ``filesystem`` comes from Boost.Filesystem. These aren't obscure components; they are things C++ programmers touch every day when writing code. Each of them survived in Boost for anywhere from three to five years to over a decade, was tested by countless projects in real environments, had bugs fixed, and API design polished, and only then was it "regularized."

## Hands-on Verification: Seeing the Origins of Boost and the Standard Library

Just talking isn't enough; let's run some code to get a feel for it. The local environment is Arch Linux WSL, GCC 16.1.1, and Boost 1.91 installed via pacman.

First, let's look at a classic example—``shared_ptr``. The Boost version and the standard library version have almost identical interfaces. This isn't a coincidence; it's because the standard library version was copied directly from the Boost version:

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

Running result:

```text
use_count: 1
value: 42
after copy, use_count: 2
```

This example itself has little technical depth, but the core point is this: ``use_count()``, ``make_shared``, copy semantics—these API designs weren't thought up by the committee sitting in a meeting room. They were settled after the Boost community used them for several years and stepped into countless pits. The standardization process is more like "ratification" than "invention."

Let's look at a more interesting example, ``boost::filesystem`` and ``std::filesystem``. The Boost version appeared much earlier; C++17 only brought the filesystem library into the standard. The script below compares the usage differences between the two:

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

Running result (GCC 16.1.1, ``-std=c++20``):

```text
created: "/tmp/test_dir"
removed: "/tmp/test_dir"
```

::: details Why does the output have quotes?
``std::filesystem::path``'s ``operator<<`` wraps the path output in double quotes; this is behavior mandated by the standard. If you don't want quotes, you can change it to ``std::cout << p.string() << "\n"``.
:::

You will find that except for the different header files and namespaces, the code logic doesn't need to change at all. This is the value of Boost as a "testing ground"—in those years when the standard library didn't have filesystem support, it gave C++ programmers a unified, cross-platform filesystem solution. When C++17 finally standardized ``std::filesystem``, the API was already very mature, and migration was almost zero-cost.

## But Boost Isn't Just the Standard Library's "Reserve Team"

Here is a common misconception: that the ultimate goal of everything in Boost is to enter the standard library, and what didn't get in is a "failure." This idea is completely wrong. There are many things in Boost that are fundamentally unsuitable for the standard library, but they are incredibly powerful in their respective domains. For example, Boost.Spirit is a parser framework based on combinators that lets you define parsing rules using EBNF-like syntax, writing parsers directly in C++. This is too domain-specific; the standard library wouldn't include it, but for text parsing, it's much easier to use than writing state machines by hand. Boost.Python is an interoperability library between C++ and Python, allowing you to expose C++ interfaces to Python almost painlessly. Putting something tied to a specific language in the standard library is clearly inappropriate. Boost.Compute is a GPGPU computing library similar to OpenCL, strongly tied to the hardware platform, so it shouldn't be in the standard either. Boost.Beast is an HTTP and WebSocket library based on Boost.Asio, now used by many people doing C++ backend.

So Boost's real positioning is: it is both one of the sources of the standard library and an independent collection of high-quality C++ libraries. Some things "graduate" to the standard library, while others keep shining in Boost. The two are not contradictory.

---

# From Boost to Beman: How the C++ Standard Library's "Conveyor Belt" Turns

## Where Exactly Does the "Testing Ground" Metaphor Go Wrong?

Earlier, we mentioned that one of Boost's positions is a "testing ground." This statement is further simplified in many tutorials to "Boost is the testing ground for the C++ standard library." But many people understand this as "everything in Boost will eventually enter the standard." This understanding is problematic because it completely ignores the two key questions of "how it enters" and "when it enters."

In reality, the relationship between Boost and the C++ Standards Committee is far less simple and direct than the three words "testing ground" imply. Boost has its own governance structure, review process, and release rhythm, while C++ standardization follows the ISO process. The goals of the two systems are not entirely consistent. Some libraries in Boost are designed to be very general and flexible, but precisely because they are too flexible, standardization often requires extensive trimming and adjustment, a process that can take years or even longer. So you see many libraries in Boost take several C++ standard versions from proposal to final adoption. This isn't because the committee is inefficient, but because the docking cost of the two systems is indeed high.

## The Beman Project: That "Conveyor Belt" Launched in 2024

In 2024, David Sankel announced the Beman project<RefLink :id="4" preview="David Sankel, Beman Project, CppCon 2024" />. At first glance, you might think "another Boost substitute?", but looking closer reveals it's not that at all.

Beman's positioning is very clear: every library in it, from day one of its inception, has the goal of entering the C++ standard. This isn't "make a good library first and see if there's a chance to standardize later," but rather "we are going to make a proposal that can be pushed directly to WG21, complete with a full reference implementation." You can think of it as a conveyor belt—libraries complete design, implementation, and practical testing in Beman, then are pushed directly onto the standardization track with a paper.

This positioning means Beman has done a lot of streamlining in its process. Boost's review process is heavy; you have to consider compatibility with dozens of other Boost libraries, meet Boost's code style requirements, and pass community voting. Beman, frankly, is aimed straight at standardization, so the overhead is much lower. There's no need to balance between "making a general library" and "making a standard proposal" because in Beman, these two things are the same thing.

Many people previously wondered, "why not just take things from Boost into the standard?" The reason is simple—Boost's design constraints and the standard's constraints aren't the same. Direct porting often doesn't work, and refactoring a library already rooted in the Boost ecosystem has high political and technical costs. Beman effectively bypasses this issue by designing from the start with the premise of "being able to enter the standard."

## What's in Beman Now?

Currently, Beman has about 8 active repositories<RefLink :id="4" preview="Beman Project, GitHub organization" />, one of which is an example library ``exemplar``, showing how a Beman library should organize code, write documentation, and accompany proposals. This ``exemplar`` itself has simple functionality, but its value as a "template" is significant.

Several sub-projects in practical directions are worth watching. For example, extensions to ``optional``—C++23 finally added ``transform`` and ``and_then`` to ``std::optional``<RefLink :id="11" preview="cppreference: std::optional, C++23 monadic operations" />, and Beman's Optional26 project aims to make further extensions targeting C++26 on this basis. When writing code, every time we encounter a "possibly no value" scenario, we struggle between ``std::optional`` and raw pointers. Using a raw pointer, ``nullptr`` can mean either "no value" or "error occurred," mixing semantics. Every time you see ``if (ptr != nullptr)``, you aren't sure if this null is business logic "none" or logical "error." Using ``std::optional`` clears up the semantics, but chaining operations is very painful.

Let's take a specific example. Suppose I have a flow that looks up user info from a user ID, then extracts an email from the user info. Using ``std::optional`` pre-C++23, you have to write it like this:

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

Look at this nesting; even with just two layers, it's already annoying. In actual business code, three or four layers of nesting are common. Each layer requires manually checking ``has_value()``, manually unwrapping, and then passing it to the next layer. Rust's ``Option::and_then`` does this well, but C++ has long lacked a corresponding mechanism.

Now Beman's ``optional`` extension fills this gap. With ``transform`` and ``and_then``, the same logic can be written like this:

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

Running this on GCC 14, this code passes completely without any extra dependencies. The semantics of ``and_then`` are: if the current ``optional`` has a value, pass that value to the given function, which returns a new ``optional``; if there is no value, return an empty ``optional`` directly, and the function won't be called at all. ``transform`` is similar, but the given function returns a normal value instead of ``optional``, and ``transform`` automatically wraps it. ``std::optional`` always felt half-baked before; now it finally has the most critical chaining ability. Moreover, this feature has been officially standardized in C++23, and Beman's ``optional`` project is mostly doing further extensions and exploration.

Besides ``optional`` extensions, Beman also has sub-projects like ``scopes`` (scope guard related), ``tasks`` (async task abstraction), and ``any_view`` (type-erased views). Just looking at the names, you can feel they are aiming at pain points truly encountered in daily development.

## There's Another Path: Individual Libraries Enter the Standard Directly

At this point, you might have a question: do all things entering the standard have to go through organizations like Boost or Beman? The answer is no. There is a group of particularly hardcore people in the C++ community who wrote a library themselves, then wrote a proposal themselves (or jointly with others), went through the heavy reviews of WG21, and finally pushed the library into the standard. This path is harder than going through Boost or Beman because one person has to handle implementation, documentation, proposal text, and defense simultaneously, but people have indeed done it.

A few typical examples: Eric Niebler's **range-v3**<RefLink :id="5" preview="Eric Niebler, range-v3, C++20 ranges reference" /> library, after being open-sourced on GitHub, basically served as the reference implementation for C++20 ranges, and many tutorials still cited range-v3 documentation when C++20 support wasn't complete. Victor Zverovich's **{fmt}**<RefLink :id="6" preview="Victor Zverovich, {fmt}, std::format reference implementation" /> was almost every C++ programmer's formatting solution when ``std::format`` wasn't widely supported. Later ``fmt`` directly became the reference implementation for ``std::format``, and Victor himself was a main driver of the proposal. Now ``std::format`` is part of the standard in C++20<RefLink :id="13" preview="P0645R10: Text Formatting for C++20" />, but in production environments, people sometimes still use ``fmt`` directly because its compilation speed and error messages are better than the standard library implementation in some scenarios. Howard Hinnant's **date**<RefLink :id="7" preview="Howard Hinnant, date library, C++20 chrono extension" /> library filled the huge gap in C++ date handling—before C++20 introduced time point extensions for ``<chrono>``, handling dates in C++ meant either using the C-era ``tm`` struct (whose pitfalls could fill a whole article) or introducing a third-party library—ultimately driving C++20's calendar and time zone support in ``<chrono>``.

Then there is ``std::span`` (C++20) and ``std::mdspan`` (C++23)<RefLink :id="12" preview="cppreference: std::mdspan, C++23 multi-dimensional view" />. ``span`` is almost everywhere in modern C++ code—whenever there's a need for "a view of a contiguous block of memory," ``span`` is much easier to use than a raw pointer plus length. Changing a function signature from ``void process(uint8_t* data, size_t size)`` to ``void process(std::span<uint8_t> data)`` improves caller code readability by a level, and you never get low-level bugs like "pointer passed correctly but length wrong."

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

``mdspan`` solves the problem of multi-dimensional array views. Handling multi-dimensional arrays in C++ has always a pain point—native multi-dimensional arrays must have sizes known at compile time, and ``vector<vector<T>>`` has performance issues due to non-contiguous memory. ``mdspan`` provides a multi-dimensional, non-owning view, and its layout mapping is customizable, meaning it can be used to view row-major C arrays, column-major Fortran arrays, or even image buffers with custom strides. A fairly large alliance is driving this library because the high-performance computing field's need for multi-dimensional array views is too urgent.

## Looking Back at the Whole Picture

By now, this chain is clear. There are roughly three paths for new C++ features to enter the standard: the first is the Boost path, with a long history but heavy process, suitable for general infrastructure that needs long polishing; the second is the Beman path, launched in 2024, a lightweight process designed specifically for standardization, aiming to be an efficient conveyor belt; the third is the individual hero path, where the author writes the library and pushes the proposal themselves, hardest but with many historical successes. These three paths aren't mutually exclusive. Beman itself has many core Boost participants; it's more of a supplement to Boost's philosophy than a competitor, and many of those individual library authors are also contributors to Boost or Beman.

C++ standardization looks like a black box—where proposals come from, how they are reviewed, why some things enter the standard quickly while others wait ten years—is completely incomprehensible. But looking back, it's not that mysterious. It's just a group of people, through different organizational forms, continuously pushing designs proven in practice into the standard. After understanding this, looking at the C++26 and C++29 proposal lists feels completely different—you can see which came from the Beman conveyor belt, which were pushed by individual library authors, and which are still in early exploration, instead of staring blankly at a list of proposal numbers.

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
