# Compiler Explorer examples

This directory contains small, single-file snippets used by the VitePress
`OnlineCompilerDemo` component.

These files should be committed with the documentation changes that reference
them. The component reads `arm-source-path` from the published repository, and
keeps a small built-in fallback only so local previews keep working before the
new files reach the default branch.

Guidelines:

- Keep each file self-contained.
- Prefer freestanding-friendly headers such as `<cstdint>` and `<cstddef>`.
- Avoid `iostream`, hosted-only containers, exceptions, dynamic allocation, and
  board-specific HAL headers in ARM snippets.
- Use these snippets for assembly inspection, not as full tutorial programs.
