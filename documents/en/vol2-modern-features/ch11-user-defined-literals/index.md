---
title: User-defined literals
description: Implementing a type-safe literal and unit system using operator
---
# User-Defined Literals

User-defined literals (UDLs) let you assign custom semantics to literal values — `100_m` means 100 meters, `1.5_rad` means radians, and `"hello"_sv` means `string_view`. Combined with `constexpr` and strong types, UDLs enable compile-time unit checking and type conversion, making them a powerful tool for implementing type-safe physical unit libraries.

## Chapter Contents

- [User-Defined Literal Basics](01-udl-basics)
- [UDL in Practice: Type-Safe Unit Systems](02-udl-practice)
