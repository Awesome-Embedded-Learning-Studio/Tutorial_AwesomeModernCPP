---
translation:
  source: documents/vol9-open-source-project-learn/chrome/04_no_destructor/index.md
  source_hash: 7f437c59cf56f3555a5b77aaba17298a555d87efe21b50eebad96de64e276f75
  translated_at: '2026-09-26T03:30:49+00:00'
  engine: anthropic
  token_count: 850
---
# NoDestructor: static lifetime management lessons from Chromium

This directory takes apart Chromium's `base::NoDestructor<T>` and works through the lifetime management of global and static objects: why Chromium bans global constructors and destructors, how placement new manages lifetime by hand, the thread safety of magic statics, and the "intentional leak" tradeoff. It is the sister series to [OnceCallback](../01_once_callback/), [WeakPtr](../02_weak_ptr/), and [flat_map](../03_flat_map/), rounding out the static-lifetime piece of vol9/chrome.

NoDestructor is a lightweight component (a thin header-only wrapper class), so this series is leaner than the previous three.

## Complete tutorial (full/)

- Prerequisites: [static storage duration, initialization, and destruction](./full/pre-00-static-storage-and-init.md), [placement new and aligned storage](./full/pre-01-placement-new-and-aligned-storage.md)
- Hands-on practice: [motivation and API](./full/04-1-no-destructor-motivation-and-api.md), [core implementation](./full/04-2-no-destructor-core-impl.md), [usage boundaries](./full/04-3-no-destructor-when-to-use.md), [LSan and leaks](./full/04-4-no-destructor-lsan-and-leak.md)

## Advanced design guide (hands_on/)

Aimed at readers with template and lifetime experience: [motivation, API, and implementation](./hands_on/01-no-destructor-design-and-impl.md), [usage boundaries and testing](./hands_on/02-no-destructor-usage-and-testing.md).
