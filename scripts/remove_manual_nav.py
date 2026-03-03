#!/usr/bin/env python3
"""
Remove manual navigation blocks from markdown articles.

MkDocs has built-in prev/next navigation (navigation.footer: true),
so manual navigation links are redundant.
"""

import re
import sys
from pathlib import Path


def remove_navigation_block(content: str) -> str:
    """Remove the manual navigation block from markdown content."""
    # Pattern to match the navigation block - use a broader approach
    # Match everything from "---\n\n## 导航" to the end of the section
    pattern = r'\n---\n\n## 导航\n\n(?:\[← 上一篇[^\]]*\]\([<]?[^)]*\.md[>]?\)[\s\n]*\|[\s\n]*\[.*?→\]\([<]?[^)]*\.md[>]?\)|\[上一篇.*?\].*?\n.*?\n\[下一篇.*?\])'

    content = re.sub(pattern, '', content, flags=re.DOTALL)

    # Also catch simpler pattern - just remove the whole navigation section
    # Match from "## 导航" to end of file or next heading
    content = re.sub(
        r'\n---\n\n## 导航\n\n\[.*?\]\(.*?\.md\)(?:[\s\n]*\|[\s\n]*\[.*?\]\(.*?\.md\))?',
        '',
        content,
        flags=re.DOTALL
    )

    # Clean up any trailing whitespace left after removal
    content = re.sub(r'\n\n\n+', '\n\n', content)

    return content


def process_file(filepath: Path) -> bool:
    """Process a single file and return True if modified."""
    content = filepath.read_text(encoding='utf-8')
    new_content = remove_navigation_block(content)

    if new_content != content:
        filepath.write_text(new_content, encoding='utf-8')
        return True
    return False


def main():
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    tutorial_dir = project_root / 'tutorial'

    # Find all markdown files
    md_files = list(tutorial_dir.rglob('*.md'))

    # Skip index.md and tags.md
    md_files = [
        f for f in md_files
        if f.name not in {'index.md', 'tags.md'}
    ]

    print(f"Processing {len(md_files)} markdown files...")

    modified = 0
    for filepath in md_files:
        if process_file(filepath):
            modified += 1
            print(f"  Modified: {filepath.relative_to(project_root)}")

    print()
    print(f"Done! Modified {modified} files.")
    print()
    print("Manual navigation blocks have been removed.")
    print("MkDocs built-in navigation (footer) will be used instead.")


if __name__ == '__main__':
    main()
