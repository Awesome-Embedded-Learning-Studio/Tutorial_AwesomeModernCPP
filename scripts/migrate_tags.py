#!/usr/bin/env python3
"""
Migrate frontmatter tags in tutorial markdown files to a standardized four-dimension system.

Tag dimensions:
  - platform: stm32f1 | stm32f4 | esp32 | rp2040 | host
  - topic: cpp-modern | peripheral | rtos | debugging | toolchain | architecture
  - difficulty: beginner | intermediate | advanced
  - peripheral (optional): gpio | uart | spi | i2c | adc | timer | pwm | dma | can

Usage: python3 scripts/migrate_tags.py [--dry-run]
"""

import os
import re
import sys
import argparse
from pathlib import Path

# ── Tag mapping: old tag → list of new standardized tags ──
TAG_MAP = {
    # ── cpp-modern topic ──
    'RAII': ['cpp-modern'],
    '智能指针': ['cpp-modern'],
    'unique_ptr': ['cpp-modern'],
    'shared_ptr': ['cpp-modern'],
    'intrusive_ptr': ['cpp-modern'],
    '移动语义': ['cpp-modern'],
    '右值引用': ['cpp-modern'],
    'RVO': ['cpp-modern'],
    'NRVO': ['cpp-modern'],
    '完美转发': ['cpp-modern'],
    'copy elision': ['cpp-modern'],
    'constexpr': ['cpp-modern'],
    'consteval': ['cpp-modern'],
    'constinit': ['cpp-modern'],
    '编译期': ['cpp-modern'],
    '编译期计算': ['cpp-modern'],
    '模板': ['cpp-modern'],
    '模板特化': ['cpp-modern'],
    '模板实例化': ['cpp-modern'],
    '泛型编程': ['cpp-modern'],
    'CRTP': ['cpp-modern'],
    'SFINAE': ['cpp-modern'],
    '类型安全': ['cpp-modern'],
    '类型推断': ['cpp-modern'],
    '类型萃取': ['cpp-modern'],
    '类型特征': ['cpp-modern'],
    'lambda': ['cpp-modern'],
    'Lambda': ['cpp-modern'],
    '函数式': ['cpp-modern'],
    'ranges': ['cpp-modern'],
    '视图': ['cpp-modern'],
    '管道': ['cpp-modern'],
    'variant': ['cpp-modern'],
    'optional': ['cpp-modern'],
    'expected': ['cpp-modern'],
    'any': ['cpp-modern'],
    'scope guard': ['cpp-modern'],
    '设计模式': ['cpp-modern'],
    '零开销抽象': ['cpp-modern'],
    '零成本抽象': ['cpp-modern'],
    'zero-overhead': ['cpp-modern'],
    'C++11': ['cpp-modern'],
    'C++14': ['cpp-modern'],
    'C++17': ['cpp-modern'],
    'C++20': ['cpp-modern'],
    'C++23': ['cpp-modern'],
    'atomic': ['cpp-modern'],
    '原子操作': ['cpp-modern'],
    '并发': ['cpp-modern'],
    '临界区': ['cpp-modern'],
    '同步': ['cpp-modern'],
    '锁': ['cpp-modern'],
    '互斥': ['cpp-modern'],
    '结构化绑定': ['cpp-modern'],
    '枚举类': ['cpp-modern'],
    '用户自定义字面量': ['cpp-modern'],
    'array': ['cpp-modern'],
    'span': ['cpp-modern'],
    '容器': ['cpp-modern'],
    '循环缓冲区': ['cpp-modern'],
    '环形缓冲区': ['cpp-modern'],
    '空基类': ['cpp-modern'],
    'EBO': ['cpp-modern'],
    '静态多态': ['cpp-modern'],
    '协程': ['cpp-modern'],
    'concept': ['cpp-modern'],
    'concepts': ['cpp-modern'],
    '约束': ['cpp-modern'],
    'requires': ['cpp-modern'],
    '模块': ['cpp-modern'],
    'modules': ['cpp-modern'],
    '内存管理': ['cpp-modern'],
    '内存策略': ['cpp-modern'],
    '分配器': ['cpp-modern'],
    '固定池': ['cpp-modern'],
    '对象池': ['cpp-modern'],
    '资源管理': ['cpp-modern'],
    '回调': ['cpp-modern'],
    '事件驱动': ['cpp-modern'],
    '性能': ['architecture'],
    '性能优化': ['architecture'],
    '优化': ['architecture'],

    # ── peripheral topic + specific peripheral ──
    'GPIO': ['peripheral', 'gpio'],
    'gpio': ['peripheral', 'gpio'],
    'UART': ['peripheral', 'uart'],
    'uart': ['peripheral', 'uart'],
    '串口': ['peripheral', 'uart'],
    'SPI': ['peripheral', 'spi'],
    'spi': ['peripheral', 'spi'],
    'I2C': ['peripheral', 'i2c'],
    'i2c': ['peripheral', 'i2c'],
    'ADC': ['peripheral', 'adc'],
    'adc': ['peripheral', 'adc'],
    'Timer': ['peripheral', 'timer'],
    'timer': ['peripheral', 'timer'],
    '定时器': ['peripheral', 'timer'],
    'PWM': ['peripheral', 'pwm'],
    'pwm': ['peripheral', 'pwm'],
    'DMA': ['peripheral', 'dma'],
    'dma': ['peripheral', 'dma'],
    'CAN': ['peripheral', 'can'],
    'can': ['peripheral', 'can'],
    '寄存器': ['peripheral'],
    'type-safe-register-access': ['peripheral'],
    '外设': ['peripheral'],
    '中断': ['peripheral'],
    'ISR': ['peripheral'],
    'LED': ['peripheral', 'gpio'],
    'led': ['peripheral', 'gpio'],
    'Button': ['peripheral', 'gpio'],
    'button': ['peripheral', 'gpio'],
    '按键': ['peripheral', 'gpio'],
    '输入': ['peripheral', 'gpio'],
    '输出': ['peripheral', 'gpio'],

    # ── toolchain topic ──
    'CMake': ['toolchain'],
    'cmake': ['toolchain'],
    '交叉编译': ['toolchain'],
    '工具链': ['toolchain'],
    '链接器': ['toolchain'],
    '链接脚本': ['toolchain'],
    '编译器': ['toolchain'],
    '编译器选项': ['toolchain'],
    '编译器优化': ['toolchain'],
    '构建系统': ['toolchain'],
    'Makefile': ['toolchain'],
    '工具': ['toolchain'],

    # ── architecture topic ──
    '嵌入式': ['architecture'],
    '嵌入式封装': ['architecture'],
    '架构': ['architecture'],
    '设计': ['architecture'],

    # ── rtos topic ──
    'RTOS': ['rtos'],
    'rtos': ['rtos'],
    'FreeRTOS': ['rtos'],
    'freertos': ['rtos'],
    '任务调度': ['rtos'],
    '调度器': ['rtos'],
    '信号量': ['rtos'],
    '互斥量': ['rtos'],
    '消息队列': ['rtos'],

    # ── debugging topic ──
    '调试': ['debugging'],
    'debug': ['debugging'],
    'GDB': ['debugging'],
    'GDB调试': ['debugging'],
    '断点': ['debugging'],
    '日志': ['debugging'],
}

# Difficulty mapping
DIFFICULTY_MAP = {
    '基础': 'beginner',
    '入门': 'beginner',
    '初级': 'beginner',
    '中级': 'intermediate',
    '进阶': 'intermediate',
    '高级': 'advanced',
    '深入': 'advanced',
}

# ── Path-based platform inference ──
def infer_platform(filepath: str) -> str:
    """Infer platform tag from file path."""
    fp = filepath.replace('\\', '/')
    if '/vol8-domains/embedded/' in fp:
        return 'stm32f1'
    return 'host'


def infer_difficulty_from_tags(old_tags: list, filepath: str) -> str:
    """Infer difficulty from old tags or file path."""
    for tag in old_tags:
        if tag in DIFFICULTY_MAP:
            return DIFFICULTY_MAP[tag]
    # Default based on content location
    fp = filepath.replace('\\', '/')
    if '/00-env-setup/' in fp:
        return 'beginner'
    if '/01-led/' in fp:
        return 'beginner'
    if '/02-button/' in fp:
        return 'intermediate'
    return 'intermediate'


def map_tags(old_tags: list, filepath: str) -> tuple:
    """Map old tags to standardized tags. Returns (new_tags, platform, difficulty)."""
    new_tags = set()
    has_topic = False
    has_difficulty = False
    has_platform = False

    for tag in old_tags:
        # Skip malformed long tags (sentences pasted into tag field)
        if len(tag) > 30:
            continue
        if tag in TAG_MAP:
            mapped = TAG_MAP[tag]
            new_tags.update(mapped)
            for m in mapped:
                if m in ('cpp-modern', 'peripheral', 'rtos', 'debugging', 'toolchain', 'architecture'):
                    has_topic = True
                if m in ('beginner', 'intermediate', 'advanced'):
                    has_difficulty = True
                if m in ('stm32f1', 'stm32f4', 'esp32', 'rp2040', 'host'):
                    has_platform = True

    # Ensure required dimensions are present
    platform = infer_platform(filepath)
    if not has_platform:
        new_tags.add(platform)

    difficulty = 'intermediate'  # default
    if not has_topic:
        new_tags.add('cpp-modern')  # default topic
    if not has_difficulty:
        difficulty = infer_difficulty_from_tags(old_tags, filepath)
        new_tags.add(difficulty)

    return sorted(new_tags), platform, difficulty


def parse_frontmatter(content: str):
    """Parse YAML frontmatter from markdown content. Returns (frontmatter_str, body_str) or (None, content)."""
    if not content.startswith('---'):
        return None, content
    end = content.find('---', 3)
    if end == -1:
        return None, content
    return content[3:end].strip(), content[end + 3:]


def extract_tags_from_fm(fm_str: str) -> list:
    """Extract tags list from frontmatter string (simple parser)."""
    lines = fm_str.split('\n')
    in_tags = False
    tags = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith('tags:'):
            in_tags = True
            # Check if tags are on the same line: tags: [a, b] or tags: a
            rest = stripped[5:].strip()
            if rest.startswith('['):
                # Inline list: [tag1, tag2]
                items = rest.strip('[]').split(',')
                tags = [t.strip().strip('"\'') for t in items if t.strip()]
                in_tags = False
            elif rest and not rest.startswith('-'):
                # Single value: tags: value
                tags = [rest.strip('"\'')]
                in_tags = False
            continue
        if in_tags:
            if stripped.startswith('- '):
                tag = stripped[2:].strip().strip('"\'')
                tags.append(tag)
            elif not stripped:
                continue
            else:
                in_tags = False
    return tags


def get_fm_field(fm_str: str, field: str) -> str:
    """Get a top-level field value from frontmatter string."""
    for line in fm_str.split('\n'):
        if line.startswith(field + ':') or line.startswith(field + ' :'):
            val = line.split(':', 1)[1].strip().strip('"\'')
            return val
    return ''


def set_fm_field(fm_str: str, field: str, value: str) -> str:
    """Set or add a top-level field in frontmatter string."""
    lines = fm_str.split('\n')
    found = False
    result = []
    for line in lines:
        if line.startswith(field + ':') or line.startswith(field + ' :'):
            result.append(f"{field}: {value}")
            found = True
        else:
            result.append(line)
    if not found:
        result.append(f"{field}: {value}")
    return '\n'.join(result)


def replace_tags_in_fm(fm_str: str, new_tags: list) -> str:
    """Replace the tags list in frontmatter string."""
    lines = fm_str.split('\n')
    result = []
    in_tags = False
    tags_inserted = False

    for line in lines:
        stripped = line.strip()
        if stripped.startswith('tags:'):
            # Skip duplicate tags lines
            if tags_inserted:
                # Skip old tag items following this duplicate header
                in_tags = True
                continue
            # Write tags header + new tag items immediately
            result.append('tags:')
            for tag in new_tags:
                result.append(f"  - {tag}")
            tags_inserted = True
            # Check for inline format like tags: [a, b]
            rest = stripped[5:].strip()
            if rest.startswith('[') or (rest and not rest.startswith('-')):
                # Inline tags already handled by writing new tags above
                pass
            else:
                # Multi-line format: skip subsequent - lines
                in_tags = True
            continue
        if in_tags:
            if stripped.startswith('- '):
                # Skip old tag line
                continue
            elif not stripped:
                continue
            else:
                # End of tags block
                in_tags = False
                result.append(line)
        else:
            result.append(line)

    # If tags: was never found, append tags at the end
    if not tags_inserted and new_tags:
        result.append('tags:')
        for tag in new_tags:
            result.append(f"  - {tag}")

    return '\n'.join(result)


def build_frontmatter(platform: str, difficulty: str, tags: list, title: str = '') -> str:
    """Build frontmatter from scratch for files without it."""
    fm = f"""---
title: "{title}"
description: ""
tags:"""
    for tag in tags:
        fm += f"\n  - {tag}"
    fm += f"""
difficulty: {difficulty}
platform: {platform}
---"""
    return fm


def process_file(filepath: str, dry_run: bool = False) -> dict:
    """Process a single markdown file. Returns change description."""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    fm_str, body = parse_frontmatter(content)
    rel_path = os.path.relpath(filepath)

    if fm_str is None:
        # No frontmatter — create one
        # Try to extract title from first heading
        title_match = re.search(r'^#\s+(.+)', body)
        title = title_match.group(1).strip() if title_match else os.path.basename(filepath)
        platform = infer_platform(filepath)

        # Determine tags based on path
        tags = [platform]
        fp = filepath.replace('\\', '/')
        if '/01-led/' in fp:
            tags.extend(['peripheral', 'beginner', 'gpio'])
            difficulty = 'beginner'
        elif '/02-button/' in fp:
            tags.extend(['peripheral', 'intermediate', 'gpio'])
            difficulty = 'intermediate'
        elif '/00-env-setup/' in fp:
            tags.extend(['toolchain', 'beginner'])
            difficulty = 'beginner'
        else:
            tags.extend(['cpp-modern', 'intermediate'])
            difficulty = 'intermediate'

        new_fm = build_frontmatter(platform, difficulty, tags, title)
        new_content = new_fm + '\n' + body

        if not dry_run:
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(new_content)

        return {'file': rel_path, 'action': 'created_fm', 'tags': tags}

    # Has frontmatter
    old_tags = extract_tags_from_fm(fm_str)
    new_tags, platform, difficulty = map_tags(old_tags, filepath)

    # Update tags in frontmatter
    new_fm = replace_tags_in_fm(fm_str, new_tags)

    # Set/update platform field
    new_fm = set_fm_field(new_fm, 'platform', platform)

    # Reconstruct content
    new_content = '---\n' + new_fm + '\n---' + body

    if not dry_run:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(new_content)

    return {'file': rel_path, 'action': 'migrated', 'old_tags': old_tags, 'new_tags': new_tags}


def main():
    parser = argparse.ArgumentParser(description='Migrate frontmatter tags to standardized system')
    parser.add_argument('--dry-run', action='store_true', help='Print changes without modifying files')
    args = parser.parse_args()

    docs_dir = Path(__file__).parent.parent / 'documents'
    md_files = sorted(docs_dir.rglob('*.md'))

    # Skip non-article files
    skip_dirs = {'stylesheets', 'javascripts', 'images', 'appendix'}
    md_files = [
        f for f in md_files
        if not any(part in skip_dirs for part in f.parts)
    ]

    changes = []
    for fpath in md_files:
        try:
            result = process_file(str(fpath), dry_run=args.dry_run)
            changes.append(result)
        except Exception as e:
            print(f"ERROR processing {fpath}: {e}", file=sys.stderr)

    # Print summary
    created = [c for c in changes if c['action'] == 'created_fm']
    migrated = [c for c in changes if c['action'] == 'migrated']

    print(f"\n{'='*60}")
    print(f"Tag Migration Summary {'(DRY RUN)' if args.dry_run else ''}")
    print(f"{'='*60}")
    print(f"Total files processed: {len(changes)}")
    print(f"Frontmatter created:   {len(created)}")
    print(f"Tags migrated:         {len(migrated)}")

    if created:
        print(f"\n── Files with new frontmatter ──")
        for c in created:
            print(f"  {c['file']}")
            print(f"    tags: {c['tags']}")

    if migrated:
        print(f"\n── Files with migrated tags ──")
        for c in migrated:
            old = ', '.join(c['old_tags'][:5])
            if len(c['old_tags']) > 5:
                old += f"... (+{len(c['old_tags'])-5})"
            print(f"  {c['file']}")
            print(f"    old: [{old}]")
            print(f"    new: {c['new_tags']}")

    print()


if __name__ == '__main__':
    main()
