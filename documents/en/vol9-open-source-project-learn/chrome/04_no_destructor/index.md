# NoDestructor: static lifetime management, learned from Chromium

This directory takes apart Chromium's `base::NoDestructor<T>` and works through the lifetime management of global and static objects: why Chromium bans global constructors and destructors, how placement new manages lifetime by hand, the thread safety of magic statics, and the "intentional leak" tradeoff. It sits alongside [OnceCallback](../01_once_callback/), [WeakPtr](../02_weak_ptr/), and [flat_map](../03_flat_map/), rounding out the static-lifetime piece of vol9/chrome.

NoDestructor is a lightweight component (a thin header-only wrapper), so this series is shorter than the other three.

## Full tutorial (full/)

- Prerequisites: [static storage duration, init, and destruction](./full/pre-00-static-storage-and-init.md), [placement new and aligned storage](./full/pre-01-placement-new-and-aligned-storage.md)
- Hands-on: [motivation and API](./full/04-1-no-destructor-motivation-and-api.md), [core implementation](./full/04-2-no-destructor-core-impl.md), [when to use](./full/04-3-no-destructor-when-to-use.md), [LSan and leaks](./full/04-4-no-destructor-lsan-and-leak.md)

## Hands-on design guide (hands_on/)

For readers comfortable with templates and lifetime: [motivation, interface, and implementation](./hands_on/01-no-destructor-design-and-impl.md), [usage boundaries and testing](./hands_on/02-no-destructor-usage-and-testing.md).
