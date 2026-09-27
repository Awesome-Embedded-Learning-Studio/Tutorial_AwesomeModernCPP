---
title: "Template Basics"
description: "A first look at function templates, class templates, and template specialization"
translation:
  source: documents/vol1-fundamentals/ch09/index.md
  source_hash: 037fb0d49329789107e485fb7178acc24d6642e928e0e68da638a9619189e9a3
  translated_at: '2026-09-27T03:58:56+00:00'
  engine: anthropic
  token_count: 200
---
# Template Basics

Templates are C++'s core mechanism for generic programming—write the code once, and it adapts to many types. In this chapter we first look at how templates turn types into compile-time parameters so that one piece of logic serves many types, and along the way we draw a clear division of labor between templates and the virtual functions from the previous chapter; then we start with function templates and see how the compiler deduces type parameters for us automatically; from there we move on to class templates and what sets them apart from ordinary classes; finally we get a first taste of template specialization, so you know what to do when a particular type calls for special handling. This is templates at the entry level—the advanced volumes will go much deeper.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="00-why-generics">Why We Need Templates</ChapterLink>
  <ChapterLink href="01-function-templates">Function Templates</ChapterLink>
  <ChapterLink href="02-class-templates">Class Templates</ChapterLink>
  <ChapterLink href="03-specialization-basics">Template Specialization Basics</ChapterLink>
</ChapterNav>
