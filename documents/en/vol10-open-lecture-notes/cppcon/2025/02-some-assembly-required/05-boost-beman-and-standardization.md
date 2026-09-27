---
chapter: 2
conference: cppcon
conference_year: 2025
cpp_standard:
- 17
- 20
description: 'CppCon 2025 talk notes — C++: Some Assembly Required by Matt Godbolt'
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
  translated_at: '2026-09-26T15:36:50+00:00'
  engine: anthropic
  token_count: 4400
---
# Boost: So This Is What the C++ Standard Library's "Backyard" Looks Like

When learning C++, a puzzle occurs to a lot of people: where do the things in the standard library actually come from? Did the committee hold a meeting one day where a room full of luminaries said, "let's toss a `shared_ptr` in there"? Or is there a more systematic process? After going through the historical material and getting this thread untangled, the conclusion sticks with you: it turns out that almost every component we use daily comes from the same place.

## First, Let's Get the STL and the Standard Library Straight

Plenty of people use "STL" and "the C++ standard library" interchangeably. After all, in everyday coding you `#include <vector>`, say you "used the STL", and nobody corrects you. Strictly speaking, though, these are two different things, and sorting them out now is what keeps the history from becoming a muddle later.

The full name of STL is "Standard Template Library"<RefLink :id="8" preview="Wikipedia: Standard Template Library, name origin and history" />—amusingly, the initials of Stepanov and Lee are also S and L, which many people take for a fun coincidence<RefLink :id="9" preview="Stepanov interview, STL naming anecdote" />. Alexander Stepanov and Meng Lee built the library<RefLink :id="1" preview="Stepanov & Lee, The Standard Template Library, HP Labs, 1995" /> while they were at HP. Stepanov is retired now, but what he did back then arguably set the tone for C++ as a whole. The ideas inside the STL—iterators, algorithms kept separate from containers, complexity guarantees—seen from 1994, they were simply in a different league. The proposal won final approval at the ANSI/ISO committee meeting in July 1994, and the committee's response was described as "overwhelmingly favorable"<RefLink :id="10" preview="Wikipedia: History of the STL, committee approval" />. Bear in mind this was the nineties—C++ standardization itself was still in its early days—and sailing through by that kind of margin tells you the work was genuinely well done.

But the STL is just that one library from Stepanov and company. Parts of it were later absorbed into the standard—only parts. SGI's STL implementation, for example, already had `hash_map`<RefLink :id="8" preview="Wikipedia: STL, SGI implementation and hash_map history" />, yet C++98 left it out; it didn't enter the standard until C++11, as `unordered_map`. So the standard library is much bigger than the STL: the STL is its most central, most dazzling piece, but not the whole of it.

## So Where Does the Rest of the Standard Library Come From

`shared_ptr` isn't the STL. `tuple` isn't the STL. `regex` isn't the STL. And `filesystem` isn't the STL either. So how did they get into the standard library? The answer fits in one word: Boost.

The first time you hear that, it can be surprising, because plenty of tutorials wave Boost off in a single line—"it's a third-party library, just know it exists". But skim Boost's history and the picture flips entirely: it isn't Boost basking in the standard library's reflected glory—it's the standard library drawing a quarter of a century of nourishment from Boost.

The Boost project's first official release came in 1999<RefLink :id="2" preview="Beman Dawes, Boost Libraries, 1999" />, almost in lockstep with the C++ standardization effort. One part of its charter—and note, **only one part**—was to serve as a testing ground for high-quality libraries: someone has a good idea, implements it in Boost first, and lets everyone use it, complain about it, and file suggestions; once industry has validated it thoroughly enough, pushing it toward the standard enters the conversation. But the "testing ground" metaphor has its limits—we'll come back to that in detail.

Here is a taste of the things we use every day, possibly without realizing their Boost pedigree: `shared_ptr`/`weak_ptr` from Boost.SmartPtr, `function`/`bind` from Boost.Function and Boost.Bind, `tuple` from Boost.Tuple, `regex` from Boost.Regex, `array` from Boost.Array, `unordered_map`/`unordered_set` from Boost.Unordered, `chrono` from Boost.Chrono, and `filesystem` from Boost.Filesystem. None of these are obscure components—they are things C++ programmers bump into in their daily code. Each one first survived in Boost for three to five years at the low end, a decade or more at the high end, got hammered on by countless projects in real environments, had its bugs mostly shaken out and its API design mostly polished, and only then was it "made permanent".

## Hands On: Tracing the Ties Between Boost and the Standard Library

Talk is cheap, so let's run some code and get a feel for it. The local environment is Arch Linux WSL, GCC 16.1.1, with Boost 1.91 installed through pacman.

Start with the most classic example: `shared_ptr`. The Boost version and the standard library version have nearly identical interfaces—and that is no coincidence, because the standard library version was copied from the Boost version:

```cpp
// File: shared_ptr_compare.cpp
// Compile: g++ -std=c++20 shared_ptr_compare.cpp -o sp

#include <iostream>
#include <memory>       // the standard library's shared_ptr
// #include <boost/shared_ptr.hpp>  // Boost's shared_ptr

int main() {
    // Standard library version
    auto p1 = std::make_shared<int>(42);
    std::cout << "use_count: " << p1.use_count() << "\n";
    std::cout << "value: " << *p1 << "\n";

    // If you uncomment the Boost include above,
    // the lines below compile as-is — the interface is exactly the same:
    // auto p2 = boost::make_shared<int>(42);
    // std::cout << "boost use_count: " << p2.use_count() << "\n";

    auto p3 = p1;  // reference count +1
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

There is nothing technically deep about this example, but that is exactly where the point lives: `use_count()`, `make_shared`, copy semantics—API designs like these were not conjured up by a committee sitting in a conference room. They are what settled out after the Boost community had used them for years and fallen into every pit along the way. Standardization here looks much more like "ratification after the fact" than "invention".

Now for a more interesting example: `boost::filesystem` versus `std::filesystem`. The Boost version showed up much earlier—the filesystem library didn't enter the standard until C++17. The program below compares how the two differ in use:

```cpp
// File: fs_compare.cpp
// Compile: g++ -std=c++20 fs_compare.cpp -o fs

#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

// If you want the Boost version, you only need to change one line:
// #include <boost/filesystem.hpp>
// namespace fs = boost::filesystem;

int main() {
    fs::path p = "/tmp/test_dir";

    // Create the directory
    if (!fs::exists(p)) {
        fs::create_directories(p);
        std::cout << "created: " << p << "\n";
    }

    // Iterate the directory
    for (const auto& entry : fs::directory_iterator(p)) {
        std::cout << "  " << entry.path().filename()
                  << " | size: " << entry.file_size() << "\n";
    }

    // Clean up
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
`std::filesystem::path`'s `operator<<` wraps the printed path in double quotes—this is behavior prescribed by the standard. If you don't want the quotes, change it to `std::cout << p.string() << "\n"`.
:::

Notice that apart from the header and the namespace, not a single line of logic needs to change. That is the value of Boost as a "testing ground": through all those years when the standard library had no filesystem support, it gave C++ programmers a unified, cross-platform story for filesystem operations, and by the time C++17 finally standardized `std::filesystem`, the API was thoroughly mature—migration cost almost nothing.

## But Boost Is More Than the Standard Library's Farm Team

A common misconception says everything in Boost has the standard library as its eventual goal, and that whatever didn't get in is a "failed product". That is completely wrong. Plenty of things in Boost were never suitable for the standard library in the first place, yet they are extremely powerful in their own domains. Boost.Spirit, for instance, is a parser-combinator framework that lets you define parsing rules in an EBNF-like grammar and write a syntax analyzer directly in C++. It is far too domain-specific for the standard library to ever take in, but if you are doing text parsing, it beats a hand-written state machine by a wide margin. Boost.Python is an interop library between C++ and Python that lets you expose C++ interfaces to Python nearly painlessly—something tied to one specific language obviously doesn't belong in the standard. Boost.Compute is an OpenCL-like GPGPU computing library, tightly bound to the hardware platform—also not standard material. And Boost.Beast is an HTTP and WebSocket library built on Boost.Asio, in heavy use among people doing C++ backends today.

So Boost's true position is this: it is at once one of the wellsprings of the standard library and an independent collection of high-quality C++ libraries. Some things "graduated" into the standard; others keep shining right where they are inside Boost. The two roles don't conflict in the least.

---

# From Boost to Beman: How the Standard Library's "Conveyor Belt" Turns

## Where the "Testing Ground" Metaphor Actually Goes Wrong

Earlier we said one part of Boost's charter is being a "testing ground". Plenty of tutorials boil that down further into "Boost is the C++ standard library's testing field", and many people read that as "whatever lands in Boost will sooner or later reach the standard". That reading has a big problem, because it completely ignores the two key questions: how something gets in, and when.

In reality, the relationship between Boost and the C++ standards committee is nowhere near as simple and direct as the words "testing ground" suggest. Boost has its own governance structure, its own review process, and its own release cadence, while C++ standardization runs the ISO track, and the two systems' goals don't fully align. Some Boost libraries are designed to be extremely general and extremely flexible, but precisely because they are so flexible, standardization demands extensive pruning and adjustment—a process that can run for years, even longer. That is why you see many Boost libraries span several C++ standard versions between proposal and final adoption. It isn't committee slowness; it's that the cost of interfacing the two systems is genuinely high.

## The Beman Project: The "Conveyor Belt" That Started Up in 2024

In 2024, David Sankel announced the Beman project<RefLink :id="4" preview="David Sankel, Beman Project, CppCon 2024" />. At first glance you might think, "another Boost replacement?"—but read further and it turns out to be nothing of the sort.

Beman's positioning is crystal clear: every library inside it has, from the day the project was chartered, had one goal—entering the C++ standard. This is not "build a nice library first and see whether standardization becomes possible later"; it is "we are producing a proposal that can go straight to WG21, with a complete reference implementation attached". You can think of it as a conveyor belt: a library completes its design, implementation, and real-world trials inside Beman, then steps onto the standardization track carrying a paper of its own.

That positioning means Beman's process is heavily simplified. Boost's review process carries real weight: you have to consider compatibility with Boost's dozens of other libraries, satisfy Boost's code style requirements, and pass the Boost community's vote. Beman, plainly, exists for standardization, so its overhead is much lower—you never have to strike a balance between "building a general-purpose library" and "building a standard proposal", because inside Beman those two are the same thing.

People used to wonder, "why not just lift things from Boost into the standard?" The reason is simple: Boost's design constraints differ from the standard's, so transplanting code as-is usually doesn't work, and reworking a library already rooted in the Boost ecosystem carries high costs, both political and technical. Beman effectively sidesteps the problem by designing from scratch on the premise of "this can enter the standard".

## What's Inside Beman Right Now

At the moment Beman counts roughly 8 active repositories<RefLink :id="4" preview="Beman Project, GitHub organization" />, one of which is the exemplar library `exemplar`, which demonstrates how a Beman library should organize its code, write its documentation, and pair itself with a proposal. `exemplar` itself does very little, but its value as a "template" is large.

Several subprojects aimed at practical directions are worth watching. Take the `optional` extension: C++23 finally gave `std::optional` `transform` and `and_then`<RefLink :id="11" preview="cppreference: std::optional, C++23 monadic operations" />, and Beman's Optional26 project builds on that base, aiming further extensions at C++26. Whenever code hits a "value that might not be there" scenario, you waver between `std::optional` and a bare pointer. Go with the bare pointer, and `nullptr` can mean either "no value" or "something went wrong"—the semantics blur together, and every `if (ptr != nullptr)` leaves you unsure whether this null is a business-level "none" or a logic-level "error". Go with `std::optional`, and the semantics are clear, but chained operations become pure pain.

A concrete example. Suppose we have a flow that looks up user info from a user ID and then extracts the email from that user info. With pre-C++23 `std::optional`, you had to write it like this:

```cpp
#include <optional>
#include <string>
#include <iostream>

struct UserInfo {
    std::string email;
};

// Simulate a lookup that may fail to find the user
std::optional<UserInfo> find_user(int user_id) {
    if (user_id == 42) {
        return UserInfo{.email = "alice@example.com"};
    }
    return std::nullopt;
}

// Extract the email from the user info; the email may be empty
std::optional<std::string> extract_email(const UserInfo& user) {
    if (user.email.empty()) {
        return std::nullopt;
    }
    return user.email;
}

int main() {
    int input_id = 42;

    // The old way: manual checks layer by layer, nested ifs — tiring just to look at
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

Look at that nesting—two levels, and it's already tiresome. In real business code, three or four levels of nesting are common, and every level needs a manual `has_value()` check, then a manual unwrap, then a hand-off to the next level. Rust's `Option::and_then` does this beautifully; C++ never had a matching mechanism.

Beman's `optional` extension exists to fill exactly this gap. With `transform` and `and_then`, the same logic reads like this:

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

    // With and_then: chained calls, so much cleaner
    auto result = find_user(input_id)
        .and_then(extract_email);

    // transform can map over the value without unwrapping it
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

Run it on GCC 14 and it compiles cleanly, with no extra dependencies. The semantics of `and_then`: if the current `optional` holds a value, that value is passed to the given function, and the function returns a new `optional`; if there is no value, an empty `optional` comes back directly and the function is never called at all. `transform` is similar, except that the given function returns a plain value rather than an `optional`, and `transform` wraps it up automatically. `std::optional` always felt like a half-finished product before; now it finally has the chaining capability that matters most. And this feature is already formally standardized in C++23—Beman's `optional` project is doing further extension and exploration on top of it.

Beyond the `optional` extension, Beman also holds subprojects such as `scopes` (scope-guard related), `tasks` (an asynchronous task abstraction), and `any_view` (a type-erased view). Just reading the names, you can tell they all target pain points you genuinely run into in day-to-day development.

## There Is One More Path: Personal Libraries Going Straight into the Standard

At this point a question may occur to you: does everything entering the standard have to pass through an organization like Boost or Beman first? The answer is no. The C++ community has a group of seriously hardcore people who wrote a library on their own, then wrote the proposal themselves (or joined forces with others), survived WG21's rings of review, and finally pushed their library into the standard. This path is harder than going through Boost or Beman, because one person has to handle the implementation, the documentation, the proposal text, and the defense all at once—but people have pulled it off.

A few of the most iconic examples: Eric Niebler's **range-v3**<RefLink :id="5" preview="Eric Niebler, range-v3, C++20 ranges reference" />, once public on GitHub, was effectively the reference implementation for C++20 ranges, and plenty of tutorials kept citing range-v3's documentation while C++20 support was still incomplete. Victor Zverovich's **{fmt}**<RefLink :id="6" preview="Victor Zverovich, {fmt}, std::format reference implementation" />, in the days before `std::format` was widely supported, was the formatting solution for practically every C++ programmer. Later `fmt` became the reference implementation for `std::format` outright, and Victor himself was one of the main driving forces behind the proposal. `std::format` is now part of the C++20 standard<RefLink :id="13" preview="P0645R10: Text Formatting for C++20" />, but in production environments people sometimes still use `fmt` directly, because in certain scenarios its compilation speed and error messages are better than the standard library implementation. Howard Hinnant's **date**<RefLink :id="7" preview="Howard Hinnant, date library, C++20 chrono extension" /> filled a huge gap in C++ date handling—before C++20 brought the time-point extensions to `<chrono>`, working with dates in C++ meant either the C-era `tm` struct (whose pitfalls could fill a whole article of their own) or pulling in a third-party library—and it ultimately drove the calendar and time-zone support in C++20's `<chrono>`.

Then come `std::span` (C++20) and `std::mdspan` (C++23)<RefLink :id="12" preview="cppreference: std::mdspan, C++23 multi-dimensional view" />. `span` is just about everywhere in modern C++ code: whenever the need is "a view over a stretch of contiguous memory", `span` beats a bare pointer plus a length by a wide margin. Once you change a function signature from `void process(uint8_t* data, size_t size)` to `void process(std::span<uint8_t> data)`, the calling code reads a notch better, and the low-level bug of "the pointer was right but the length was wrong" simply stops happening.

```cpp
#include <span>
#include <vector>
#include <cstdint>
#include <iostream>

// The old way: the caller had to keep data and len in sync — the compiler can't help you
// void process(uint8_t* data, size_t len);

// The new way: span carries its length with it, and converts implicitly from vector, array, and C arrays
void process(std::span<const uint8_t> data) {
    std::cout << "收到 " << data.size() << " 字节数据\n";
    for (size_t i = 0; i < data.size(); ++i) {
        std::cout << static_cast<int>(data[i]) << " ";
    }
    std::cout << "\n";
}

int main() {
    std::vector<uint8_t> vec = {1, 2, 3, 4, 5};

    // Pass the vector straight in — perfect
    process(vec);

    // Taking a subspan is just as easy
    process(std::span<uint8_t>(vec).subspan(1, 3));

    // C arrays work too
    uint8_t arr[] = {10, 20, 30};
    process(arr);

    return 0;
}
```

`mdspan` solves the problem of multidimensional array views. Multidimensional arrays have long been a pain point in C++—native multidimensional arrays must have compile-time sizes, while `vector<vector<T>>` suffers the performance problem of non-contiguous memory. `mdspan` provides a multidimensional, non-owning view, and its layout mapping is customizable, which means you can use it to view row-major C arrays, column-major Fortran arrays, even image buffers with custom strides. A rather large coalition stands behind this library, because the high-performance computing world's need for multidimensional array views is acute.

## Stepping Back to See the Whole Picture

With that, the whole chain is untangled. New C++ features enter the standard along roughly three paths. The first is the Boost path: a long history but a heavy process, suited to general-purpose infrastructure that needs extended polishing. The second is the Beman path, newly launched in 2024: a lightweight process designed specifically for standardization, aiming to be an efficient conveyor belt. The third is the lone-hero path, where the author writes the library and pushes the proposal personally—the hardest road, but history has no shortage of success stories. The three paths are not mutually exclusive: Beman itself counts many core Boost participants, so it reads more like a complement to the Boost idea than a competitor, and many of those individual library authors are simultaneously Boost or Beman contributors.

C++ standardization looks like a black box—where proposals come from, how they get reviewed, why some things enter the standard quickly while others wait ten years—it's all opaque. But looking back, it isn't that mysterious: it's simply a group of people, organized in different forms, continuously pushing designs proven in real-world combat into the standard. Once you understand that, reading the C++26 and C++29 proposal lists feels completely different—you can tell which ones came off the Beman conveyor belt, which ones individual library authors are pushing, and which ones are still at the early-exploration stage, instead of staring at a stack of proposal numbers in a daze.

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
