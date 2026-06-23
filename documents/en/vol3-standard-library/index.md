---
title: 'Volume Three: Deep Dive into the Standard Library'
description: Deep dive into STL containers, iterators, and algorithms
platform: host
tags:
- cpp-modern
- host
- intermediate
translation:
  source: documents/vol3-standard-library/index.md
  source_hash: 66cf6bb0c9f71a00fabe2b857a16f167459c10b820e4b7b274551a4350eff572
  translated_at: '2026-06-16T04:01:20.283851+00:00'
  engine: anthropic
  token_count: 373
---
# Volume III: Deep Dive into the Standard Library

## Overview

This volume provides an in-depth look at the C++ Standard Library, focusing on containers, iterators, algorithms, and general utilities. We explore the underlying implementation of each component to understand "why it is designed this way + how to use it correctly," rather than simply listing APIs.

## Containers and Data Structures

<ChapterNav variant="sub">
  <ChapterLink href="containers/01-container-selection-guide">Container Selection Guide</ChapterLink>
  <ChapterLink href="containers/02-array">array: Fixed-Size Arrays</ChapterLink>
  <ChapterLink href="containers/03-vector-deep-dive">vector Deep Dive</ChapterLink>
  <ChapterLink href="containers/04-string-memory-deep-dive">string Deep Dive</ChapterLink>
  <ChapterLink href="containers/05-deque-list-forward-list">deque, list, and forward_list</ChapterLink>
  <ChapterLink href="containers/06-map-set-deep-dive">map and set Deep Dive</ChapterLink>
  <ChapterLink href="containers/07-unordered-map-set-deep-dive">unordered_map and set Deep Dive</ChapterLink>
  <ChapterLink href="containers/08-span">span: Non-owning View</ChapterLink>
  <ChapterLink href="containers/09-container-adapters">Container Adapters: stack/queue/priority_queue</ChapterLink>
  <ChapterLink href="containers/10-new-containers-cpp23-26">New Standard Containers: flat_map/inplace_vector/mdspan</ChapterLink>
  <ChapterLink href="containers/11-initializer-lists">Initializer Lists</ChapterLink>
  <ChapterLink href="containers/12-object-size-and-trivial-types">Object Size and Trivial Types</ChapterLink>
  <ChapterLink href="containers/13-custom-allocators">Custom Allocators</ChapterLink>
</ChapterNav>

## Iterators and Algorithms

<ChapterNav variant="sub">
  <ChapterLink href="iterators-algorithms/40-iterator-basics-and-categories">Iterator Basics and Categories</ChapterLink>
</ChapterNav>

## Strings and Text

<ChapterNav variant="sub">
  <ChapterLink href="strings/30-char8-t-utf8">char8_t and UTF-8</ChapterLink>
</ChapterNav>
