---
title: Design Patterns
description: 'Modern C++ implementations of GoF design patterns: an evolutionary guide
  to the problems solved by each pattern. This series of 21 articles covers creational,
  structural, and behavioral patterns, complete with compilable projects.'
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/index.md
  source_hash: 9b40049f117ab804b3ee4f7f7d263db4dcd2200594b9562741803693ff352001
  translated_at: '2026-06-24T01:04:59.874173+00:00'
  engine: anthropic
  token_count: 584
---
# Design Patterns

Design patterns represent classic solutions to recurring design problems, distilled from past experience. This volume covers the 21 core patterns from the Gang of Four (GoF), re-implementing them using **modern C++**.

Unlike traditional tutorials that follow a "Definition + UML + Example" format, our approach is to **start from the most intuitive, primitive code and step-by-step derive each pattern**. We clarify exactly what problem each pattern solves, why each evolutionary step is insufficient, and how—with C++17/20 features like `std::variant`, concepts, CRTP, and templates—we can achieve safer, zero-overhead versions of these classic patterns.

Key features throughout this volume: every pattern includes "let's verify this" real terminal output (not just theory on paper); common pitfalls are highlighted in `::: warning` blocks; and wherever modern C++ can replace old idioms, we place "Classic vs. Modern" side-by-side for comparison.

The accompanying compilable project is located in the repository at [code/volumn_codes/vol4/design-patterns/](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns). Each pattern is a separate CMake sub-project. Run `cmake -S . -B build && cmake --build build` to build and run.

## Creational

<ChapterNav variant="sub">
  <ChapterLink href="01-singleton">Singleton Pattern: From Comment Constraints to Meyer's Singleton</ChapterLink>
  <ChapterLink href="02-builder">Builder Pattern</ChapterLink>
  <ChapterLink href="03-factory-method-abstract-factory">Factory Method & Abstract Factory</ChapterLink>
  <ChapterLink href="04-prototype">Prototype Pattern</ChapterLink>
</ChapterNav>

## Structural

<ChapterNav variant="sub">
  <ChapterLink href="05-adapter">Adapter Pattern</ChapterLink>
  <ChapterLink href="06-bridge">Bridge Pattern (pImpl)</ChapterLink>
  <ChapterLink href="07-decorator">Decorator Pattern</ChapterLink>
  <ChapterLink href="08-composite">Composite Pattern</ChapterLink>
  <ChapterLink href="09-facade">Facade Pattern</ChapterLink>
  <ChapterLink href="10-flyweight">Flyweight Pattern</ChapterLink>
  <ChapterLink href="11-proxy">Proxy Pattern</ChapterLink>
</ChapterNav>

## Behavioral

<ChapterNav variant="sub">
  <ChapterLink href="12-strategy">Strategy Pattern</ChapterLink>
  <ChapterLink href="13-command">Command Pattern</ChapterLink>
  <ChapterLink href="14-state">State Machine Pattern</ChapterLink>
  <ChapterLink href="15-memento">Memento Pattern</ChapterLink>
  <ChapterLink href="16-visitor">Visitor Pattern (variant + visit)</ChapterLink>
  <ChapterLink href="17-observer">Observer Pattern</ChapterLink>
  <ChapterLink href="18-iterator">Iterator Pattern</ChapterLink>
  <ChapterLink href="19-chain-of-responsibility">Chain of Responsibility Pattern</ChapterLink>
  <ChapterLink href="20-interpreter">Interpreter Pattern</ChapterLink>
  <ChapterLink href="21-mediator">Mediator Pattern</ChapterLink>
</ChapterNav>

## Advanced: Generic and Template Patterns (Planned)

Beyond the classic GoF patterns, C++ possesses its own set of "template-level" design techniques. These serve as an extension of this volume and will be covered in the future:

- Policy-Based Design
- Type Erasure (underlying mechanism of `std::function`)
- CRTP (Static Polymorphism)
- Mixin and Compositional Design
- Tag Dispatching and `if constexpr` dispatch
- NVI (Non-Virtual Interface)
- Templates and DSL Construction
