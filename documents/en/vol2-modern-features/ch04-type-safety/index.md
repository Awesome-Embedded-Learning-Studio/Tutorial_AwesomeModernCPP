---
title: Type safety
description: Building a safer type system with strong types, variant, optional, and
  any
---
# Type Safety

C-era enums, unions, and raw pointers leave behind too many type safety pitfalls—implicit conversions, undefined behavior, null pointer dereferences... Modern C++ provides a suite of tools to plug these holes. In this chapter, we explore how `enum class` puts an end to the nightmare of implicit enum conversions, how strong typedefs prevent parameter mix-ups, how `variant` safely replaces unions, how `optional` elegantly expresses "maybe no value," and how `any` achieves type erasure when needed.

## Chapter Contents

- [enum class and Strongly-Typed Enums](01-enum-class)
- [Strong typedefs: Type Safety Against Mix-ups](02-strong-types)
- [std::variant: Type-Safe Unions](03-variant)
- [std::optional: Elegantly Expressing "Maybe No Value"](04-optional)
- [std::any and Type Erasure](05-any)
