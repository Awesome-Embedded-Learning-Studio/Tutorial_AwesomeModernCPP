#!/usr/bin/env python3
"""
Fix missing frontmatter in translated files.

Reads frontmatter from the Chinese source file and injects it into the
corresponding English translation, translating only title and description.
Body content is left untouched.

Usage:
    python3 scripts/fix_frontmatter.py              # Fix all missing frontmatter
    python3 scripts/fix_frontmatter.py --dry-run    # Show what would be fixed
"""

import argparse
import json
import os
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

try:
    import yaml
except ImportError:
    print('ERROR: pyyaml is not installed.', file=sys.stderr)
    print('  source .venv/bin/activate && pip install pyyaml', file=sys.stderr)
    sys.exit(1)

try:
    import urllib.request
    import urllib.error
except ImportError:
    pass

PROJECT_ROOT = Path(__file__).parent.parent
DOCS_DIR = PROJECT_ROOT / 'documents'
EN_DIR = PROJECT_ROOT / 'documents' / 'en'

SUPPORTED_ENGINES = {'anthropic', 'openai'}


def load_api_key(engine: str) -> str:
    """Load API key from environment or .env file."""
    engine_vars = {
        'anthropic': ['ANTHROPIC_AUTH_TOKEN'],
        'openai': ['OPENAI_API_KEY'],
    }
    for var in engine_vars.get(engine, []) + ['TRANSLATE_API_KEY']:
        key = os.environ.get(var)
        if key:
            return key

    env_file = PROJECT_ROOT / '.env'
    if env_file.exists():
        for line in env_file.read_text(encoding='utf-8').split('\n'):
            line = line.strip()
            if line.startswith('#') or '=' not in line:
                continue
            k, v = line.split('=', 1)
            k = k.strip()
            v = v.strip().strip('"').strip("'")
            for var in engine_vars.get(engine, []) + ['TRANSLATE_API_KEY']:
                if k == var:
                    return v
    return None


def translate_text(text: str, api_key: str, engine: str) -> str:
    """Translate a short text using the API."""
    base_url = os.environ.get('ANTHROPIC_BASE_URL', 'https://api.anthropic.com') \
        if engine == 'anthropic' else os.environ.get('OPENAI_BASE_URL', 'https://api.openai.com/v1')
    model = os.environ.get('ANTHROPIC_DEFAULT_SONNET_MODEL', 'claude-sonnet-4-20250514') \
        if engine == 'anthropic' else 'gpt-4o-mini'

    prompt = f'Translate this to English. Output ONLY the translation, nothing else: {text}'

    if engine == 'anthropic':
        payload = json.dumps({
            'model': model,
            'max_tokens': 512,
            'messages': [{'role': 'user', 'content': prompt}],
            'temperature': 0.3,
        }).encode('utf-8')
        req = urllib.request.Request(
            f'{base_url}/v1/messages', data=payload,
            headers={
                'Content-Type': 'application/json',
                'x-api-key': api_key,
                'anthropic-version': '2023-06-01',
            })
    else:
        payload = json.dumps({
            'model': model,
            'messages': [{'role': 'user', 'content': prompt}],
            'temperature': 0.3,
        }).encode('utf-8')
        req = urllib.request.Request(
            f'{base_url}/chat/completions', data=payload,
            headers={
                'Content-Type': 'application/json',
                'Authorization': f'Bearer {api_key}',
            })

    resp = urllib.request.urlopen(req, timeout=60)
    data = json.loads(resp.read().decode('utf-8'))
    if engine == 'anthropic':
        return data['content'][0]['text'].strip().strip('"')
    else:
        return data['choices'][0]['message']['content'].strip().strip('"')


def parse_frontmatter(content: str):
    """Parse frontmatter. Returns (dict, frontmatter_str, body)."""
    match = re.match(r'^---\s*\n(.*?)\n---\s*\n(.*)', content, re.DOTALL)
    if not match:
        return {}, '', content
    try:
        fm = yaml.safe_load(match.group(1))
        return fm or {}, match.group(1), match.group(2)
    except Exception:
        return {}, match.group(1), match.group(2)


def find_source_for(en_path: Path) -> Path:
    """Find the Chinese source file for an English translation."""
    rel = en_path.relative_to(EN_DIR)
    return DOCS_DIR / rel


def main():
    parser = argparse.ArgumentParser(description='Fix missing frontmatter in translated files')
    parser.add_argument('--dry-run', action='store_true', help='Show what would be fixed')
    parser.add_argument('--engine', choices=SUPPORTED_ENGINES, default='anthropic')
    parser.add_argument('-y', '--yes', action='store_true', help='Skip confirmation')
    args = parser.parse_args()

    # Find all en/ .md files missing frontmatter
    missing = []
    for f in sorted(EN_DIR.rglob('*.md')):
        content = f.read_text(encoding='utf-8')
        fm, _, _ = parse_frontmatter(content)
        if not fm:
            missing.append(f)

    if not missing:
        print('All translated files have frontmatter. Nothing to fix.')
        return

    print(f'Found {len(missing)} files with missing frontmatter.')
    if not args.dry_run and not args.yes:
        confirm = input('Fix all? [y/N] ').strip().lower()
        if confirm != 'y':
            print('Aborted.')
            return

    # Load API key for title/description translation
    api_key = None
    if not args.dry_run:
        api_key = load_api_key(args.engine)
        if not api_key:
            print(f'ERROR: No API key for {args.engine}.', file=sys.stderr)
            sys.exit(1)

    fixed = 0
    skipped = 0
    for en_path in missing:
        src_path = find_source_for(en_path)
        if not src_path.exists():
            print(f'  SKIP {en_path.relative_to(PROJECT_ROOT)}: source not found')
            skipped += 1
            continue

        src_content = src_path.read_text(encoding='utf-8')
        src_fm, _, _ = parse_frontmatter(src_content)
        if not src_fm:
            print(f'  SKIP {en_path.relative_to(PROJECT_ROOT)}: source also has no frontmatter')
            skipped += 1
            continue

        en_content = en_path.read_text(encoding='utf-8')
        _, _, en_body = parse_frontmatter(en_content)

        if args.dry_run:
            print(f'  [DRY RUN] {en_path.relative_to(PROJECT_ROOT)}')
            fixed += 1
            continue

        # Translate title and description if they contain Chinese
        merged_fm = dict(src_fm)
        for field in ('title', 'description'):
            val = merged_fm.get(field, '')
            if val and re.search(r'[\u4e00-\u9fff]', val):
                try:
                    merged_fm[field] = translate_text(val, api_key, args.engine)
                except Exception as e:
                    print(f'  WARN {en_path.name}: failed to translate {field}: {e}')
                    # Keep the Chinese value as fallback

        # Build output
        fm_str = yaml.dump(merged_fm, allow_unicode=True, sort_keys=False,
                           default_flow_style=False).strip()
        new_content = f'---\n{fm_str}\n---\n{en_body}'
        en_path.write_text(new_content, encoding='utf-8')
        print(f'  OK {en_path.relative_to(PROJECT_ROOT)}')
        fixed += 1

    print(f'\nFixed: {fixed}, Skipped: {skipped}')


if __name__ == '__main__':
    main()
