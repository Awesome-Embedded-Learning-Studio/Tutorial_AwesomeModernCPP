#!/usr/bin/env python3
"""
Frontmatter Validation Script for Tutorial Articles

Validates that all markdown articles have proper frontmatter with required fields.
Usage: python scripts/validate_frontmatter.py
"""

import re
import sys
from pathlib import Path
from typing import Dict, List, Set, Tuple

# Valid tags taxonomy
VALID_TAGS = {
    # Concepts
    'RAII', '移动语义', '零开销抽象', '编译期计算', '类型安全',
    '内存管理', '异步编程', '模板元编程',

    # Language features
    'constexpr', 'consteval', 'constinit', 'lambda', 'CRTP',
    'concepts', 'coroutine', 'if_constexpr', '模板', '泛型',

    # Patterns
    '对象池', '状态机', '工厂模式', '策略模式', '单例模式',
    '观察者模式', 'RAII守卫', '回调机制',

    # Containers
    'array', 'span', 'vector', 'map', 'unordered_map',
    '循环缓冲区', '侵入式容器', '容器',

    # Smart pointers
    'unique_ptr', 'shared_ptr', 'weak_ptr', 'intrusive_ptr',
    '智能指针', '引用计数',

    # Type safety
    'enum', 'enum_class', 'variant', 'optional', 'expected',
    '类型别名', '字面量',

    # Functional
    '函数对象', 'std_function', 'std_invoke', 'Ranges',

    # Concurrency
    'atomic', 'memory_order', 'mutex', '无锁',

    # Embedded specific
    '嵌入式', '单片机', '外设管理', '寄存器', '链接器',
    '交叉编译', '工具链', 'CMake',

    # General
    '基础', '入门', '进阶', '实战', '优化'
}

VALID_DIFFICULTY = {'beginner', 'intermediate', 'advanced'}
VALID_CPP_STANDARDS = {'11', '14', '17', '20', '23'}

# Required fields for articles
REQUIRED_FIELDS = {'title', 'chapter', 'order'}
RECOMMENDED_FIELDS = {'description', 'tags'}


class FrontmatterValidator:
    def __init__(self, tutorial_dir: Path):
        self.tutorial_dir = tutorial_dir
        self.errors: List[str] = []
        self.warnings: List[str] = []
        self.stats = {
            'total': 0,
            'valid': 0,
            'with_errors': 0,
            'with_warnings': 0
        }

    def parse_frontmatter(self, content: str) -> Tuple[Dict, bool]:
        """Parse YAML frontmatter from markdown content."""
        match = re.match(r'^---\s*\n(.*?)\n---\s*\n(.*)', content, re.DOTALL)
        if not match:
            return {}, False

        try:
            import yaml
            frontmatter = yaml.safe_load(match.group(1))
            return frontmatter if frontmatter else {}, True
        except ImportError:
            print("Warning: PyYAML not installed, skipping YAML validation")
            return {}, True
        except yaml.YAMLError as e:
            return {}, False

    def validate_field_types(self, frontmatter: Dict, filepath: Path):
        """Validate field types and values."""
        # Validate chapter
        if 'chapter' in frontmatter:
            chapter = frontmatter['chapter']
            if not isinstance(chapter, int) or chapter < 0 or chapter > 100:
                self.errors.append(f"{filepath}: Invalid chapter value: {chapter}")

        # Validate order
        if 'order' in frontmatter:
            order = frontmatter['order']
            if not isinstance(order, int) or order < 0:
                self.errors.append(f"{filepath}: Invalid order value: {order}")

        # Validate difficulty
        if 'difficulty' in frontmatter:
            difficulty = frontmatter['difficulty']
            if difficulty not in VALID_DIFFICULTY:
                self.errors.append(
                    f"{filepath}: Invalid difficulty: {difficulty}. "
                    f"Must be one of {VALID_DIFFICULTY}"
                )

        # Validate cpp_standard
        if 'cpp_standard' in frontmatter:
            std = frontmatter['cpp_standard']
            if isinstance(std, list):
                for s in std:
                    if str(s) not in VALID_CPP_STANDARDS:
                        self.errors.append(
                            f"{filepath}: Invalid C++ standard: {s}. "
                            f"Valid values: {VALID_CPP_STANDARDS}"
                        )
            elif str(std) not in VALID_CPP_STANDARDS:
                self.errors.append(f"{filepath}: Invalid C++ standard: {std}")

        # Validate tags
        if 'tags' in frontmatter:
            tags = frontmatter['tags']
            if isinstance(tags, list):
                for tag in tags:
                    if tag not in VALID_TAGS:
                        self.warnings.append(
                            f"{filepath}: Unknown tag: '{tag}'. "
                            f"Consider adding it to VALID_TAGS if appropriate."
                        )

    def validate_file(self, filepath: Path) -> bool:
        """Validate a single markdown file."""
        self.stats['total'] += 1

        content = filepath.read_text(encoding='utf-8')
        frontmatter, has_frontmatter = self.parse_frontmatter(content)

        if not has_frontmatter:
            self.warnings.append(f"{filepath}: No frontmatter found (skipping validation)")
            return True

        # Check required fields
        missing = REQUIRED_FIELDS - set(frontmatter.keys())
        if missing:
            self.errors.append(
                f"{filepath}: Missing required fields: {', '.join(missing)}"
            )

        # Check for recommended fields
        missing_recommended = RECOMMENDED_FIELDS - set(frontmatter.keys())
        if missing_recommended:
            self.warnings.append(
                f"{filepath}: Missing recommended fields: {', '.join(missing_recommended)}"
            )

        # Validate field types
        self.validate_field_types(frontmatter, filepath)

        # Count results
        if any(str(filepath) in e for e in self.errors):
            self.stats['with_errors'] += 1
        elif any(str(filepath) in w for w in self.warnings):
            self.stats['with_warnings'] += 1
        else:
            self.stats['valid'] += 1

        return len([e for e in self.errors if str(filepath) in e]) == 0

    def run(self) -> bool:
        """Run validation on all markdown files in tutorial directory."""
        md_files = list(self.tutorial_dir.rglob('*.md'))

        # Skip index.md files and tags.md (they don't need frontmatter)
        md_files = [
            f for f in md_files
            if f.name not in {'index.md', 'tags.md'}
        ]

        print(f"Validating {len(md_files)} markdown files...")
        print()

        for filepath in md_files:
            self.validate_file(filepath)

        self.print_summary()
        return self.stats['with_errors'] == 0

    def print_summary(self):
        """Print validation summary."""
        print("=" * 60)
        print("Validation Summary")
        print("=" * 60)
        print(f"Total files checked: {self.stats['total']}")
        print(f"Valid:              {self.stats['valid']}")
        print(f"With warnings:      {self.stats['with_warnings']}")
        print(f"With errors:        {self.stats['with_errors']}")
        print()

        if self.warnings:
            print("Warnings:")
            print("-" * 60)
            for warning in self.warnings[:10]:  # Show first 10
                print(f"  ⚠ {warning}")
            if len(self.warnings) > 10:
                print(f"  ... and {len(self.warnings) - 10} more warnings")
            print()

        if self.errors:
            print("Errors:")
            print("-" * 60)
            for error in self.errors[:10]:  # Show first 10
                print(f"  ✗ {error}")
            if len(self.errors) > 10:
                print(f"  ... and {len(self.errors) - 10} more errors")
            print()

        if self.stats['with_errors'] == 0:
            print("✓ All files passed validation!")
        else:
            print("✗ Some files have errors. Please fix them before committing.")


def main():
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    tutorial_dir = project_root / 'documents'

    if not tutorial_dir.exists():
        print(f"Error: Tutorial directory not found: {tutorial_dir}")
        sys.exit(1)

    validator = FrontmatterValidator(tutorial_dir)
    success = validator.run()

    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
