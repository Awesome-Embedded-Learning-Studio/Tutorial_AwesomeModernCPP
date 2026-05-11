---
title: constexpr and compile-time computation
description: Make computation happen at compile time to achieve true zero-cost abstraction.
---
# constexpr and Compile-Time Computation

If the result of a computation can be determined at compile time, why wait until runtime? `constexpr` makes it possible to evaluate functions and variables at compile time, while `consteval` and `constinit` provide further hard guarantees for compile-time evaluation. In this chapter, we start with the basics of `constexpr`, understand the design constraints of literal types and `constexpr` constructors, master the new tools in C++20, and finally apply compile-time lookup tables, string processing, and design patterns in practice.

## Chapter Contents

- [constexpr Basics: The Art of Compile-Time Evaluation](01-constexpr-basics)
- [constexpr Constructors and Literal Types](02-constexpr-ctor)
- [consteval and constinit: New Tools for Compile-Time Guarantees](03-consteval-constinit)
- [Compile-Time Computation in Practice: From Lookup Tables to Compile-Time Strings](04-compile-time-practice)
