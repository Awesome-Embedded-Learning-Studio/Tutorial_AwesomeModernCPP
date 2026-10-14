---
title: "What Your Class Looks Like in Assembly: this, Constructors, and the vtable"
description: "A class is a source-level abstraction; at the assembly level there is nothing but bytes in memory and a few call instructions. This topic follows the this pointer to its register, finds where constructor and destructor calls get inserted, and takes the table of function pointers behind virtual functions apart."
sidebar_order: 5
translation:
  source: documents/vol10-open-lecture-notes/cpu-book/05-classes-in-assembly/index.md
  source_hash: 2433c545302a0da2e77e2576dfafcc842fbc198872df7c5c1cab621075b13424
  translated_at: '2026-10-14'
  engine: manual
---

# What Your Class Looks Like in Assembly: this, Constructors, and the vtable

A class is a source-level abstraction; at the assembly level there is nothing but bytes in memory and a few call instructions. This topic follows the this pointer to its register, finds where constructor and destructor calls get inserted, and takes the table of function pointers behind virtual functions apart.

<ChapterNav variant="sub">
  <ChapterLink href="01-this-pointer-and-member-offsets">What Your Class Looks Like in Assembly: The this Pointer and Member Offsets</ChapterLink>
  <ChapterLink href="02-constructor-and-destructor-calls">What Your Class Looks Like in Assembly: Where Constructor and Destructor Calls Get Inserted</ChapterLink>
  <ChapterLink href="03-virtual-functions-and-vtables">What Your Class Looks Like in Assembly: Virtual Functions and the vtable</ChapterLink>
  <ChapterLink href="04-devirtualization-under-o2">What Your Classes Look Like in Assembly: Devirtualization at -O2</ChapterLink>
</ChapterNav>
