---
title: Smart pointers and RAII
description: Implement automatic resource management using RAII and smart pointers
---
# Smart Pointers and RAII

Manually managing resources (`new`/`delete`, `fopen`/`fclose`, `lock`/`unlock`) is a nightmare for C++ programmers. The RAII (Resource Acquisition Is Initialization) principle tells us to bind resource acquisition to object construction, leave release to destructors, and let scope manage the lifetime for us. In this chapter, we first dive deep into RAII, then master the design philosophy and correct usage of `unique_ptr`, `shared_ptr`, and `weak_ptr` one by one, and finally see how custom deleters and scope guards handle more complex resource scenarios.

## Chapter Contents

- [Deep Dive into RAII: The Cornerstone of Resource Management](01-raii-deep-dive)
- [Understanding unique_ptr: Zero-Overhead Smart Pointer with Exclusive Ownership](02-unique-ptr)
- [Understanding shared_ptr: Shared Ownership and Reference Counting](03-shared-ptr)
- [weak_ptr and Circular References](04-weak-ptr)
- [Custom Deleters and Intrusive Reference Counting](05-custom-deleter)
- [scope_guard and defer: Generic Scope Guards](06-scope-guard)
