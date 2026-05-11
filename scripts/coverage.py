#!/usr/bin/env python3
"""
Translation coverage report for TAMCPP VitePress project.

Scans documents/ and documents/en/ to calculate translation coverage
per category and overall.

Usage:
    python3 scripts/coverage.py              # Print report to stdout
    python3 scripts/coverage.py --json       # Output JSON for shields.io badge
    python3 scripts/coverage.py --update     # Update README.md badge section
"""

import json
import re
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent
DOCS_DIR = PROJECT_ROOT / 'documents'
I18N_DIR = PROJECT_ROOT / 'documents' / 'en'

# Categories to track (subdirectory under documents/)
CATEGORIES = [
    'vol1-fundamentals',
    'vol2-modern-features',
    'vol3-standard-library',
    'vol4-advanced',
    'vol5-concurrency',
    'vol6-performance',
    'vol7-engineering',
    'vol8-domains',
    'vol9-open-source-project-learn',
    'compilation',
    'cpp-reference',
    'projects',
    'appendix',
]

EXCLUDE_DIRS = {'images', 'generated', 'hooks', 'stylesheets', 'javascripts', 'en'}

# Friendly labels for display
LABELS = {
    'vol1-fundamentals': 'Vol.1 Fundamentals',
    'vol2-modern-features': 'Vol.2 Modern Features',
    'vol3-standard-library': 'Vol.3 Standard Library',
    'vol4-advanced': 'Vol.4 Advanced',
    'vol5-concurrency': 'Vol.5 Concurrency',
    'vol6-performance': 'Vol.6 Performance',
    'vol7-engineering': 'Vol.7 Engineering',
    'vol8-domains': 'Vol.8 Domains',
    'vol9-open-source-project-learn': 'Vol.9 Open Source',
    'compilation': 'Compilation',
    'cpp-reference': 'C++ Reference',
    'projects': 'Projects',
    'appendix': 'Appendix',
    '_root': 'Root Pages',
}

# Top-level docs (not in a category)
TOP_LEVEL_FILES = ['index.md', 'tags.md']


def find_md_files(directory: Path) -> list[Path]:
    """Find all .md files excluding non-content dirs."""
    files = []
    for f in directory.rglob('*.md'):
        if any(part in EXCLUDE_DIRS for part in f.parts):
            continue
        files.append(f)
    return sorted(files)


def compute_coverage():
    """Compute translation coverage stats."""
    results = {}
    total_source = 0
    total_translated = 0

    # Per-category stats
    for cat in CATEGORIES:
        source_dir = DOCS_DIR / cat
        if not source_dir.exists():
            results[cat] = {'source': 0, 'translated': 0, 'percentage': 0}
            continue

        source_files = find_md_files(source_dir)
        translated_count = 0

        for sf in source_files:
            rel = str(sf.relative_to(DOCS_DIR))
            translated_path = I18N_DIR / rel
            if translated_path.exists():
                translated_count += 1

        count = len(source_files)
        pct = round(translated_count / count * 100) if count > 0 else 0
        results[cat] = {
            'source': count,
            'translated': translated_count,
            'percentage': pct,
        }
        total_source += count
        total_translated += translated_count

    # Top-level files
    tl_translated = 0
    tl_count = 0
    for fname in TOP_LEVEL_FILES:
        source = DOCS_DIR / fname
        if source.exists():
            tl_count += 1
            if (I18N_DIR / fname).exists():
                tl_translated += 1
    results['_root'] = {
        'source': tl_count,
        'translated': tl_translated,
        'percentage': round(tl_translated / tl_count * 100) if tl_count > 0 else 0,
    }
    total_source += tl_count
    total_translated += tl_translated

    overall_pct = round(total_translated / total_source * 100) if total_source > 0 else 0

    return {
        'overall': {
            'source': total_source,
            'translated': total_translated,
            'percentage': overall_pct,
        },
        'categories': results,
    }


def print_report(data):
    """Print a human-readable coverage report."""
    print("Translation Coverage Report")
    print('=' * 50)
    print(f"Overall: {data['overall']['translated']}/{data['overall']['source']} "
          f"({data['overall']['percentage']}%)")
    print('-' * 50)

    for cat, stats in data['categories'].items():
        label = LABELS.get(cat, cat)
        pct = stats['percentage']
        bar = '█' * (pct // 5) + '░' * (20 - pct // 5)
        print(f"  {label:<25} {bar} {stats['translated']:>3}/{stats['source']:<3} ({pct}%)")

    print('=' * 50)


def output_json(data):
    """Output JSON for shields.io endpoint badge."""
    badge = {
        'schemaVersion': 1,
        'label': 'en coverage',
        'message': f"{data['overall']['percentage']}%",
        'color': 'green' if data['overall']['percentage'] >= 80 else
                 'yellow' if data['overall']['percentage'] >= 50 else 'red',
    }
    print(json.dumps(badge, indent=2))


def update_readme(data):
    """Update README.md with coverage stats."""
    readme_path = PROJECT_ROOT / 'README.md'
    if not readme_path.exists():
        print("README.md not found", file=sys.stderr)
        return

    content = readme_path.read_text(encoding='utf-8')
    pct = data['overall']['percentage']
    translated = data['overall']['translated']
    total = data['overall']['source']

    color = 'green' if pct >= 80 else 'yellow' if pct >= 50 else 'red'
    badge_url = f"https://img.shields.io/badge/en_coverage-{pct}%25-{color}.svg"
    coverage_line = f"![English Coverage]({badge_url}) {translated}/{total} docs translated"

    marker_start = '<!-- COVERAGE_START -->'
    marker_end = '<!-- COVERAGE_END -->'

    if marker_start in content:
        pattern = f"{marker_start}.*?{marker_end}"
        replacement = f"{marker_start}\n{coverage_line}\n{marker_end}"
        content = re.sub(pattern, replacement, content, flags=re.DOTALL)
    else:
        # Insert after the badges block (after the --- that follows badges)
        lines = content.split('\n')
        new_lines = []
        inserted = False
        for i, line in enumerate(lines):
            new_lines.append(line)
            if not inserted and line.strip() == '---':
                # Check if previous lines have badges
                above = '\n'.join(lines[max(0, i - 5):i])
                if 'shields.io' in above:
                    new_lines.append('')
                    new_lines.append(marker_start)
                    new_lines.append(coverage_line)
                    new_lines.append(marker_end)
                    inserted = True
        content = '\n'.join(new_lines)

    readme_path.write_text(content, encoding='utf-8')
    print(f"Updated README.md: {translated}/{total} ({pct}%)")


def main():
    if len(sys.argv) > 1:
        arg = sys.argv[1]
        if arg == '--json':
            data = compute_coverage()
            output_json(data)
        elif arg == '--update':
            data = compute_coverage()
            print_report(data)
            update_readme(data)
        else:
            print(f"Unknown argument: {arg}", file=sys.stderr)
            sys.exit(1)
    else:
        data = compute_coverage()
        print_report(data)


if __name__ == '__main__':
    main()
