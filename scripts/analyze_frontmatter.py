#!/usr/bin/env python3
"""
Frontmatter Statistics Analyzer

Analyzes tutorial articles to show the value of frontmatter metadata.
Usage: python scripts/analyze_frontmatter.py
"""

import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Dict, List


def parse_frontmatter(content: str) -> tuple:
    """Parse YAML frontmatter, returns (frontmatter_dict, has_frontmatter)."""
    import re
    match = re.match(r'^---\s*\n(.*?)\n---', content, re.DOTALL)
    if not match:
        return {}, False

    try:
        import yaml
        frontmatter = yaml.safe_load(match.group(1))
        return frontmatter if frontmatter else {}, True
    except ImportError:
        print("Warning: PyYAML not installed, skipping detailed analysis")
        return {}, True
    except Exception:
        return {}, True


class FrontmatterAnalyzer:
    def __init__(self, tutorial_dir: Path):
        self.tutorial_dir = tutorial_dir
        self.stats = {
            'total': 0,
            'with_frontmatter': 0,
            'without_frontmatter': 0,
        }
        self.by_difficulty = Counter()
        self.by_chapter = defaultdict(int)
        self.by_tags = Counter()
        self.total_reading_time = 0
        self.cpp_standards = Counter()

    def analyze_file(self, filepath: Path, rel_path: str):
        """Analyze a single markdown file."""
        self.stats['total'] += 1

        try:
            content = filepath.read_text(encoding='utf-8')
        except Exception:
            return

        frontmatter, has_fm = parse_frontmatter(content)

        if not has_fm:
            self.stats['without_frontmatter'] += 1
            return

        self.stats['with_frontmatter'] += 1

        # Count by difficulty
        difficulty = frontmatter.get('difficulty', 'unknown')
        self.by_difficulty[difficulty] += 1

        # Count by chapter
        chapter = frontmatter.get('chapter', 'unknown')
        self.by_chapter[chapter] += 1

        # Count tags
        tags = frontmatter.get('tags', [])
        if isinstance(tags, list):
            for tag in tags:
                self.by_tags[tag] += 1

        # Reading time
        reading_time = frontmatter.get('reading_time_minutes', 0)
        self.total_reading_time += reading_time

        # C++ standards
        standards = frontmatter.get('cpp_standard', [])
        if isinstance(standards, list):
            for std in standards:
                self.cpp_standards[f'C++{std}'] += 1

    def run(self):
        """Run analysis on all markdown files."""
        md_files = list(self.tutorial_dir.rglob('*.md'))
        md_files = [f for f in md_files if f.name not in {'index.md', 'tags.md'}]

        print(f"Analyzing {len(md_files)} markdown files...")
        print()

        for filepath in md_files:
            rel_path = str(filepath.relative_to(self.tutorial_dir))
            self.analyze_file(filepath, rel_path)

        self.print_report()

    def print_report(self):
        """Print analysis report."""
        print("=" * 60)
        print("Frontmatter 分析报告")
        print("=" * 60)
        print()

        # Overall status
        print("📊 总体状态")
        print("-" * 60)
        print(f"  总文章数:     {self.stats['total']}")
        print(f"  有元数据:     {self.stats['with_frontmatter']} "
              f"({self.stats['with_frontmatter']/self.stats['total']*100:.1f}%)")
        print(f"  无元数据:     {self.stats['without_frontmatter']} "
              f"({self.stats['without_frontmatter']/self.stats['total']*100:.1f}%)")
        print()

        # Difficulty distribution
        if self.by_difficulty:
            print("📈 难度分布")
            print("-" * 60)
            total_with_difficulty = sum(self.by_difficulty.values())
            for level in ['beginner', 'intermediate', 'advanced']:
                count = self.by_difficulty.get(level, 0)
                if count > 0:
                    pct = count / total_with_difficulty * 100
                    emoji = {'beginner': '🟢', 'intermediate': '🟡', 'advanced': '🔴'}
                    print(f"  {emoji.get(level, '⚪')} {level:12} {count:3} 篇 ({pct:5.1f}%)")
            print()

        # Chapter distribution
        if self.by_chapter:
            print("📚 章节分布")
            print("-" * 60)
            for chapter in sorted(self.by_chapter.keys()):
                if chapter != 'unknown':
                    count = self.by_chapter[chapter]
                    print(f"  Chapter {chapter:2}   {count:2} 篇")
            print()

        # Top tags
        if self.by_tags:
            print("🏷️ 热门标签 (Top 10)")
            print("-" * 60)
            for tag, count in self.by_tags.most_common(10):
                print(f"  {tag:20} {count:2} 篇")
            print()

        # Reading time
        if self.total_reading_time > 0:
            print("⏱️ 阅读时间")
            print("-" * 60)
            print(f"  总计:         {self.total_reading_time} 分钟")
            print(f"  平均每篇:     {self.total_reading_time/self.stats['with_frontmatter']:.1f} 分钟")
            print(f"  完整阅读:     ~{self.total_reading_time//60} 小时 {self.total_reading_time%60} 分钟")
            print()

        # C++ standards
        if self.cpp_standards:
            print("⚙️ C++ 标准覆盖")
            print("-" * 60)
            for std in sorted(self.cpp_standards.keys()):
                count = self.cpp_standards[std]
                print(f"  {std:8} {count:2} 篇")
            print()

        # Value proposition
        print("=" * 60)
        print("💡 元数据带来的价值")
        print("=" * 60)

        if self.stats['with_frontmatter'] > 0:
            print(f"  ✅ 标签系统:  {len(self.by_tags)} 个不同标签")
            print(f"  ✅ 难度分级:  {len(self.by_difficulty)} 个级别")
            print(f"  ✅ 学习路径:  ~{self.total_reading_time//60} 小时完整内容")
            print(f"  ✅ 内容发现:  平均每篇 {len(self.by_tags)/max(self.stats['with_frontmatter'],1):.1f} 个标签")
        else:
            print("  ⏸️ 添加 frontmatter 后启用以上功能")

        print()
        print("提示: 运行 'python scripts/validate_frontmatter.py' 验证元数据")


def main():
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    tutorial_dir = project_root / 'tutorial'

    if not tutorial_dir.exists():
        print(f"Error: Tutorial directory not found: {tutorial_dir}")
        sys.exit(1)

    analyzer = FrontmatterAnalyzer(tutorial_dir)
    analyzer.run()


if __name__ == '__main__':
    main()
