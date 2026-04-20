#!/usr/bin/env python3
"""
Navigation Reachability Checker for MkDocs + awesome-pages

Checks that every section index.md contains Markdown links to ALL of its
child articles. Works by parsing source Markdown files directly - no build
step required.

A section index without links to its children means users cannot navigate
to those articles from the index page.

Usage:
    python scripts/check_nav_reachability.py [--json FILE] [--quiet] [--fail-on-incomplete]
"""

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------


@dataclass
class SectionCheck:
    source_index: str
    expected_children: List[str]
    found_children: List[str]
    missing_children: List[str]
    has_all_children: bool


@dataclass
class CheckResult:
    sections: List[SectionCheck] = field(default_factory=list)
    stats: dict = field(default_factory=dict)


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SKIP_DIRS = {"images", "hooks", "stylesheets", "javascripts"}
LINK_PATTERN = re.compile(r"\[([^\]]+)\]\(([^)]+)\)")


# ---------------------------------------------------------------------------
# Core logic
# ---------------------------------------------------------------------------


def _source_rel_to_url(rel_path: str) -> str:
    """Convert source rel path to expected URL slug (stem only)."""
    p = Path(rel_path)
    if p.name == "index.md":
        return ""
    return p.stem


def build_section_map(
    documents_dir: Path,
) -> Dict[str, Tuple[str, List[str]]]:
    """Map each index.md to (rel_path, [child_stems]).

    Only considers direct .md children in the same directory.
    """
    sections: Dict[str, Tuple[str, List[str]]] = {}

    for index_file in sorted(documents_dir.rglob("index.md")):
        rel = str(index_file.relative_to(documents_dir))
        parts = Path(rel).parts
        if any(p in SKIP_DIRS for p in parts):
            continue

        parent_dir = index_file.parent
        children = []
        for child in sorted(parent_dir.glob("*.md")):
            if child.name == "index.md" or child.name.endswith(".en.md"):
                continue
            children.append(child.stem)

        if children:
            sections[rel] = children

    return sections


def extract_linked_stems(content: str, index_rel: str) -> Set[str]:
    """Extract the target stems from all internal Markdown links in content.

    Resolves relative paths to figure out which file the link points to,
    then returns the stem (filename without .md) of the target.
    """
    documents_dir = Path("documents")
    source_file = documents_dir / index_rel
    source_dir = source_file.parent
    linked_stems: Set[str] = set()

    in_code_block = False
    for line in content.split("\n"):
        stripped = line.strip()
        if stripped.startswith("```") or stripped.startswith("~~~"):
            in_code_block = not in_code_block
            continue
        if in_code_block:
            continue

        cleaned = re.sub(r"`[^`]+`", "", line)

        for match in LINK_PATTERN.finditer(cleaned):
            url = match.group(2).split("#")[0]
            if not url:
                continue
            if url.startswith(("http:", "https:", "mailto:")):
                continue
            if Path(url).suffix.lower() in {".png", ".jpg", ".jpeg", ".gif",
                                             ".svg", ".bmp", ".webp", ".ico"}:
                continue

            resolved = _resolve_link_stem(url, source_dir, documents_dir)
            if resolved:
                linked_stems.add(resolved)

    return linked_stems


def _resolve_link_stem(
    link_url: str, source_dir: Path, documents_dir: Path
) -> Optional[str]:
    """Resolve a Markdown link URL to the target file's stem."""
    try:
        target = (source_dir / link_url).resolve()
    except Exception:
        return None

    # Try direct file
    if target.is_file() and target.suffix == ".md":
        try:
            rel = str(target.relative_to(documents_dir.resolve()))
            return Path(rel).stem
        except ValueError:
            return None

    # Try with .md extension
    if not target.suffix:
        with_md = Path(str(target) + ".md")
        if with_md.is_file():
            try:
                rel = str(with_md.relative_to(documents_dir.resolve()))
                if not rel.endswith(".en.md"):
                    return Path(rel).stem
            except ValueError:
                return None

    # Try as directory with index.md
    if target.is_dir():
        index = target / "index.md"
        if index.exists():
            try:
                rel = str(index.relative_to(documents_dir.resolve()))
                return Path(rel).stem  # Returns "index" for directory links
            except ValueError:
                return None

    # Try target.md
    target_md = Path(str(target) + ".md")
    if target_md.is_file():
        try:
            rel = str(target_md.relative_to(documents_dir.resolve()))
            if not rel.endswith(".en.md"):
                return Path(rel).stem
        except ValueError:
            return None

    return None


def check_sections(
    section_map: Dict[str, Tuple[str, List[str]]],
    documents_dir: Path,
) -> List[SectionCheck]:
    results = []

    for index_rel, expected_stems in sorted(section_map.items()):
        filepath = documents_dir / index_rel
        try:
            content = filepath.read_text(encoding="utf-8")
        except Exception:
            results.append(SectionCheck(
                source_index=index_rel,
                expected_children=expected_stems,
                found_children=[],
                missing_children=expected_stems,
                has_all_children=False,
            ))
            continue

        linked = extract_linked_stems(content, index_rel)
        missing = [s for s in expected_stems if s not in linked]

        results.append(SectionCheck(
            source_index=index_rel,
            expected_children=expected_stems,
            found_children=[s for s in expected_stems if s in linked],
            missing_children=missing,
            has_all_children=len(missing) == 0,
        ))

    return results


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------


def print_console(result: CheckResult, quiet: bool = False):
    sections = result.sections
    good = [s for s in sections if s.has_all_children]
    bad = [s for s in sections if not s.has_all_children]

    print("=" * 60)
    print("Section Index Link Check")
    print("=" * 60)
    print(f"Sections: {len(sections)} | OK: {len(good)} | Incomplete: {len(bad)}")
    print()

    if bad:
        print(f"--- Incomplete ({len(bad)}) ---")
        for s in bad:
            total = len(s.expected_children)
            found = len(s.found_children)
            print(f"  {s.source_index} ({found}/{total} linked)")
            for child in s.missing_children:
                print(f"    X {child}")
        print()

    if not quiet and good:
        print(f"--- OK ({len(good)}) ---")
        for s in good:
            total = len(s.expected_children)
            print(f"  + {s.source_index} ({total}/{total})")
        print()

    if not bad:
        print("All section index pages link to their children")

    print("=" * 60)
    return len(bad)


def write_json(result: CheckResult, output_file: str):
    output = {
        "stats": result.stats,
        "incomplete": [
            {
                "source_index": s.source_index,
                "found": len(s.found_children),
                "expected": len(s.expected_children),
                "missing_children": s.missing_children,
            }
            for s in result.sections
            if not s.has_all_children
        ],
        "ok": [
            {
                "source_index": s.source_index,
                "children": len(s.expected_children),
            }
            for s in result.sections
            if s.has_all_children
        ],
    }

    with open(output_file, "w", encoding="utf-8") as f:
        json.dump(output, f, ensure_ascii=False, indent=2)

    print(f"\nJSON report written to: {output_file}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(
        description="Check section index pages link to all their children"
    )
    parser.add_argument(
        "--json", metavar="FILE", help="Also write JSON report to FILE"
    )
    parser.add_argument(
        "--quiet", action="store_true",
        help="Only show problems, no OK sections"
    )
    parser.add_argument(
        "--fail-on-incomplete", action="store_true",
        help="Exit non-zero when incomplete sections found"
    )
    args = parser.parse_args()

    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    documents_dir = project_root / "documents"

    if not documents_dir.exists():
        print(f"Error: documents directory not found: {documents_dir}")
        sys.exit(1)

    print("Scanning sections...")
    section_map = build_section_map(documents_dir)
    print(f"Found {len(section_map)} sections with child articles")

    print("Checking index pages for child links...")
    results = check_sections(section_map, documents_dir)

    good = sum(1 for s in results if s.has_all_children)
    bad = sum(1 for s in results if not s.has_all_children)
    missing_total = sum(len(s.missing_children) for s in results if not s.has_all_children)

    result = CheckResult(
        sections=results,
        stats={
            "total_sections": len(results),
            "ok": good,
            "incomplete": bad,
            "missing_child_links": missing_total,
        },
    )

    fail_count = print_console(result, quiet=args.quiet)

    if args.json:
        write_json(result, args.json)

    if args.fail_on_incomplete and fail_count > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
