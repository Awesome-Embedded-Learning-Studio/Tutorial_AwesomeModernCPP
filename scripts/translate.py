#!/usr/bin/env python3
"""
AI Translation Script — local incremental translation of Chinese Markdown docs.

Translates Chinese .md files to English .en.md files using OpenAI-compatible API.
Runs entirely locally, no CI involvement.

Usage:
    python3 scripts/translate.py --changed              # Translate changed files vs main
    python3 scripts/translate.py --file <path>           # Translate single file
    python3 scripts/translate.py --all                   # Translate all (with confirmation)
    python3 scripts/translate.py --changed --dry-run     # Estimate cost only
    python3 scripts/translate.py --changed --engine anthropic  # Use Anthropic API

API key is read from:
    1. TRANSLATE_API_KEY environment variable
    2. .env file in project root
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SUPPORTED_ENGINES = {'openai', 'anthropic', 'deepl'}
MAX_TOKENS_PER_RUN = 100_000
CHARS_PER_TOKEN = 4  # rough estimate for mixed CJK/English

TRANSLATION_SYSTEM_PROMPT = """You are a professional technical translator specializing \
in C++ and embedded systems documentation.
Translate the following Chinese Markdown content to English.

Rules:
- Maintain the exact Markdown structure and formatting
- Use precise technical terminology
- Keep code blocks, URLs, and image paths completely unchanged
- Keep Mermaid diagrams unchanged
- Translate naturally, not literally
- Preserve all Markdown formatting (tables, admonitions, footnotes, etc.)
- For frontmatter: translate title and description fields only, keep all other fields
- Do not translate code, filenames, or command-line examples
- Use American English spelling conventions
"""

# ---------------------------------------------------------------------------
# Frontmatter parsing
# ---------------------------------------------------------------------------

def parse_frontmatter(content: str) -> Tuple[Dict, str, str]:
    """Parse frontmatter, returns (dict, frontmatter_str, body)."""
    match = re.match(r'^---\s*\n(.*?)\n---\s*\n(.*)', content, re.DOTALL)
    if not match:
        return {}, '', content
    try:
        import yaml
        fm = yaml.safe_load(match.group(1))
        return fm or {}, match.group(1), match.group(2)
    except ImportError:
        return {}, match.group(1), match.group(2)
    except Exception:
        return {}, match.group(1), match.group(2)


def reconstruct_frontmatter(fm_str: str, translated_fm: Dict) -> str:
    """Reconstruct frontmatter with translated fields."""
    lines = fm_str.split('\n')
    result = []
    for line in lines:
        # Translate title and description fields
        if line.startswith('title:'):
            title = translated_fm.get('title', '')
            if title:
                result.append(f'title: "{title}"')
            else:
                result.append(line)
        elif line.startswith('description:'):
            desc = translated_fm.get('description', '')
            if desc:
                result.append(f'description: "{desc}"')
            else:
                result.append(line)
        else:
            result.append(line)
    return '\n'.join(result)


# ---------------------------------------------------------------------------
# Content preservation (code blocks, mermaid, etc.)
# ---------------------------------------------------------------------------

def extract_preserved_blocks(content: str) -> Tuple[str, Dict[str, str]]:
    """Replace code blocks and mermaid with placeholders. Returns modified content and mapping."""
    placeholders = {}
    counter = 0

    def replace_block(m):
        nonlocal counter
        key = f'__PRESERVED_BLOCK_{counter}__'
        placeholders[key] = m.group(0)
        counter += 1
        return key

    # Replace fenced code blocks (```...``` and ~~~...~~~)
    modified = re.sub(r'```[\s\S]*?```', replace_block, content)
    modified = re.sub(r'~~~[\s\S]*?~~~', replace_block, modified)

    return modified, placeholders


def restore_preserved_blocks(content: str, placeholders: Dict[str, str]) -> str:
    """Restore preserved blocks from placeholders."""
    for key, value in placeholders.items():
        content = content.replace(key, value)
    return content


# ---------------------------------------------------------------------------
# API clients
# ---------------------------------------------------------------------------

class TranslationClient:
    """Base class for translation API clients."""

    def __init__(self, api_key: str, model: str = ''):
        self.api_key = api_key
        self.model = model

    def translate(self, text: str) -> str:
        raise NotImplementedError

    def count_tokens(self, text: str) -> int:
        return len(text) // CHARS_PER_TOKEN


class OpenAIClient(TranslationClient):
    def __init__(self, api_key: str, model: str = 'gpt-4o-mini', base_url: str = ''):
        super().__init__(api_key, model)
        self.base_url = base_url or os.environ.get('OPENAI_BASE_URL', 'https://api.openai.com/v1')

    def translate(self, text: str) -> str:
        import urllib.request
        import urllib.error

        url = f'{self.base_url}/chat/completions'
        payload = json.dumps({
            'model': self.model,
            'messages': [
                {'role': 'system', 'content': TRANSLATION_SYSTEM_PROMPT},
                {'role': 'user', 'content': text},
            ],
            'temperature': 0.3,
        }).encode('utf-8')

        req = urllib.request.Request(
            url,
            data=payload,
            headers={
                'Content-Type': 'application/json',
                'Authorization': f'Bearer {self.api_key}',
            },
        )

        try:
            resp = urllib.request.urlopen(req, timeout=120)
            data = json.loads(resp.read().decode('utf-8'))
            return data['choices'][0]['message']['content']
        except urllib.error.HTTPError as e:
            body = e.read().decode('utf-8', errors='replace')
            raise RuntimeError(f"API error {e.code}: {body}")
        except KeyError:
            raise RuntimeError(f"Unexpected API response: {data}")


class AnthropicClient(TranslationClient):
    def __init__(self, api_key: str, model: str = 'claude-sonnet-4-20250514',
                 base_url: str = ''):
        super().__init__(api_key, model)
        self.base_url = (base_url or
                         os.environ.get('ANTHROPIC_BASE_URL', 'https://api.anthropic.com'))

    def translate(self, text: str) -> str:
        import urllib.request
        import urllib.error

        url = f'{self.base_url}/v1/messages'
        payload = json.dumps({
            'model': self.model,
            'max_tokens': 8192,
            'system': TRANSLATION_SYSTEM_PROMPT,
            'messages': [
                {'role': 'user', 'content': text},
            ],
            'temperature': 0.3,
        }).encode('utf-8')

        req = urllib.request.Request(
            url,
            data=payload,
            headers={
                'Content-Type': 'application/json',
                'x-api-key': self.api_key,
                'anthropic-version': '2023-06-01',
            },
        )

        try:
            resp = urllib.request.urlopen(req, timeout=120)
            data = json.loads(resp.read().decode('utf-8'))
            return data['content'][0]['text']
        except urllib.error.HTTPError as e:
            body = e.read().decode('utf-8', errors='replace')
            raise RuntimeError(f"API error {e.code}: {body}")
        except (KeyError, IndexError):
            raise RuntimeError(f"Unexpected API response: {data}")


class DeepLClient(TranslationClient):
    def __init__(self, api_key: str):
        super().__init__(api_key)

    def translate(self, text: str) -> str:
        import urllib.request
        import urllib.error

        url = 'https://api-free.deepl.com/v2/translate'
        payload = urllib.parse.urlencode({
            'auth_key': self.api_key,
            'text': text,
            'source_lang': 'ZH',
            'target_lang': 'EN',
        }).encode('utf-8')

        req = urllib.request.Request(url, data=payload)
        try:
            resp = urllib.request.urlopen(req, timeout=120)
            data = json.loads(resp.read().decode('utf-8'))
            return data['translations'][0]['text']
        except urllib.error.HTTPError as e:
            body = e.read().decode('utf-8', errors='replace')
            raise RuntimeError(f"API error {e.code}: {body}")


def create_client(engine: str, api_key: str) -> TranslationClient:
    if engine == 'openai':
        return OpenAIClient(api_key)
    elif engine == 'anthropic':
        model = os.environ.get('ANTHROPIC_DEFAULT_SONNET_MODEL', 'claude-sonnet-4-20250514')
        return AnthropicClient(api_key, model=model)
    elif engine == 'deepl':
        return DeepLClient(api_key)
    else:
        raise ValueError(f"Unsupported engine: {engine}")


# ---------------------------------------------------------------------------
# Translation pipeline
# ---------------------------------------------------------------------------

class TranslationPipeline:
    def __init__(self, client: TranslationClient, dry_run: bool = False,
                 max_tokens: int = MAX_TOKENS_PER_RUN):
        self.client = client
        self.dry_run = dry_run
        self.max_tokens = max_tokens
        self.tokens_used = 0
        self.files_translated = 0

    def translate_file(self, input_path: Path) -> Optional[str]:
        """Translate a single file. Returns translated content or None."""
        try:
            content = input_path.read_text(encoding='utf-8')
        except Exception as e:
            print(f"  Error reading {input_path}: {e}")
            return None

        # Skip non-Chinese files
        chinese_chars = len(re.findall(r'[\u4e00-\u9fff]', content))
        if chinese_chars < 5:
            print(f"  Skipping {input_path.name}: insufficient Chinese content")
            return None

        # Estimate tokens
        estimated_tokens = self.client.count_tokens(content)
        if self.tokens_used + estimated_tokens > self.max_tokens:
            print(f"  Skipping {input_path.name}: would exceed token limit "
                  f"({self.tokens_used + estimated_tokens} > {self.max_tokens})")
            return None

        if self.dry_run:
            cost = self._estimate_cost(estimated_tokens)
            print(f"  [DRY RUN] {input_path.name}: ~{estimated_tokens} tokens, ~${cost:.4f}")
            self.tokens_used += estimated_tokens
            return None

        # Parse frontmatter
        fm, fm_str, body = parse_frontmatter(content)

        # Extract preserved blocks (code, mermaid)
        body_modified, placeholders = extract_preserved_blocks(body)

        # Translate body
        print(f"  Translating {input_path.name}...", end=' ', flush=True)
        try:
            translated_body = self.client.translate(body_modified)
        except Exception as e:
            print(f"FAILED: {e}")
            return None

        # Restore preserved blocks
        translated_body = restore_preserved_blocks(translated_body, placeholders)

        # Translate frontmatter title and description
        translated_fm = {}
        if fm:
            title = fm.get('title', '')
            if title and re.search(r'[\u4e00-\u9fff]', title):
                try:
                    translated_fm['title'] = self.client.translate(
                        f"Translate this title to English: {title}"
                    ).strip().strip('"')
                except Exception:
                    translated_fm['title'] = title
            desc = fm.get('description', '')
            if desc and re.search(r'[\u4e00-\u9fff]', desc):
                try:
                    translated_fm['description'] = self.client.translate(
                        f"Translate this description to English: {desc}"
                    ).strip().strip('"')
                except Exception:
                    translated_fm['description'] = desc

        # Reconstruct
        if fm_str:
            new_fm = reconstruct_frontmatter(fm_str, translated_fm)
            result = f'---\n{new_fm}\n---\n{translated_body}'
        else:
            result = translated_body

        self.tokens_used += estimated_tokens
        self.files_translated += 1
        print("OK")
        return result

    def _estimate_cost(self, tokens: int) -> float:
        # Rough estimates per 1K tokens
        return tokens / 1000 * 0.01  # ~$0.01 per 1K tokens


# ---------------------------------------------------------------------------
# Change detection
# ---------------------------------------------------------------------------

def detect_changed_files(project_root: Path) -> List[Path]:
    """Detect changed .md files relative to main branch."""
    try:
        result = subprocess.run(
            ['git', 'diff', '--name-only', '--diff-filter=ACM', 'main...HEAD',
             '--', 'documents/**/*.md'],
            cwd=str(project_root),
            capture_output=True,
            text=True,
        )
        files = []
        for line in result.stdout.strip().split('\n'):
            if not line:
                continue
            path = project_root / line
            # Skip .en.md files
            if path.name.endswith('.en.md'):
                continue
            if path.exists() and path.suffix == '.md':
                files.append(path)
        return files
    except Exception as e:
        print(f"Warning: Could not detect changed files: {e}")
        return []


def find_all_chinese_md(docs_root: Path) -> List[Path]:
    """Find all Chinese .md files (excluding .en.md)."""
    files = []
    for f in docs_root.rglob('*.md'):
        if f.name.endswith('.en.md') or f.name in ('index.md', 'tags.md'):
            continue
        if any(part in ('images', 'generated', 'hooks', 'stylesheets', 'javascripts')
               for part in f.parts):
            continue
        files.append(f)
    return sorted(files)


def get_output_path(input_path: Path) -> Path:
    """Compute .en.md output path for a given .md file."""
    return input_path.with_suffix('.en.md')


# ---------------------------------------------------------------------------
# API Key loading
# ---------------------------------------------------------------------------

def load_api_key(engine: str) -> Optional[str]:
    """Load API key from environment or .env file.

    Reuses existing project variables:
    - ANTHROPIC_AUTH_TOKEN (for Anthropic / Claude API)
    - OPENAI_API_KEY (for OpenAI)
    Falls back to TRANSLATE_API_KEY as a generic key.
    """
    # Engine-specific env vars (matching existing project conventions)
    engine_vars = {
        'anthropic': ['ANTHROPIC_AUTH_TOKEN'],
        'openai': ['OPENAI_API_KEY'],
        'deepl': ['DEEPL_API_KEY'],
    }
    fallback_vars = ['TRANSLATE_API_KEY']

    # Try environment variables
    for var in engine_vars.get(engine, []) + fallback_vars:
        key = os.environ.get(var)
        if key:
            return key

    # Try .env file
    env_file = Path(__file__).parent.parent / '.env'
    if env_file.exists():
        for line in env_file.read_text(encoding='utf-8').split('\n'):
            line = line.strip()
            if line.startswith('#') or '=' not in line:
                continue
            k, v = line.split('=', 1)
            k = k.strip()
            v = v.strip().strip('"').strip("'")
            for var in engine_vars.get(engine, []) + fallback_vars:
                if k == var:
                    return v

    return None


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='Translate Chinese Markdown docs to English')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--changed', action='store_true',
                       help='Translate files changed relative to main')
    group.add_argument('--file', type=str,
                       help='Translate a single file')
    group.add_argument('--all', action='store_true',
                       help='Translate all Chinese .md files')
    parser.add_argument('--engine', choices=SUPPORTED_ENGINES,
                        default='openai', help='Translation engine (default: openai)')
    parser.add_argument('--dry-run', action='store_true',
                        help='Estimate tokens and cost without translating')
    parser.add_argument('--max-tokens', type=int, default=MAX_TOKENS_PER_RUN,
                        help=f'Max tokens per run (default: {MAX_TOKENS_PER_RUN})')
    args = parser.parse_args()

    project_root = Path(__file__).parent.parent
    docs_root = project_root / 'documents'

    # Collect files to translate
    if args.file:
        target = Path(args.file)
        if not target.is_absolute():
            target = project_root / target
        if not target.exists():
            print(f"Error: File not found: {target}")
            sys.exit(1)
        files = [target]
    elif args.changed:
        files = detect_changed_files(project_root)
        if not files:
            print("No changed Chinese .md files detected.")
            sys.exit(0)
        print(f"Detected {len(files)} changed file(s):")
        for f in files:
            print(f"  {f.relative_to(project_root)}")
        print()
    elif args.all:
        files = find_all_chinese_md(docs_root)
        if not files:
            print("No Chinese .md files found.")
            sys.exit(0)
        print(f"Found {len(files)} Chinese .md files.")
        if not args.dry_run:
            confirm = input("Translate all? [y/N] ").strip().lower()
            if confirm != 'y':
                print("Aborted.")
                sys.exit(0)
        print()

    # Load API key
    if not args.dry_run:
        api_key = load_api_key(args.engine)
        if not api_key:
            print(f"Error: No API key found for {args.engine}.")
            print()
            print("Set one of:")
            if args.engine == 'anthropic':
                print("  export ANTHROPIC_AUTH_TOKEN='your-key-here'")
            elif args.engine == 'openai':
                print("  export OPENAI_API_KEY='your-key-here'")
            print("  export TRANSLATE_API_KEY='your-key-here'")
            print(f"  Or add to .env (see .env.example for reference)")
            sys.exit(1)
        client = create_client(args.engine, api_key)
    else:
        client = create_client(args.engine, 'dummy')

    pipeline = TranslationPipeline(client, dry_run=args.dry_run,
                                   max_tokens=args.max_tokens)

    # Translate files
    success_count = 0
    fail_count = 0
    for filepath in files:
        rel = filepath.relative_to(project_root)
        output_path = get_output_path(filepath)

        if args.dry_run:
            pipeline.translate_file(filepath)
            continue

        result = pipeline.translate_file(filepath)
        if result is not None:
            try:
                output_path.write_text(result, encoding='utf-8')
                print(f"  Written: {output_path.relative_to(project_root)}")
                success_count += 1
            except Exception as e:
                print(f"  Error writing {output_path}: {e}")
                fail_count += 1
        else:
            fail_count += 1

    # Summary
    print()
    print("=" * 50)
    if args.dry_run:
        print(f"Estimated tokens: {pipeline.tokens_used:,}")
        cost = pipeline._estimate_cost(pipeline.tokens_used)
        print(f"Estimated cost:   ~${cost:.2f}")
        print(f"Files:            {len(files)}")
    else:
        print(f"Files translated: {success_count}")
        print(f"Files failed:     {fail_count}")
        print(f"Tokens used:      {pipeline.tokens_used:,}")
    print("=" * 50)

    sys.exit(1 if fail_count > 0 and not args.dry_run else 0)


if __name__ == '__main__':
    main()
