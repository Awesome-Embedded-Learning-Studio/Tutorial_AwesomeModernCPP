#!/usr/bin/env python3
"""
Document Generator — auto-generate TOC, cross-references, code index, and stats.

Usage:
    python3 scripts/generate_docs.py                # Generate all
    python3 scripts/generate_docs.py --update       # Write to documents/generated/
    python3 scripts/generate_docs.py --ci           # Diff mode (exit 1 if stale)
    python3 scripts/generate_docs.py --format json  # JSON output
"""

import argparse
import json
import os
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from datetime import date
from pathlib import Path
from typing import Dict, List, Optional, Tuple

SKIP_FILENAMES = {'index.md', 'tags.md', 'README.md'}
SKIP_DIR_PARTS = {'images', 'generated', 'hooks', 'stylesheets', 'javascripts'}

VOLUME_NAMES = {
    'vol1-fundamentals': 'C++ 基础入门',
    'vol2-modern-features': '现代 C++ 特性',
    'vol3-standard-library': '标准库实战',
    'vol4-advanced': '高级主题',
    'vol5-concurrency': '并发编程',
    'vol6-performance': '性能优化',
    'vol7-engineering': '工程实践',
    'vol8-domains': '领域应用',
    'compilation': '编译与链接',
    'cpp-reference': 'C++ 速查手册',
    'appendix': '附录',
    'projects': '综合项目',
}


@dataclass
class Article:
    path: Path
    rel_path: str
    frontmatter: Dict = field(default_factory=dict)
    title: str = ''
    chapter: int = 0
    order: int = 0
    tags: List[str] = field(default_factory=list)
    difficulty: str = ''
    platform: str = ''
    reading_time_minutes: int = 0
    cpp_standard: List = field(default_factory=list)
    volume: str = ''
    links: List[Tuple[str, str]] = field(default_factory=list)  # (text, url)
    code_refs: List[str] = field(default_factory=list)


def parse_frontmatter(content: str) -> Tuple[Dict, bool]:
    match = re.match(r'^---\s*\n(.*?)\n---', content, re.DOTALL)
    if not match:
        return {}, False
    try:
        import yaml
        fm = yaml.safe_load(match.group(1))
        return fm if fm else {}, True
    except Exception:
        return {}, True


def is_article(filepath: Path) -> bool:
    if filepath.name in SKIP_FILENAMES:
        return False
    if any(part in SKIP_DIR_PARTS for part in filepath.parts):
        return False
    # Skip translated .en.md files
    if filepath.name.endswith('.en.md'):
        return False
    return True


def extract_links(content: str) -> List[Tuple[str, str]]:
    """Extract markdown links (text, url), skipping code blocks."""
    links = []
    in_code = False
    pattern = r'\[([^\]]+)\]\(([^)]+)\)'
    for line in content.split('\n'):
        stripped = line.strip()
        if stripped.startswith('```') or stripped.startswith('~~~'):
            in_code = not in_code
            continue
        if in_code:
            continue
        cleaned = re.sub(r'`[^`]+`', '', line)
        for m in re.finditer(pattern, cleaned):
            text, url = m.group(1), m.group(2)
            if url.startswith(('http://', 'https://', '#', 'mailto:')):
                continue
            links.append((text, url))
    return links


def extract_code_refs(content: str) -> List[str]:
    """Extract code file references."""
    refs = []
    # [text](path with .cpp/.c/.h/.hpp)
    for m in re.finditer(r'\[[^\]]*\]\(([^)]*\.(?:cpp|c|h|hpp|py|cmake))\)', content):
        refs.append(m.group(1))
    # --8<-- "path"
    for m in re.finditer(r'--8<--\s+"([^"]+)"', content):
        refs.append(m.group(1))
    return refs


class DocGenerator:
    def __init__(self, docs_root: Path, code_root: Path):
        self.docs_root = docs_root
        self.code_root = code_root
        self.articles: List[Article] = []
        self._scan_articles()

    def _scan_articles(self):
        for filepath in sorted(self.docs_root.rglob('*.md')):
            if not is_article(filepath):
                continue

            try:
                content = filepath.read_text(encoding='utf-8')
            except Exception:
                continue

            fm, has_fm = parse_frontmatter(content)
            rel = str(filepath.relative_to(self.docs_root))
            parts = rel.split(os.sep)

            volume = parts[0] if parts else ''

            article = Article(
                path=filepath,
                rel_path=rel,
                frontmatter=fm,
                title=fm.get('title', filepath.stem),
                chapter=fm.get('chapter', 0) if isinstance(fm.get('chapter'), int) else 0,
                order=fm.get('order', 0) if isinstance(fm.get('order'), int) else 0,
                tags=fm.get('tags', []) if isinstance(fm.get('tags'), list) else [],
                difficulty=fm.get('difficulty', ''),
                platform=fm.get('platform', ''),
                reading_time_minutes=fm.get('reading_time_minutes', 0) or 0,
                cpp_standard=fm.get('cpp_standard', []) if isinstance(fm.get('cpp_standard'), list) else [],
                volume=volume,
                links=extract_links(content),
                code_refs=extract_code_refs(content),
            )
            self.articles.append(article)

    def generate_toc(self) -> str:
        """Generate table of contents."""
        lines = [f'# 文章目录', '', f'> 自动生成于 {date.today()}', '']

        # Group by volume
        volumes: Dict[str, List[Article]] = defaultdict(list)
        for a in self.articles:
            volumes[a.volume].append(a)

        for vol_key in sorted(volumes.keys()):
            vol_name = VOLUME_NAMES.get(vol_key, vol_key)
            articles = sorted(volumes[vol_key], key=lambda a: (a.chapter, a.order))
            lines.append(f'## {vol_name}')
            lines.append('')
            lines.append('| 序号 | 标题 | 平台 | 难度 | C++ 标准 |')
            lines.append('|------|------|------|------|----------|')

            for i, a in enumerate(articles, 1):
                platform = a.platform or '-'
                diff = {'beginner': '★☆☆', 'intermediate': '★★☆', 'advanced': '★★★'}.get(a.difficulty, '-')
                stds = ', '.join(f'C++{s}' for s in a.cpp_standard) if a.cpp_standard else '-'
                lines.append(f'| {i:02d} | {a.title} | {platform} | {diff} | {stds} |')

            lines.append('')

        return '\n'.join(lines)

    def generate_cross_references(self) -> str:
        """Generate cross-reference table between articles."""
        lines = [f'# 交叉引用表', '', f'> 自动生成于 {date.today()}', '']

        # Build link graph
        ref_graph: Dict[str, List[str]] = defaultdict(list)
        for a in self.articles:
            for text, url in a.links:
                url_clean = url.split('#')[0]
                if url_clean:
                    ref_graph[a.rel_path].append(url_clean)

        # Mermaid diagram (top 30 most connected)
        lines.append('## 引用关系图')
        lines.append('')
        lines.append('```mermaid')
        lines.append('graph LR')

        # Sort by number of links, take top entries
        sorted_articles = sorted(ref_graph.items(), key=lambda x: len(x[1]), reverse=True)
        node_ids = {}
        for i, (src, _) in enumerate(sorted_articles[:25]):
            stem = Path(src).stem[:20].replace('-', '_')
            node_ids[src] = f'n{i}'
            title = self._find_title(src)[:20]
            lines.append(f'    n{i}["{title}"]')

        lines.append('')

        seen_edges = set()
        for src, targets in sorted_articles[:25]:
            src_id = node_ids.get(src)
            if not src_id:
                continue
            for target in targets[:5]:
                tgt_id = node_ids.get(target)
                if tgt_id and (src_id, tgt_id) not in seen_edges:
                    lines.append(f'    {src_id} --> {tgt_id}')
                    seen_edges.add((src_id, tgt_id))

        lines.append('```')
        lines.append('')

        # Table
        lines.append('## 引用详情')
        lines.append('')
        lines.append('| 文章 | 引用目标 |')
        lines.append('|------|----------|')

        for src, targets in sorted(ref_graph.items(), key=lambda x: x[0]):
            if targets:
                title = self._find_title(src)
                unique_targets = sorted(set(targets))
                target_strs = [f'`{t}`' for t in unique_targets[:5]]
                extra = f' (+{len(unique_targets) - 5} more)' if len(unique_targets) > 5 else ''
                lines.append(f'| {title} | {", ".join(target_strs)}{extra} |')

        lines.append('')
        return '\n'.join(lines)

    def generate_code_index(self) -> str:
        """Generate reverse index: code file -> articles referencing it."""
        lines = [f'# 代码引用索引', '', f'> 自动生成于 {date.today()}', '']

        # Build reverse index
        code_to_articles: Dict[str, List[str]] = defaultdict(list)
        for a in self.articles:
            for ref in a.code_refs:
                code_to_articles[ref].append(a.title)

        if not code_to_articles:
            lines.append('_暂无代码文件引用。_')
            return '\n'.join(lines)

        lines.append(f'共 {len(code_to_articles)} 个代码文件被引用。')
        lines.append('')
        lines.append('| 代码文件 | 引用文章 |')
        lines.append('|----------|----------|')

        for code_file in sorted(code_to_articles.keys()):
            articles = code_to_articles[code_file]
            article_strs = ', '.join(sorted(set(articles)))
            lines.append(f'| `{code_file}` | {article_strs} |')

        lines.append('')
        return '\n'.join(lines)

    def generate_stats(self) -> str:
        """Generate project statistics report."""
        lines = [f'# 项目统计报告', '', f'> 自动生成于 {date.today()}', '']

        total = len(self.articles)
        total_reading_time = sum(a.reading_time_minutes for a in self.articles)

        # Count words (rough estimate)
        total_chars = 0
        for a in self.articles:
            try:
                content = a.path.read_text(encoding='utf-8')
                body = re.sub(r'^---\s*\n.*?\n---\s*\n', '', content, flags=re.DOTALL)
                total_chars += len(re.findall(r'[\u4e00-\u9fff]', body))
                total_chars += len(re.findall(r'[a-zA-Z]+', body))
            except Exception:
                pass

        lines.append('## 总览')
        lines.append('')
        lines.append('| 指标 | 数值 |')
        lines.append('|------|------|')
        lines.append(f'| 总文章数 | {total} |')
        lines.append(f'| 总字数（约） | {total_chars:,} |')
        lines.append(f'| 总阅读时间 | {total_reading_time} 分钟 (~{total_reading_time // 60} 小时) |')
        lines.append(f'| 平均阅读时间 | {total_reading_time / max(total, 1):.1f} 分钟 |')
        has_code = sum(1 for a in self.articles if a.code_refs)
        lines.append(f'| 有代码引用的文章 | {has_code} ({has_code / max(total, 1) * 100:.0f}%) |')
        lines.append('')

        # Platform distribution
        platforms = Counter(a.platform for a in self.articles if a.platform)
        if platforms:
            lines.append('## 平台分布')
            lines.append('')
            lines.append('| 平台 | 文章数 | 占比 |')
            lines.append('|------|--------|------|')
            for plat, count in platforms.most_common():
                pct = count / max(total, 1) * 100
                lines.append(f'| {plat} | {count} | {pct:.0f}% |')
            lines.append('')

        # Difficulty distribution
        difficulties = Counter(a.difficulty for a in self.articles if a.difficulty)
        if difficulties:
            lines.append('## 难度分布')
            lines.append('')
            lines.append('| 难度 | 文章数 | 占比 |')
            lines.append('|------|--------|------|')
            for diff in ['beginner', 'intermediate', 'advanced']:
                count = difficulties.get(diff, 0)
                if count > 0:
                    pct = count / max(total, 1) * 100
                    lines.append(f'| {diff} | {count} | {pct:.0f}% |')
            lines.append('')

        # Volume distribution
        volumes = Counter(a.volume for a in self.articles)
        lines.append('## 卷分布')
        lines.append('')
        lines.append('| 卷 | 名称 | 文章数 |')
        lines.append('|----|------|--------|')
        for vol_key in sorted(volumes.keys()):
            name = VOLUME_NAMES.get(vol_key, vol_key)
            lines.append(f'| {vol_key} | {name} | {volumes[vol_key]} |')
        lines.append('')

        # C++ standard coverage
        std_counter = Counter()
        for a in self.articles:
            for s in a.cpp_standard:
                std_counter[f'C++{s}'] += 1
        if std_counter:
            lines.append('## C++ 标准覆盖')
            lines.append('')
            lines.append('| 标准 | 文章数 |')
            lines.append('|------|--------|')
            for std in sorted(std_counter.keys()):
                lines.append(f'| {std} | {std_counter[std]} |')
            lines.append('')

        # Tag cloud
        tags = Counter()
        for a in self.articles:
            for t in a.tags:
                tags[t] += 1
        if tags:
            lines.append('## 标签频率')
            lines.append('')
            tag_parts = [f'{tag}: {count}' for tag, count in tags.most_common(30)]
            lines.append(' | '.join(tag_parts))
            lines.append('')

        return '\n'.join(lines)

    def _find_title(self, rel_path: str) -> str:
        for a in self.articles:
            if a.rel_path == rel_path:
                return a.title
        return Path(rel_path).stem

    def to_json(self) -> str:
        """Export all data as JSON."""
        data = {
            'generated': str(date.today()),
            'stats': {
                'total_articles': len(self.articles),
                'total_reading_time': sum(a.reading_time_minutes for a in self.articles),
                'by_platform': dict(Counter(a.platform for a in self.articles if a.platform)),
                'by_difficulty': dict(Counter(a.difficulty for a in self.articles if a.difficulty)),
                'by_volume': dict(Counter(a.volume for a in self.articles)),
            },
            'articles': [
                {
                    'path': a.rel_path,
                    'title': a.title,
                    'volume': a.volume,
                    'chapter': a.chapter,
                    'order': a.order,
                    'difficulty': a.difficulty,
                    'platform': a.platform,
                    'tags': a.tags,
                    'reading_time_minutes': a.reading_time_minutes,
                    'cpp_standard': a.cpp_standard,
                    'links': [{'text': t, 'url': u} for t, u in a.links],
                    'code_refs': a.code_refs,
                }
                for a in self.articles
            ],
        }
        return json.dumps(data, ensure_ascii=False, indent=2)


def main():
    parser = argparse.ArgumentParser(description='Generate documentation artifacts')
    parser.add_argument('--update', action='store_true',
                        help='Write generated files to documents/generated/')
    parser.add_argument('--ci', action='store_true',
                        help='CI mode: fail if generated files are stale')
    parser.add_argument('--format', choices=['markdown', 'json'], default='markdown',
                        help='Output format (default: markdown)')
    args = parser.parse_args()

    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    docs_root = project_root / 'documents'
    code_root = project_root / 'code'
    gen_dir = docs_root / 'generated'

    if not docs_root.exists():
        print(f"Error: documents/ directory not found: {docs_root}")
        sys.exit(1)

    gen = DocGenerator(docs_root, code_root)

    if args.format == 'json':
        output = gen.to_json()
        if args.update:
            gen_dir.mkdir(parents=True, exist_ok=True)
            (gen_dir / 'docs.json').write_text(output, encoding='utf-8')
            print(f"Written: documents/generated/docs.json")
        else:
            print(output)
        return

    # Generate all outputs
    outputs = {
        'toc.md': gen.generate_toc(),
        'cross-references.md': gen.generate_cross_references(),
        'code-index.md': gen.generate_code_index(),
        'stats.md': gen.generate_stats(),
    }

    if args.update:
        gen_dir.mkdir(parents=True, exist_ok=True)
        for filename, content in outputs.items():
            filepath = gen_dir / filename
            filepath.write_text(content, encoding='utf-8')
            print(f"Written: documents/generated/{filename}")
        # Also write JSON
        (gen_dir / 'docs.json').write_text(gen.to_json(), encoding='utf-8')
        print(f"Written: documents/generated/docs.json")
        return

    if args.ci:
        gen_dir.mkdir(parents=True, exist_ok=True)
        stale = []
        for filename, content in outputs.items():
            filepath = gen_dir / filename
            if not filepath.exists():
                stale.append(filename)
            else:
                existing = filepath.read_text(encoding='utf-8')
                if existing != content:
                    stale.append(filename)

        if stale:
            print(f"Stale generated files: {', '.join(stale)}")
            print("Run: python3 scripts/generate_docs.py --update")
            sys.exit(1)
        else:
            print("All generated files are up to date.")
            return

    # Default: print to stdout
    for filename, content in outputs.items():
        print(content)
        print()


if __name__ == '__main__':
    main()
