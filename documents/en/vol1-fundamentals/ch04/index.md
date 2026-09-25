---
title: "Pointers and References"
description: "Understand pointers, references, and their relationship with memory, plus a first look at smart pointers"
translation:
  source: documents/vol1-fundamentals/ch04/index.md
  source_hash: 4a3937a3099c3fc80b0bbbda391b4e525db386ad34d3e8210cd65adad7476f76
  translated_at: '2026-09-25T10:38:37+00:00'
  engine: anthropic
  token_count: 200
---
# Pointers and References

Whenever people rank the things about C++ that cause headaches, pointers are guaranteed a spot—but once we're clear on the difference between an "address" and a "value," pointers turn out to be far less scary. This chapter starts with pointer basics, including pointer arithmetic and the close relationship between pointers and arrays, then introduces references—a safer "alias" mechanism. Finally, we get an early preview of smart pointers, setting up the expectation that "modern C++ has no need for raw pointers."

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink num="1" href="01-pointer-basics" desc="a variable that holds another variable's address">Pointer Basics</ChapterLink>
  <ChapterLink num="2" href="02-pointer-arithmetic" desc="p + 1 moves by more than just one byte">Pointer Arithmetic and Arrays</ChapterLink>
  <ChapterLink num="3" href="03-references" desc="give a variable an alias and spare yourself the pointer pain">References</ChapterLink>
  <ChapterLink num="4" href="04-smart-ptr-preview" desc="stop keeping track of delete by hand">Smart Pointer Preview</ChapterLink>
</ChapterNav>
