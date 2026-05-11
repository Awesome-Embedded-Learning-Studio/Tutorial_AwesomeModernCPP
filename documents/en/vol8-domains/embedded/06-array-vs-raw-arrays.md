# Embedded C++ Tutorial — `std::array` vs C Arrays, Do You Know the Difference?

When writing embedded code, you will likely hesitate between two approaches: `int buf[16];` and `std::array<int, 16> buf;`. If you are like me—valuing both performance and elegance—you will want to know: which one is more "embedded-friendly"?

------

## Why `std::array` Looks Like "a C Array Wearing a Coat" — But Is Actually Smarter

On the surface, `std::array<T, N>` is simply an aggregate type containing a `T elems[N]` underneath: elements are contiguous in memory, and there is no mysterious overhead in the layout. Therefore, in many scenarios, `std::array` is equivalent to a raw array in terms of performance and memory footprint. In other words, you do not pay any extra runtime cost by switching to `std::array`.

But `std::array` wraps the array in a type: it has value semantics (it can be copied and assigned), provides `.size()`, offers `.data()`, includes `begin()`/`end()`, integrates seamlessly with STL algorithms, supports `constexpr` (with modern compilers), and can be better deduced as a template parameter. Most importantly, it makes the information that "length is part of the type" explicit, making it much harder to lose size information when calling interfaces.

In other words: `std::array` is a "safer, more modern" array.

------

## The Honest Simplicity and Fatal Naivety of Raw C Arrays

The advantage of raw arrays is "zero abstraction"—you have complete control over memory. This is crucial in startup code, driver layers, and buffers located in specific address spaces (such as those mapped to peripheral register addresses). Raw arrays do not create headaches with ABI, linkers, or alignment—as long as you know what you are doing, they are highly reliable.

However, raw arrays also bring a host of common pitfalls: they decay into pointers in function parameters (so `sizeof` yields a pointer size inside a function), cannot be directly copied or assigned (`b = a;` will fail to compile), and offer no bounds or size protection. In embedded code, these "missing conveniences" force you to frequently write `memcpy`, constantly double-check whether `N` is correct, and make rookie mistakes like "forgetting to pass the length" during code reviews.

A real-world scenario: you pass a raw array to a C API for DMA, but forget to tell the caller the length. As a result, DMA writes out of bounds and overwrites your most precious variable. Raw arrays do not warn you about these low-probability, high-cost errors.

------

## Advantages of `std::array`: Safer, More Readable, and More Modern C++ Friendly

The everyday advantages of `std::array` can be summarized as: clear semantics, friendly interfaces, and direct compatibility with algorithms. For example, `std::sort(a.begin(), a.end())` or `std::span(a)` are readily available benefits. `std::array` can be `=`, copied, or even safely returned as a function return value (without decaying), which makes code in many mid-level logic layers more concise and less prone to memory manipulation bugs.

In an embedded context, this means test code, unit test stubs, and buffer wrappers will be much cleaner: you can write functions that return `std::array` instead of a messy pile of `memcpy`. Furthermore, when the compiler supports `constexpr`, `std::array` can construct constant tables at compile time, resulting in code that is both efficient and safe.

------

## So When Should We Stick with Raw C Arrays?

`std::array` is great, but it is not invincible. In the following scenarios, raw arrays remain the more appropriate choice:

1. **Initialization phase or early boot code (startup / crt0)**: Before `main()`, C++ global construction rules and runtime support can be problematic. Raw arrays are more straightforward and reliable in such code, especially when you need to absolutely guarantee that no constructors or runtime code intervene.
2. **Placed in specific linker sections / at fixed addresses**: Interrupt vector tables, device-mapped buffers, and bootloader tables often require precisely declaring object locations and byte order in the linker script. Raw arrays map more directly to the expected memory layout, reducing unnecessary abstraction.
3. **Strict ABI or interoperability with external C APIs where you need raw pointers**: Although `std::array` has `.data()`, in scenarios that are highly particular about binary compatibility, using raw arrays is more intuitive during auditing (especially in legacy codebases).
4. **Extreme resource constraints where you must avoid the compiler generating any extra metadata**: Such situations are rare, but they do exist in some ultra-embedded or lowest-level kernel code.

------

## The Bottom Line

Raw arrays are simple, reliable tools suited for the layer closest to the hardware; `std::array` is a more modern, safer container that better aligns with C++ philosophy, suited for business logic, algorithm layers, and the vast majority of embedded application code. Treat them as two different knives in your toolbox: use the survival knife (raw arrays) to fix chip pins, and use the precision knife (`std::array`) for protocol parsing and buffer logic.

One final piece of advice: when you can express the array size as a template parameter of `std::array<T, N>`, go ahead and use `std::array`; when you must precisely control every single byte in a linker script or the earliest boot code, fall back to raw arrays without hesitation. Embedded development is not about "staying pure"—it is about using the right tool for the actual need. `std::array` will often give you less code and fewer bugs, but occasionally you still need to roll up your sleeves and reach into raw memory to fix the low-level stuff.

------

## Code Examples
