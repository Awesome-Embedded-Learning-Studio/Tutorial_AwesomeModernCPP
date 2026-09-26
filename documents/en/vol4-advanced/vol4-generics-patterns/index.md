---
title: "Design Patterns"
description: "Modern C++ implementations of the GoF design patterns: an evolutionary walkthrough of the problem each pattern solves. 21 articles covering the three big categories — creational, structural, and behavioral — with a compilable companion project"
translation:
  source: documents/vol4-advanced/vol4-generics-patterns/index.md
  source_hash: 5a31061b372a455d085e485e6975b4789d761a5abf5f83ca0b0852c36408a7f0
  translated_at: '2026-09-26T05:29:18+00:00'
  engine: anthropic
  token_count: 850
---

# Design Patterns

Design patterns are the classic solutions to recurring design problems, distilled and handed down by the people who came before us. This volume covers the 21 core GoF patterns and re-implements every one of them with **modern C++**.

Instead of the traditional "definition + UML + example" lecture format, our approach is to **start from the most intuitive, most primitive code and let each pattern emerge step by step** — pinning down what problem it actually solves, why each evolutionary stage is still not enough, and what a safer, closer-to-zero-overhead version of the same pattern looks like once C++17/20 brings `std::variant`, concepts, CRTP, and templates to the table.

The companion compilable project lives in the repository at [code/volumn_codes/vol4/design-patterns/](https://github.com/Awesome-Embedded-Learning-Studio/Tutorial_AwesomeModernCPP/tree/main/code/volumn_codes/vol4/design-patterns); each pattern is an independent CMake sub-project, and `cmake -S . -B build && cmake --build build` is all it takes to run one.

## Creational

<ChapterNav variant="sub">
  <ChapterLink href="01-singleton">Singleton Pattern: From Comment-Only Constraints to Meyer's Singleton</ChapterLink>
  <ChapterLink href="02-builder">Builder Pattern</ChapterLink>
  <ChapterLink href="03-factory-method-abstract-factory">Factory Method and Abstract Factory</ChapterLink>
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

Beyond the classic GoF patterns, C++ has its own set of "template-level" design techniques. They are the extension direction of this volume, to be filled in later:

- Policy-Based Design
- Type erasure (the machinery underneath `std::function`)
- CRTP (static polymorphism)
- Mixins and composition-based design
- Tag dispatching and `if constexpr` dispatch
- NVI (Non-Virtual Interface)
- Templates and DSL construction
