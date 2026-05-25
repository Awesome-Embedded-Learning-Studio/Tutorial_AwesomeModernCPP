#!/usr/bin/env python3
"""
Markdown Link Checker for Tutorial

Checks internal markdown links in tutorial files.
Usage: python scripts/check_links.py [--fix]
"""

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple


class LinkChecker:
    # Patterns to skip
    SKIP_PATTERNS = [
        r'^https?://',  # External URLs
        r'^mailto:',
        r'^#',          # Anchor links
        r'^http://',    # External URLs
    ]

    # Image extensions to check against filesystem
    IMAGE_EXTENSIONS = {'.png', '.jpg', '.jpeg', '.gif', '.svg', '.bmp', '.webp', '.ico', '.drawio'}

    # Files to skip from checking
    SKIP_FILES = {'tags.md'}

    def __init__(self, tutorial_dir: Path, fix: bool = False):
        self.tutorial_dir = tutorial_dir
        self.fix = fix
        self.errors: List[str] = []
        self.warnings: List[str] = []
        self.all_files: Dict[str, Path] = {}
        self.link_map: Dict[str, List[Tuple[Path, str]]] = {}

    def is_external_link(self, url: str) -> bool:
        """Check if URL is external."""
        for pattern in self.SKIP_PATTERNS:
            if re.match(pattern, url):
                return True
        return False

    def build_file_index(self):
        """Build index of all markdown files."""
        for md_file in self.tutorial_dir.rglob('*.md'):
            # Store relative path from tutorial_dir
            rel_path = md_file.relative_to(self.tutorial_dir)
            self.all_files[str(rel_path)] = md_file

    def extract_links(self, content: str, filepath: Path) -> List[Tuple[int, str, str]]:
        """Extract markdown links from content.

        Returns: List of (line_number, link_text, link_url)
        """
        links = []
        # Match [text](url) and [text](<url>)
        markdown_pattern = r'\[([^\]]+)\]\(([^)]+)\)|\[([^\]]+)\]\(<([^>]+)>\)'
        # Match Vue components with literal href attributes, such as
        # <ChapterLink href="01-vector">Title</ChapterLink>.
        component_href_pattern = (
            r'<([A-Z][\w.]*)\b[^>]*\bhref=(["\'])([^"\']+)\2'
        )

        in_code_block = False
        for line_num, line in enumerate(content.split('\n'), 1):
            # Track fenced code blocks (``` or ~~~)
            stripped = line.strip()
            if stripped.startswith('```') or stripped.startswith('~~~'):
                in_code_block = not in_code_block
                continue
            if in_code_block:
                continue

            # Strip inline code spans to avoid matching C++ syntax like `[&](args)`
            cleaned = re.sub(r'`[^`]+`', '', line)

            for match in re.finditer(markdown_pattern, cleaned):
                groups = match.groups()
                if groups[1]:  # Regular link
                    link_text, link_url = groups[0], groups[1]
                else:  # Angle bracket link
                    link_text, link_url = groups[2], groups[3]

                links.append((line_num, link_text, link_url))

            for match in re.finditer(component_href_pattern, cleaned):
                component_name = match.group(1)
                link_url = match.group(3)
                links.append((line_num, component_name, link_url))

        return links

    def candidate_paths(self, link_url: str, source_file: Path) -> List[str]:
        """Return possible markdown targets for a link path."""
        # Remove fragments/anchors
        link_url = link_url.split('#')[0]
        if not link_url:
            return []

        # VitePress treats a trailing slash as an index page.
        link_url = link_url.rstrip('/')

        if not link_url:
            link_url = 'index'

        if link_url.startswith('/'):
            target = self.tutorial_dir / link_url.lstrip('/')
        else:
            target = source_file.parent / link_url

        try:
            resolved = target.resolve()
            rel = resolved.relative_to(self.tutorial_dir)
        except (ValueError, RuntimeError):
            return [link_url]

        candidates = [rel]
        if rel.suffix != '.md':
            candidates.append(rel.with_suffix('.md'))
            candidates.append(rel / 'index.md')

        return [str(candidate) for candidate in candidates]

    def check_file(self, filepath: Path):
        """Check links in a single file."""
        rel_path = filepath.relative_to(self.tutorial_dir)

        if filepath.name in self.SKIP_FILES:
            return

        try:
            content = filepath.read_text(encoding='utf-8')
        except Exception as e:
            self.errors.append(f"{rel_path}: Failed to read file: {e}")
            return

        links = self.extract_links(content, filepath)

        for line_num, link_text, link_url in links:
            if self.is_external_link(link_url):
                continue

            # Check image links against filesystem
            link_ext = Path(link_url.split('#')[0]).suffix.lower()
            if link_ext in self.IMAGE_EXTENSIONS:
                candidates = self.candidate_paths(link_url, filepath)
                if candidates and not any((self.tutorial_dir / candidate).exists() for candidate in candidates):
                    self.errors.append(
                        f"{rel_path}:{line_num} - Broken image: [{link_text}]({link_url})"
                    )
                continue

            candidates = self.candidate_paths(link_url, filepath)

            if not candidates:
                continue

            # Check if file exists
            existing = next((candidate for candidate in candidates if candidate in self.all_files), None)
            if existing is None:
                self.errors.append(
                    f"{rel_path}:{line_num} - Broken link: [{link_text}]({link_url})"
                )
            else:
                # Track valid link for reverse index
                key = str(existing)
                if key not in self.link_map:
                    self.link_map[key] = []
                self.link_map[key].append((filepath, link_text))

    def suggest_fix(self, filepath: Path, line_num: int, old_link: str, new_link: str):
        """Suggest a fix for a link."""
        print(f"  Would fix: {filepath}:{line_num} ({old_link} -> {new_link})")

    def run(self) -> bool:
        """Run the link checker."""
        print("Building file index...")
        self.build_file_index()
        print(f"Found {len(self.all_files)} markdown files")
        print()

        print("Checking links...")
        for filepath in self.all_files.values():
            self.check_file(filepath)

        self.print_summary()
        return len(self.errors) == 0

    def print_summary(self):
        """Print check summary."""
        print()
        print("=" * 60)
        print("Link Check Summary")
        print("=" * 60)

        if not self.errors and not self.warnings:
            print("✅ All links are valid!")
            return

        if self.warnings:
            print()
            print("Warnings (missing .md extension):")
            print("-" * 60)
            for warning in self.warnings[:20]:
                print(f"  ⚠ {warning}")
            if len(self.warnings) > 20:
                print(f"  ... and {len(self.warnings) - 20} more warnings")
            print()
            print("Tip: Run with --fix to automatically add .md extensions")

        if self.errors:
            print()
            print("Broken links:")
            print("-" * 60)
            for error in self.errors[:20]:
                print(f"  ✗ {error}")
            if len(self.errors) > 20:
                print(f"  ... and {len(self.errors) - 20} more errors")

        print()
        print(f"Total files checked: {len(self.all_files)}")
        print(f"Errors: {len(self.errors)}")
        print(f"Warnings: {len(self.warnings)}")

        # Print orphaned files (files not linked to anywhere)
        self.print_orphaned_files()

    def print_orphaned_files(self):
        """Print files that are not linked to anywhere."""
        linked_files = set()
        for links in self.link_map.values():
            for filepath, _ in links:
                linked_files.add(str(filepath.relative_to(self.tutorial_dir)))

        orphans = []
        for rel_path in self.all_files:
            filepath = self.all_files[rel_path]
            # Skip index files
            if filepath.name in self.SKIP_FILES:
                continue
            # Skip if this file is linked somewhere
            if str(filepath.relative_to(self.tutorial_dir)) in linked_files:
                continue
            orphans.append(rel_path)

        if orphans:
            print()
            print("Potentially orphaned files (not linked internally):")
            print("-" * 60)
            for orphan in sorted(orphans)[:10]:
                print(f"  📄 {orphan}")
            if len(orphans) > 10:
                print(f"  ... and {len(orphans) - 10} more files")


def main():
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    tutorial_dir = project_root / 'documents'

    if not tutorial_dir.exists():
        print(f"Error: Tutorial directory not found: {tutorial_dir}")
        sys.exit(1)

    parser = argparse.ArgumentParser(description='Check markdown links in tutorial')
    parser.add_argument('--fix', action='store_true', help='Auto-fix simple issues')
    args = parser.parse_args()

    checker = LinkChecker(tutorial_dir, fix=args.fix)
    success = checker.run()

    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
