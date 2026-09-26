---
title: "Filesystem Library"
description: "Cross-platform path and file operations with std::filesystem"
translation:
  source: documents/vol2-modern-features/ch09-filesystem/index.md
  source_hash: c5249106feedf3013c8c32dcbd875805445f93ed5e793b18115a907835585648
  translated_at: '2026-09-25T16:13:19+00:00'
  engine: anthropic
  token_count: 360
---
# Filesystem Library

Before std::filesystem came along, the C++ standard library had virtually no ability to touch the file system — you had to fall back on POSIX APIs or platform-specific functions. The C++17 filesystem library finally closes that gap, providing cross-platform path handling, file operations, and directory traversal. In this chapter we start with path manipulation, get comfortable with creating, querying, updating, and deleting files and directories, and finish by building a practical directory traversal and search tool.

## Chapter Contents

<ChapterNav variant="sub">
  <ChapterLink href="01-filesystem-path">Path Operations: Cross-Platform Path Handling</ChapterLink>
  <ChapterLink href="02-filesystem-ops">File and Directory Operations</ChapterLink>
  <ChapterLink href="03-directory-iteration">Directory Traversal and Search</ChapterLink>
</ChapterNav>
