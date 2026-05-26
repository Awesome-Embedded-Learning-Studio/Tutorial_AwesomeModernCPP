---
title: Filesystem Library
description: Implementing cross-platform path and file operations with `std::filesystem`
translation:
  source: documents/vol2-modern-features/ch09-filesystem/index.md
  source_hash: c5249106feedf3013c8c32dcbd875805445f93ed5e793b18115a907835585648
  translated_at: '2026-05-26T11:33:57.801258+00:00'
  engine: anthropic
  token_count: 125
---
# Filesystem Library

Before `std::filesystem` arrived, the C++ standard library had almost no capability for file system operations—you had to rely on POSIX APIs or platform-specific functions. The C++17 `filesystem` library finally filled this gap, providing cross-platform path manipulation, file operations, and directory traversal capabilities. In this chapter, we start with path manipulation, master creating, reading, updating, and deleting files and directories, and finally build a practical directory traversal and search tool.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-filesystem-path">path Operations: Cross-Platform Path Handling</ChapterLink>
  <ChapterLink href="02-filesystem-ops">File and Directory Operations</ChapterLink>
  <ChapterLink href="03-directory-iteration">Directory Traversal and Search</ChapterLink>
</ChapterNav>
