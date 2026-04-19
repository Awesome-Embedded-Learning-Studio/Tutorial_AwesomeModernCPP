#!/usr/bin/env python3
"""
Content Quality Checker for Tutorial Articles

Runs comprehensive quality checks on all Markdown documentation:
- Frontmatter validation
- Code reference existence
- Internal & external link validity
- Tag consistency
- Image reference validity

Usage:
    python3 scripts/check_quality.py documents/
    python3 scripts/check_quality.py documents/ --strict
    python3 scripts/check_quality.py documents/ --fix
"""

import abc
import argparse
import os
import re
import sys
import urllib.request
import urllib.error
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

# ---------------------------------------------------------------------------
# Re-use VALID_TAGS from validate_frontmatter.py (avoid duplication)
# ---------------------------------------------------------------------------

VALID_TAGS = {
    'RAII', '移动语义', '零开销抽象', '编译期计算', '类型安全',
    '内存管理', '异步编程', '模板元编程',
    'constexpr', 'consteval', 'constinit', 'lambda', 'CRTP',
    'concepts', 'coroutine', 'if_constexpr', '模板', '泛型',
    '对象池', '状态机', '工厂模式', '策略模式', '单例模式',
    '观察者模式', 'RAII守卫', '回调机制',
    'array', 'span', 'vector', 'map', 'unordered_map',
    '循环缓冲区', '侵入式容器', '容器',
    'unique_ptr', 'shared_ptr', 'weak_ptr', 'intrusive_ptr',
    '智能指针', '引用计数',
    'enum', 'enum_class', 'variant', 'optional', 'expected',
    '类型别名', '字面量',
    '函数对象', 'std_function', 'std_invoke', 'Ranges',
    'atomic', 'memory_order', 'mutex', '无锁',
    '嵌入式', '单片机', '外设管理', '寄存器', '链接器',
    '交叉编译', '工具链', 'CMake',
    '基础', '入门', '进阶', '实战', '优化', '工程实践',
    'host', 'stm32f1',
    'beginner', 'intermediate', 'advanced', 'cpp-modern',
}

VALID_DIFFICULTY = {'beginner', 'intermediate', 'advanced'}

REQUIRED_FM_FIELDS = {'title', 'chapter', 'order'}
RECOMMENDED_FM_FIELDS = {'description', 'tags'}

SKIP_FILENAMES = {'index.md', 'tags.md', 'README.md'}
SKIP_DIR_PARTS = {'images', 'generated'}

IMAGE_EXTENSIONS = {'.png', '.jpg', '.jpeg', '.gif', '.svg', '.webp'}
MAX_IMAGE_SIZE_MB = 2.0


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

@dataclass
class Issue:
    filepath: Path
    line: Optional[int]
    severity: str  # 'error' | 'warning'
    checker: str
    message: str


@dataclass
class Report:
    files_scanned: int = 0
    errors: List[Issue] = field(default_factory=list)
    warnings: List[Issue] = field(default_factory=list)

    @property
    def total_issues(self) -> int:
        return len(self.errors) + len(self.warnings)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def parse_frontmatter(content: str) -> Tuple[Dict, bool]:
    """Parse YAML frontmatter, returns (dict, has_frontmatter)."""
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
    """Check if a .md file should be treated as an article."""
    if filepath.name in SKIP_FILENAMES:
        return False
    if any(part in SKIP_DIR_PARTS for part in filepath.parts):
        return False
    return True


def extract_links(content: str) -> List[Tuple[int, str, str]]:
    """Extract (line_number, text, url) from markdown links, skipping code blocks."""
    links = []
    in_code_block = False
    pattern = r'\[([^\]]+)\]\(([^)]+)\)|\[([^\]]+)\]\(<([^>]+)>\)'

    for line_num, line in enumerate(content.split('\n'), 1):
        stripped = line.strip()
        if stripped.startswith('```') or stripped.startswith('~~~'):
            in_code_block = not in_code_block
            continue
        if in_code_block:
            continue

        cleaned = re.sub(r'`[^`]+`', '', line)
        for m in re.finditer(pattern, cleaned):
            groups = m.groups()
            if groups[1]:
                text, url = groups[0], groups[1]
            else:
                text, url = groups[2], groups[3]
            links.append((line_num, text, url))

    return links


def normalize_link(link_url: str, source_file: Path, root: Path) -> str:
    """Resolve a relative link to a path relative to root."""
    link_url = link_url.split('#')[0]
    if not link_url:
        return ''
    try:
        resolved = (source_file.parent / link_url).resolve()
        return str(resolved.relative_to(root))
    except (ValueError, RuntimeError):
        return link_url


# ---------------------------------------------------------------------------
# Checkers
# ---------------------------------------------------------------------------

class QualityChecker(abc.ABC):
    @abc.abstractmethod
    def check(self, filepath: Path, content: str,
              frontmatter: Dict, report: Report) -> None: ...


class FrontmatterChecker(QualityChecker):
    def check(self, filepath: Path, content: str,
              frontmatter: Dict, report: Report) -> None:
        rel = filepath
        fm, has_fm = parse_frontmatter(content)
        if not has_fm:
            report.warnings.append(Issue(rel, None, 'warning',
                                         'frontmatter', 'No frontmatter found'))
            return

        missing = REQUIRED_FM_FIELDS - set(fm.keys())
        if missing:
            report.errors.append(Issue(rel, None, 'error', 'frontmatter',
                                       f"Missing required fields: {', '.join(sorted(missing))}"))

        missing_rec = RECOMMENDED_FM_FIELDS - set(fm.keys())
        if missing_rec:
            report.warnings.append(Issue(rel, None, 'warning', 'frontmatter',
                                         f"Missing recommended fields: {', '.join(sorted(missing_rec))}"))

        # Validate types
        if 'chapter' in fm:
            ch = fm['chapter']
            if not isinstance(ch, int) or ch < 0 or ch > 100:
                report.errors.append(Issue(rel, None, 'error', 'frontmatter',
                                           f"Invalid chapter value: {ch}"))
        if 'order' in fm:
            od = fm['order']
            if not isinstance(od, int) or od < 0:
                report.errors.append(Issue(rel, None, 'error', 'frontmatter',
                                           f"Invalid order value: {od}"))
        if 'difficulty' in fm and fm['difficulty'] not in VALID_DIFFICULTY:
            report.errors.append(Issue(rel, None, 'error', 'frontmatter',
                                       f"Invalid difficulty: {fm['difficulty']}"))


class CodeReferenceChecker(QualityChecker):
    """Check that code file paths referenced in docs actually exist."""

    # Patterns: [text](../code/...) or --8<-- "code/..."
    CODE_LINK_RE = re.compile(r'\[([^\]]*)\]\(([^)]*(?:code/|\.cpp|\.c|\.h|\.hpp)[^)]*)\)', re.IGNORECASE)
    SNIPPET_RE = re.compile(r'--8<--\s+"([^"]+)"')

    def check(self, filepath: Path, content: str,
              frontmatter: Dict, report: Report) -> None:
        for m in self.CODE_LINK_RE.finditer(content):
            url = m.group(2)
            if url.startswith('http'):
                continue
            resolved = (filepath.parent / url).resolve()
            if not resolved.exists():
                line = content[:m.start()].count('\n') + 1
                report.errors.append(Issue(filepath, line, 'error',
                                           'code_reference',
                                           f"Code reference not found: {url}"))

        for m in self.SNIPPET_RE.finditer(content):
            path_str = m.group(1)
            resolved = (filepath.parent / path_str).resolve()
            if not resolved.exists():
                line = content[:m.start()].count('\n') + 1
                report.warnings.append(Issue(filepath, line, 'warning',
                                             'code_reference',
                                             f"Snippet not found: {path_str}"))


class InternalLinkChecker(QualityChecker):
    """Check internal (relative) markdown links exist."""

    def __init__(self, root: Path):
        self.root = root
        self.file_index: Set[str] = set()
        self._build_index()

    def _build_index(self):
        for f in self.root.rglob('*.md'):
            try:
                self.file_index.add(str(f.relative_to(self.root)))
            except ValueError:
                pass
        # Also index images
        for ext in IMAGE_EXTENSIONS:
            for f in self.root.rglob(f'*{ext}'):
                try:
                    self.file_index.add(str(f.relative_to(self.root)))
                except ValueError:
                    pass

    def check(self, filepath: Path, content: str,
              frontmatter: Dict, report: Report) -> None:
        links = extract_links(content)
        for line_num, text, url in links:
            if url.startswith(('http://', 'https://', 'mailto:', '#', 'tel:')):
                continue

            ext = Path(url.split('#')[0]).suffix.lower()
            normalized = normalize_link(url, filepath, self.root)
            if not normalized:
                continue

            if normalized not in self.file_index:
                # Try adding .md
                if not normalized.endswith('.md') and not ext:
                    normalized_md = normalized + '.md'
                    if normalized_md in self.file_index:
                        report.warnings.append(Issue(filepath, line_num, 'warning',
                                                     'internal_link',
                                                     f"Missing .md extension: [{text}]({url})"))
                        continue

                report.errors.append(Issue(filepath, line_num, 'error',
                                           'internal_link',
                                           f"Broken internal link: [{text}]({url})"))


class ExternalLinkChecker(QualityChecker):
    """Check external HTTP/HTTPS links return 2xx."""

    TIMEOUT = 10

    def check(self, filepath: Path, content: str,
              frontmatter: Dict, report: Report) -> None:
        links = extract_links(content)
        for line_num, text, url in links:
            if not url.startswith(('http://', 'https://')):
                continue
            try:
                req = urllib.request.Request(url, method='HEAD',
                                             headers={'User-Agent': 'Mozilla/5.0'})
                resp = urllib.request.urlopen(req, timeout=self.TIMEOUT)
                if resp.status >= 400:
                    report.warnings.append(Issue(filepath, line_num, 'warning',
                                                 'external_link',
                                                 f"External link returned {resp.status}: {url}"))
            except urllib.error.HTTPError as e:
                report.warnings.append(Issue(filepath, line_num, 'warning',
                                             'external_link',
                                             f"External link error ({e.code}): {url}"))
            except Exception as e:
                report.warnings.append(Issue(filepath, line_num, 'warning',
                                             'external_link',
                                             f"External link unreachable: {url} ({type(e).__name__})"))


class TagConsistencyChecker(QualityChecker):
    def check(self, filepath: Path, content: str,
              frontmatter: Dict, report: Report) -> None:
        fm, has_fm = parse_frontmatter(content)
        if not has_fm:
            return
        tags = fm.get('tags', [])
        if not isinstance(tags, list):
            return
        for tag in tags:
            if tag not in VALID_TAGS:
                report.errors.append(Issue(filepath, None, 'error',
                                           'tag_consistency',
                                           f"Unknown tag: '{tag}'"))


class ImageReferenceChecker(QualityChecker):
    """Check image references exist, have valid format, and reasonable size."""

    IMG_RE = re.compile(r'!\[([^\]]*)\]\(([^)]+)\)')

    def __init__(self, root: Path):
        self.root = root

    def check(self, filepath: Path, content: str,
              frontmatter: Dict, report: Report) -> None:
        for m in self.IMG_RE.finditer(content):
            alt, url = m.group(1), m.group(2)
            if url.startswith(('http://', 'https://')):
                continue

            line = content[:m.start()].count('\n') + 1
            normalized = normalize_link(url, filepath, self.root)
            if not normalized:
                continue

            resolved = self.root / normalized
            if not resolved.exists():
                report.errors.append(Issue(filepath, line, 'error',
                                           'image_reference',
                                           f"Image not found: {url}"))
                continue

            ext = resolved.suffix.lower()
            if ext not in IMAGE_EXTENSIONS:
                report.warnings.append(Issue(filepath, line, 'warning',
                                             'image_reference',
                                             f"Unusual image format: {ext}"))

            size_mb = resolved.stat().st_size / (1024 * 1024)
            if size_mb > MAX_IMAGE_SIZE_MB:
                report.warnings.append(Issue(filepath, line, 'warning',
                                             'image_reference',
                                             f"Large image ({size_mb:.1f}MB): {url}"))


class ReadingTimeChecker(QualityChecker):
    """Verify reading_time_minutes matches estimated value (if present)."""

    def check(self, filepath: Path, content: str,
              frontmatter: Dict, report: Report) -> None:
        fm, has_fm = parse_frontmatter(content)
        if not has_fm:
            return
        declared = fm.get('reading_time_minutes')
        if declared is None:
            return  # Not set, skip

        # Estimate: strip frontmatter, count Chinese chars + English words
        body = re.sub(r'^---\s*\n.*?\n---\s*\n', '', content, flags=re.DOTALL)
        # Strip code blocks (50% speed)
        code_blocks = re.findall(r'```[\s\S]*?```', body)
        code_text = '\n'.join(code_blocks)
        plain_text = re.sub(r'```[\s\S]*?```', '', body)

        chinese_chars = len(re.findall(r'[\u4e00-\u9fff]', plain_text))
        english_words = len(re.findall(r'[a-zA-Z]+', plain_text))
        code_chinese = len(re.findall(r'[\u4e00-\u9fff]', code_text))
        code_english = len(re.findall(r'[a-zA-Z]+', code_text))

        # Chinese: 300 chars/min, English: 200 words/min, code: half speed
        minutes = ((chinese_chars / 300) + (english_words / 200) +
                   (code_chinese / 150) + (code_english / 100))

        estimated = max(1, round(minutes))
        if declared > 0:
            deviation = abs(declared - estimated) / max(estimated, 1) * 100
            if deviation > 50:
                report.warnings.append(Issue(filepath, None, 'warning',
                                             'reading_time',
                                             f"reading_time_minutes={declared} "
                                             f"but estimated={estimated} "
                                             f"(deviation {deviation:.0f}%)"))


# ---------------------------------------------------------------------------
# Main runner
# ---------------------------------------------------------------------------

def run_all_checkers(directory: Path, fix: bool = False) -> Report:
    report = Report()

    md_files = sorted(f for f in directory.rglob('*.md') if is_article(f))
    report.files_scanned = len(md_files)

    # Initialize checkers that need root context
    int_link_checker = InternalLinkChecker(directory)
    img_checker = ImageReferenceChecker(directory)

    checkers: List[QualityChecker] = [
        FrontmatterChecker(),
        CodeReferenceChecker(),
        int_link_checker,
        # ExternalLinkChecker is expensive; skip unless explicitly requested
        TagConsistencyChecker(),
        img_checker,
        ReadingTimeChecker(),
    ]

    print(f"Scanning {report.files_scanned} markdown files...")
    print()

    for filepath in md_files:
        try:
            content = filepath.read_text(encoding='utf-8')
        except Exception as e:
            report.errors.append(Issue(filepath, None, 'error', 'io',
                                       f"Failed to read: {e}"))
            continue

        fm, _ = parse_frontmatter(content)
        for checker in checkers:
            checker.check(filepath, content, fm, report)

        # Auto-fix reading_time_minutes if requested
        if fix and _:
            _fix_reading_time(filepath, content, fm, report)

    return report


def _fix_reading_time(filepath: Path, content: str,
                      fm: Dict, report: Report) -> None:
    """Auto-calculate and update reading_time_minutes in frontmatter."""
    if not fm:
        return
    body = re.sub(r'^---\s*\n.*?\n---\s*\n', '', content, flags=re.DOTALL)
    chinese_chars = len(re.findall(r'[\u4e00-\u9fff]', body))
    english_words = len(re.findall(r'[a-zA-Z]+', body))
    minutes = max(1, round((chinese_chars / 300) + (english_words / 200)))

    old_val = fm.get('reading_time_minutes')
    if old_val != minutes:
        fm['reading_time_minutes'] = minutes
        # Reconstruct frontmatter
        try:
            import yaml
            new_fm = yaml.dump(fm, allow_unicode=True, default_flow_style=False).strip()
            new_content = f"---\n{new_fm}\n---\n{body}"
            filepath.write_text(new_content, encoding='utf-8')
            print(f"  Fixed reading_time_minutes: {old_val} -> {minutes} for {filepath}")
        except ImportError:
            pass


def print_report(report: Report) -> None:
    print()
    print("=" * 60)
    print("Content Quality Report")
    print("=" * 60)
    print(f"Files scanned:  {report.files_scanned}")

    all_issues = list(report.errors) + list(report.warnings)
    print(f"Issues:         {len(all_issues)}")
    print()

    if all_issues:
        print("ERRORS:")
        print("-" * 60)
        for issue in all_issues[:50]:
            loc = f"{issue.filepath}"
            if issue.line:
                loc += f":{issue.line}"
            print(f"  [{issue.checker}] {loc} - {issue.message}")
        if len(all_issues) > 50:
            print(f"  ... and {len(all_issues) - 50} more errors")
        print()

    if len(all_issues) == 0:
        print("All checks passed!")
    else:
        print(f"Result: FAILED ({len(all_issues)} issue(s))")


def main():
    parser = argparse.ArgumentParser(
        description='Content quality checker for tutorial articles')
    parser.add_argument('path', nargs='?', default='documents',
                        help='Directory to check (default: documents/)')
    parser.add_argument('--fix', action='store_true',
                        help='Auto-fix issues where possible')
    parser.add_argument('--check-external', action='store_true',
                        help='Also check external HTTP links (slow)')
    args = parser.parse_args()

    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    target = project_root / args.path

    if not target.exists():
        print(f"Error: Directory not found: {target}")
        sys.exit(1)

    report = run_all_checkers(target, fix=args.fix)

    # Optionally add external link checker
    if args.check_external:
        print("\nChecking external links...")
        ext_checker = ExternalLinkChecker()
        md_files = sorted(f for f in target.rglob('*.md') if is_article(f))
        for filepath in md_files:
            try:
                content = filepath.read_text(encoding='utf-8')
                fm, _ = parse_frontmatter(content)
                ext_checker.check(filepath, content, fm, report)
            except Exception:
                pass

    print_report(report)

    fail_count = len(report.errors) + len(report.warnings)
    sys.exit(1 if fail_count > 0 else 0)


if __name__ == '__main__':
    main()
