#!/usr/bin/env python3
"""
AI Translation Script — local incremental translation of Chinese Markdown docs.

Translates Chinese .md files to English .en.md files using OpenAI/Anthropic API.
Runs entirely locally, no CI involvement.

Usage:
    python3 scripts/translate.py --changed              # Translate changed files vs main
    python3 scripts/translate.py --file <path>           # Translate single file
    python3 scripts/translate.py --all                   # Translate all (with confirmation)
    python3 scripts/translate.py --changed --dry-run     # Estimate cost only
    python3 scripts/translate.py --changed --engine anthropic  # Use Anthropic API
    python3 scripts/translate.py --changed --force       # Force re-translation
    python3 scripts/translate.py --all --batch-size 20   # Translate max 20 files

API key is read from:
    1. Environment variables (ANTHROPIC_AUTH_TOKEN / OPENAI_API_KEY / DEEPL_API_KEY)
    2. TRANSLATE_API_KEY generic fallback
    3. .env file in project root
"""

import argparse
import hashlib
import json
import logging
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Dict, List, Optional, Tuple

# ---------------------------------------------------------------------------
# Logging setup
# ---------------------------------------------------------------------------

logger = logging.getLogger('translate')


def setup_logging(cache_dir: Path, verbose: bool = False) -> None:
    """Configure dual-output logging: console (stderr) + file."""
    level = logging.DEBUG if verbose else logging.INFO
    logger.setLevel(logging.DEBUG)

    # Console handler — stderr so it doesn't interfere with piping
    console = logging.StreamHandler(sys.stderr)
    console.setLevel(level)
    console.setFormatter(logging.Formatter('%(levelname)s %(message)s'))
    logger.addHandler(console)

    # File handler
    cache_dir.mkdir(parents=True, exist_ok=True)
    fh = logging.FileHandler(cache_dir / 'translate.log', encoding='utf-8')
    fh.setLevel(logging.DEBUG)
    fh.setFormatter(logging.Formatter(
        '%(asctime)s %(levelname)s %(name)s %(message)s'))
    logger.addHandler(fh)


# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SUPPORTED_ENGINES = {'openai', 'anthropic', 'deepl'}
MAX_TOKENS_PER_RUN = 100_000
CHARS_PER_TOKEN = 4  # rough estimate for mixed CJK/English
MAX_SINGLE_FILE_CHARS = 60_000  # ~15K tokens; split files larger than this

# ---------------------------------------------------------------------------
# Terminology Glossary
# ---------------------------------------------------------------------------

glossary_cache: Optional[Dict[str, str]] = None


def load_terminology_glossary(project_root: Path) -> Dict[str, str]:
    """Parse terminology.md and return {Chinese: English} mapping."""
    global glossary_cache
    if glossary_cache is not None:
        return glossary_cache

    glossary_path = project_root / 'documents' / 'appendix' / 'terminology.md'
    if not glossary_path.exists():
        logger.warning('Terminology glossary not found: %s', glossary_path)
        glossary_cache = {}
        return glossary_cache

    terms: Dict[str, str] = {}
    content = glossary_path.read_text(encoding='utf-8')
    # Parse markdown table rows: | English | 中文 | 备注 |
    for line in content.split('\n'):
        line = line.strip()
        if not line.startswith('|') or line.startswith('|-') or line.startswith('| -'):
            continue
        cells = [c.strip() for c in line.split('|')]
        # Filter empty strings from leading/trailing |
        cells = [c for c in cells if c]
        if len(cells) < 2:
            continue
        en_term = cells[0].strip()
        zh_term = cells[1].strip()
        # Skip header row and separator
        if en_term.lower() == 'english' or set(en_term) <= {'-'}:
            continue
        if zh_term and en_term and re.search(r'[\u4e00-\u9fff]', zh_term):
            terms[zh_term] = en_term

    glossary_cache = terms
    logger.debug('Loaded %d terminology entries', len(terms))
    return terms


def format_terminology_for_prompt(terms: Dict[str, str]) -> str:
    """Format terminology dict as a section for injection into system prompt."""
    if not terms:
        return ''
    lines = ['\n### Terminology Reference', '',
             'Use these established translations consistently:',
             '']
    lines.append('| Chinese | English |')
    lines.append('|---------|---------|')
    for zh, en in sorted(terms.items(), key=lambda x: len(x[0]), reverse=True):
        lines.append(f'| {zh} | {en} |')
    lines.append('')
    return '\n'.join(lines)


def build_system_prompt(project_root: Path) -> str:
    """Build the full system prompt with terminology injection."""
    base_prompt = """You are a professional technical translator specializing in \
modern C++ and embedded systems development. Your task is to translate Chinese \
Markdown documentation into natural, idiomatic English.

## Context
This is a tutorial project teaching modern C++ (C++11 through C++23) for \
embedded systems (STM32, ESP32, RP2040). The content ranges from C++ language \
features to hardware peripheral programming, RTOS concepts, and build systems.

## Translation Rules

### General
- Translate naturally, not literally. Prioritize readability for English-speaking developers.
- Maintain the exact Markdown structure and formatting.
- Use precise technical terminology consistently.

### Content Preservation
- Do NOT translate code inside fenced code blocks (``` or ~~~).
- Do NOT translate inline code (`like this`).
- Translate code comments only if they contain Chinese text.
- Preserve the language specifier in fenced blocks (```cpp, ```python, etc.).
- Do NOT translate Mermaid diagram code blocks — keep them exactly as they are.
- Do NOT translate LaTeX math expressions ($...$ and $$...$$).
- When a fenced code block represents command output (not source code), use ```text as the language specifier instead of bare ```.

### Tables
- Translate cell content, preserve table structure.
- Keep numeric values unchanged.

### Images and Links
- In ![alt text](url), translate alt text but keep the URL unchanged.
- In [link text](url), translate link text but keep the URL unchanged.

### Admonitions
- Translate admonition content.
- Keep admonition types (note, warning, tip, etc.) unchanged.

### Frontmatter (YAML between ---)
- Translate ONLY the 'title' and 'description' fields.
- Keep all other fields (date, tags, difficulty, etc.) unchanged.
- The 'translation' metadata block must be kept as-is.

### Style Guide
- Use active voice.
- Use "we" instead of "you" for a collaborative, hands-on tone.
- Keep sentences concise.
- Use serial comma (Oxford comma).
- Use American English spelling conventions.
- Spell out numbers below 10 (one, two, three).
"""

    terms = load_terminology_glossary(project_root)
    terminology_section = format_terminology_for_prompt(terms)
    return base_prompt + terminology_section


# ---------------------------------------------------------------------------
# Content Preprocessor — preserves code/mermaid/math/image URLs
# ---------------------------------------------------------------------------

class ContentPreprocessor:
    """Extract content that should not be translated and replace with placeholders."""

    def extract_all(self, content: str) -> Tuple[str, Dict[str, str]]:
        """Replace all preserved blocks with placeholders.

        Returns (modified_content, placeholder_dict).
        """
        placeholders: Dict[str, str] = {}
        counter = 0
        modified = content

        def _replace(m: re.Match) -> str:
            nonlocal counter
            key = f'__PRESERVED_{counter}__'
            placeholders[key] = m.group(0)
            counter += 1
            return key

        # 1. Fenced code blocks (``` and ~~~)
        modified = re.sub(r'(`{3,}|~{3,})[\s\S]*?\1', _replace, modified)

        # 2. Display math $$...$$
        modified = re.sub(r'\$\$[\s\S]*?\$\$', _replace, modified)

        # 3. Inline math $...$  (not preceded/followed by $ or digit)
        modified = re.sub(r'(?<!\$)\$(?!\$)(.+?)(?<!\$)\$(?!\$)', _replace, modified)

        # 4. Inline code `...` (single/multiple backticks, not inside preserved)
        modified = re.sub(r'(?<!`)(`+)(?!`)(.+?)\1', _replace, modified)

        return modified, placeholders

    def restore_all(self, content: str, placeholders: Dict[str, str]) -> str:
        """Restore preserved blocks from placeholders."""
        for key, value in placeholders.items():
            content = content.replace(key, value)
        return content

    @staticmethod
    def count_preserved(placeholders: Dict[str, str]) -> int:
        return len(placeholders)


# ---------------------------------------------------------------------------
# Translation Metadata
# ---------------------------------------------------------------------------

@dataclass
class TranslationMetadata:
    source: str = ''
    source_hash: str = ''
    translated_at: str = ''
    engine: str = ''
    token_count: int = 0

    def to_dict(self) -> Dict:
        return {
            'source': self.source,
            'source_hash': self.source_hash,
            'translated_at': self.translated_at,
            'engine': self.engine,
            'token_count': self.token_count,
        }


# ---------------------------------------------------------------------------
# Frontmatter parsing & reconstruction
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
    except Exception:
        return {}, match.group(1), match.group(2)


def compute_source_hash(content: str) -> str:
    """Compute SHA-256 hash of file content."""
    return hashlib.sha256(content.encode('utf-8')).hexdigest()


def reconstruct_frontmatter(fm: Dict, translated_fm: Dict,
                            metadata: Optional[TranslationMetadata] = None) -> str:
    """Reconstruct frontmatter with translated fields and metadata.

    Uses YAML serialization for robustness.
    """
    import yaml

    merged = dict(fm)
    # Overlay translated fields
    if 'title' in translated_fm:
        merged['title'] = translated_fm['title']
    if 'description' in translated_fm:
        merged['description'] = translated_fm['description']
    # Embed translation metadata
    if metadata:
        merged['translation'] = metadata.to_dict()

    return yaml.dump(merged, allow_unicode=True, sort_keys=False,
                     default_flow_style=False).strip()


# ---------------------------------------------------------------------------
# Translation Manifest — content-hash based incremental detection
# ---------------------------------------------------------------------------

class TranslationManifest:
    """Manages .cache/translations/manifest.json for incremental translation."""

    def __init__(self, cache_dir: Path):
        self.cache_dir = cache_dir
        self.manifest_path = cache_dir / 'manifest.json'
        self.entries: Dict[str, Dict] = {}

    def load(self) -> None:
        """Load manifest from disk."""
        if self.manifest_path.exists():
            try:
                self.entries = json.loads(
                    self.manifest_path.read_text(encoding='utf-8'))
                logger.debug('Loaded manifest with %d entries', len(self.entries))
            except (json.JSONDecodeError, OSError) as e:
                logger.warning('Failed to load manifest: %s; starting fresh', e)
                self.entries = {}
        else:
            self.entries = {}

    def save(self) -> None:
        """Persist manifest to disk."""
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        self.manifest_path.write_text(
            json.dumps(self.entries, indent=2, ensure_ascii=False),
            encoding='utf-8')

    def is_up_to_date(self, rel_path: str, current_hash: str) -> bool:
        """Check if translation exists and source hash matches."""
        entry = self.entries.get(rel_path)
        if not entry:
            return False
        if entry.get('source_hash') != current_hash:
            return False
        return True

    def record(self, rel_path: str, source_hash: str,
               engine: str, token_count: int) -> None:
        """Record a successful translation."""
        self.entries[rel_path] = {
            'source_hash': source_hash,
            'translated_at': datetime.now(timezone.utc).isoformat(),
            'engine': engine,
            'token_count': token_count,
        }

    def remove(self, rel_path: str) -> None:
        """Remove an entry from the manifest."""
        self.entries.pop(rel_path, None)


# ---------------------------------------------------------------------------
# API clients
# ---------------------------------------------------------------------------

class TranslationClient:
    """Base class for translation API clients."""

    def __init__(self, api_key: str, model: str = ''):
        self.api_key = api_key
        self.model = model

    def translate(self, text: str, system_prompt: str = '') -> str:
        raise NotImplementedError

    def count_tokens(self, text: str) -> int:
        return len(text) // CHARS_PER_TOKEN


class OpenAIClient(TranslationClient):
    def __init__(self, api_key: str, model: str = 'gpt-4o-mini', base_url: str = ''):
        super().__init__(api_key, model)
        self.base_url = base_url or os.environ.get(
            'OPENAI_BASE_URL', 'https://api.openai.com/v1')

    def translate(self, text: str, system_prompt: str = '') -> str:
        import urllib.request
        import urllib.error

        url = f'{self.base_url}/chat/completions'
        payload = json.dumps({
            'model': self.model,
            'messages': [
                {'role': 'system', 'content': system_prompt},
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
            raise RuntimeError(f'API error {e.code}: {body}')
        except KeyError:
            raise RuntimeError(f'Unexpected API response: {data}')


class AnthropicClient(TranslationClient):
    def __init__(self, api_key: str,
                 model: str = 'claude-sonnet-4-20250514',
                 base_url: str = ''):
        super().__init__(api_key, model)
        self.base_url = (base_url or
                         os.environ.get('ANTHROPIC_BASE_URL',
                                        'https://api.anthropic.com'))

    def translate(self, text: str, system_prompt: str = '') -> str:
        import urllib.request
        import urllib.error

        url = f'{self.base_url}/v1/messages'
        payload = json.dumps({
            'model': self.model,
            'max_tokens': 8192,
            'system': system_prompt,
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
            resp = urllib.request.urlopen(req, timeout=300)
            data = json.loads(resp.read().decode('utf-8'))
            return data['content'][0]['text']
        except urllib.error.HTTPError as e:
            body = e.read().decode('utf-8', errors='replace')
            raise RuntimeError(f'API error {e.code}: {body}')
        except (KeyError, IndexError):
            raise RuntimeError(f'Unexpected API response: {data}')


class DeepLClient(TranslationClient):
    def __init__(self, api_key: str):
        super().__init__(api_key)

    def translate(self, text: str, system_prompt: str = '') -> str:
        import urllib.parse
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
            raise RuntimeError(f'API error {e.code}: {body}')


def create_client(engine: str, api_key: str) -> TranslationClient:
    if engine == 'openai':
        return OpenAIClient(api_key)
    elif engine == 'anthropic':
        model = os.environ.get('ANTHROPIC_DEFAULT_SONNET_MODEL',
                               'claude-sonnet-4-20250514')
        return AnthropicClient(api_key, model=model)
    elif engine == 'deepl':
        return DeepLClient(api_key)
    else:
        raise ValueError(f'Unsupported engine: {engine}')


# ---------------------------------------------------------------------------
# Large file splitting
# ---------------------------------------------------------------------------

def split_body_by_headings(body: str, max_chars: int = MAX_SINGLE_FILE_CHARS
                           ) -> List[str]:
    """Split body text at ## heading boundaries to keep each chunk under max_chars.

    Returns a list of body chunks. Each chunk starts with its heading.
    If the body fits in one chunk, returns [body].
    """
    if len(body) <= max_chars:
        return [body]

    # Split at ## headings (but not # which is the document title)
    parts = re.split(r'(?=^## )', body, flags=re.MULTILINE)

    # Reassemble into chunks that fit within max_chars
    chunks: List[str] = []
    current = ''
    for part in parts:
        if not current:
            current = part
        elif len(current) + len(part) <= max_chars:
            current += part
        else:
            if current.strip():
                chunks.append(current)
            current = part
    if current.strip():
        chunks.append(current)

    if len(chunks) > 1:
        logger.debug('Split into %d chunks (max %d chars each)',
                     len(chunks), max_chars)
    return chunks if chunks else [body]


# ---------------------------------------------------------------------------
# Translation Pipeline
# ---------------------------------------------------------------------------

class TranslationPipeline:
    def __init__(self, client: TranslationClient,
                 manifest: TranslationManifest,
                 preprocessor: ContentPreprocessor,
                 system_prompt: str,
                 engine_name: str,
                 dry_run: bool = False,
                 force: bool = False,
                 max_tokens: int = MAX_TOKENS_PER_RUN):
        self.client = client
        self.manifest = manifest
        self.preprocessor = preprocessor
        self.system_prompt = system_prompt
        self.engine_name = engine_name
        self.dry_run = dry_run
        self.force = force
        self.max_tokens = max_tokens
        self.tokens_used = 0
        self.files_translated = 0
        self.files_skipped = 0
        self.files_failed = 0

    def should_translate(self, rel_path: str, source_hash: str) -> bool:
        """Check if file needs translation based on content hash."""
        if self.force:
            return True
        if self.manifest.is_up_to_date(rel_path, source_hash):
            logger.info('SKIP %s: source hash unchanged (%s)', rel_path,
                        source_hash[:12])
            self.files_skipped += 1
            return False
        return True

    def translate_file(self, input_path: Path,
                       project_root: Path) -> Optional[str]:
        """Translate a single file. Returns translated content or None."""
        rel_path = str(input_path.relative_to(project_root))

        try:
            content = input_path.read_text(encoding='utf-8')
        except Exception as e:
            logger.error('Error reading %s: %s', rel_path, e)
            self.files_failed += 1
            return None

        # Compute source hash
        source_hash = compute_source_hash(content)

        # Check incremental — skip if unchanged
        if not self.should_translate(rel_path, source_hash):
            return None

        # Skip non-Chinese files
        chinese_chars = len(re.findall(r'[\u4e00-\u9fff]', content))
        if chinese_chars < 5:
            logger.info('SKIP %s: insufficient Chinese content (%d CJK chars)',
                        rel_path, chinese_chars)
            self.files_skipped += 1
            return None

        # Estimate tokens
        estimated_tokens = self.client.count_tokens(content)
        if self.tokens_used + estimated_tokens > self.max_tokens:
            logger.info(
                'SKIP %s: would exceed token limit (%d + %d > %d)',
                rel_path, self.tokens_used, estimated_tokens, self.max_tokens)
            self.files_skipped += 1
            return None

        if self.dry_run:
            cost = self._estimate_cost(estimated_tokens)
            logger.info('[DRY RUN] %s: ~%d tokens, ~$%.4f',
                        rel_path, estimated_tokens, cost)
            self.tokens_used += estimated_tokens
            return None

        # Parse frontmatter
        fm, fm_str, body = parse_frontmatter(content)

        # Extract preserved blocks (code, mermaid, math, inline code)
        body_modified, placeholders = self.preprocessor.extract_all(body)

        # Split large files into chunks at ## heading boundaries
        chunks = split_body_by_headings(body_modified)
        if len(chunks) > 1:
            logger.info('Translating %s in %d chunks...', rel_path, len(chunks))

        # Translate body (possibly chunked)
        translated_parts: List[str] = []
        for i, chunk in enumerate(chunks):
            if len(chunks) > 1:
                logger.info('  Chunk %d/%d (%d chars)...',
                            i + 1, len(chunks), len(chunk))
            try:
                translated = self.client.translate(chunk, self.system_prompt)
                translated_parts.append(translated)
            except Exception as e:
                logger.error('FAILED %s chunk %d/%d: %s',
                             rel_path, i + 1, len(chunks), e)
                self.files_failed += 1
                return None

        translated_body = '\n'.join(translated_parts)

        # Restore preserved blocks
        translated_body = self.preprocessor.restore_all(
            translated_body, placeholders)

        # Validate no leftover placeholders
        leftover = re.findall(r'__PRESERVED_\d+__', translated_body)
        if leftover:
            logger.warning(
                '%s: %d leftover placeholders in translation output',
                rel_path, len(leftover))

        # Translate frontmatter title and description
        translated_fm: Dict[str, str] = {}
        if fm:
            title = fm.get('title', '')
            if title and re.search(r'[\u4e00-\u9fff]', title):
                try:
                    translated_fm['title'] = self.client.translate(
                        f'Translate this title to English: {title}',
                        self.system_prompt
                    ).strip().strip('"')
                except Exception:
                    translated_fm['title'] = title
            desc = fm.get('description', '')
            if desc and re.search(r'[\u4e00-\u9fff]', desc):
                try:
                    translated_fm['description'] = self.client.translate(
                        f'Translate this description to English: {desc}',
                        self.system_prompt
                    ).strip().strip('"')
                except Exception:
                    translated_fm['description'] = desc

        # Build translation metadata
        metadata = TranslationMetadata(
            source=rel_path,
            source_hash=source_hash,
            translated_at=datetime.now(timezone.utc).isoformat(),
            engine=self.engine_name,
            token_count=estimated_tokens,
        )

        # Reconstruct document
        if fm:
            new_fm = reconstruct_frontmatter(fm, translated_fm, metadata)
            result = f'---\n{new_fm}\n---\n{translated_body}'
        else:
            result = translated_body

        self.tokens_used += estimated_tokens
        self.files_translated += 1

        # Update manifest
        self.manifest.record(rel_path, source_hash,
                             self.engine_name, estimated_tokens)

        logger.info('OK %s', rel_path)
        return result

    def _estimate_cost(self, tokens: int) -> float:
        return tokens / 1000 * 0.01


# ---------------------------------------------------------------------------
# Post-translation validation
# ---------------------------------------------------------------------------

def validate_translation(output_path: Path, source_block_count: int) -> List[str]:
    """Validate a translated .en.md file. Returns list of warnings."""
    warnings: List[str] = []
    try:
        content = output_path.read_text(encoding='utf-8')
    except Exception as e:
        return [f'Cannot read output: {e}']

    # Check for leftover placeholders
    leftover = re.findall(r'__PRESERVED_\d+__', content)
    if leftover:
        warnings.append(f'{len(leftover)} leftover __PRESERVED_ placeholders')

    # Check frontmatter is parseable
    fm, fm_str, body = parse_frontmatter(content)
    if fm_str and not fm:
        warnings.append('Frontmatter exists but failed to parse as YAML')

    # Check translation metadata exists
    if fm and 'translation' not in fm:
        warnings.append('Missing translation metadata in frontmatter')

    return warnings


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
        logger.warning('Could not detect changed files: %s', e)
        return []


def find_all_chinese_md(docs_root: Path) -> List[Path]:
    """Find all Chinese .md files (excluding .en.md)."""
    files = []
    for f in docs_root.rglob('*.md'):
        if f.name.endswith('.en.md') or f.name in ('index.md', 'tags.md'):
            continue
        if any(part in ('images', 'generated', 'hooks', 'stylesheets',
                        'javascripts') for part in f.parts):
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
    """Load API key from environment or .env file."""
    engine_vars = {
        'anthropic': ['ANTHROPIC_AUTH_TOKEN'],
        'openai': ['OPENAI_API_KEY'],
        'deepl': ['DEEPL_API_KEY'],
    }
    fallback_vars = ['TRANSLATE_API_KEY']

    for var in engine_vars.get(engine, []) + fallback_vars:
        key = os.environ.get(var)
        if key:
            return key

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
    parser.add_argument('--force', action='store_true',
                        help='Force re-translation regardless of content hash')
    parser.add_argument('--max-tokens', type=int, default=MAX_TOKENS_PER_RUN,
                        help=f'Max tokens per run (default: {MAX_TOKENS_PER_RUN})')
    parser.add_argument('--batch-size', type=int, default=0,
                        help='Max files to translate per run (default: unlimited)')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Enable debug-level logging')
    args = parser.parse_args()

    project_root = Path(__file__).parent.parent
    docs_root = project_root / 'documents'
    cache_dir = project_root / '.cache' / 'translations'

    # Setup logging
    setup_logging(cache_dir, args.verbose)
    logger.info('Translation run started (engine=%s, dry_run=%s, force=%s)',
                args.engine, args.dry_run, args.force)

    # Collect files to translate
    if args.file:
        target = Path(args.file)
        if not target.is_absolute():
            target = project_root / target
        if not target.exists():
            logger.error('File not found: %s', target)
            sys.exit(1)
        files = [target]
    elif args.changed:
        files = detect_changed_files(project_root)
        if not files:
            logger.info('No changed Chinese .md files detected.')
            sys.exit(0)
        logger.info('Detected %d changed file(s):', len(files))
        for f in files:
            logger.info('  %s', f.relative_to(project_root))
    elif args.all:
        files = find_all_chinese_md(docs_root)
        if not files:
            logger.info('No Chinese .md files found.')
            sys.exit(0)
        logger.info('Found %d Chinese .md files.', len(files))
        if not args.dry_run:
            confirm = input('Translate all? [y/N] ').strip().lower()
            if confirm != 'y':
                logger.info('Aborted.')
                sys.exit(0)

    # Load API key
    if not args.dry_run:
        api_key = load_api_key(args.engine)
        if not api_key:
            logger.error('No API key found for %s.', args.engine)
            print(f'\nSet one of:', file=sys.stderr)
            if args.engine == 'anthropic':
                print('  export ANTHROPIC_AUTH_TOKEN=\'your-key\'', file=sys.stderr)
            elif args.engine == 'openai':
                print('  export OPENAI_API_KEY=\'your-key\'', file=sys.stderr)
            print('  export TRANSLATE_API_KEY=\'your-key\'', file=sys.stderr)
            print('  Or add to .env (see .env.example)', file=sys.stderr)
            sys.exit(1)
        client = create_client(args.engine, api_key)
    else:
        client = create_client(args.engine, 'dummy')

    # Initialize components
    manifest = TranslationManifest(cache_dir)
    manifest.load()

    preprocessor = ContentPreprocessor()
    system_prompt = build_system_prompt(project_root)

    pipeline = TranslationPipeline(
        client=client,
        manifest=manifest,
        preprocessor=preprocessor,
        system_prompt=system_prompt,
        engine_name=args.engine,
        dry_run=args.dry_run,
        force=args.force,
        max_tokens=args.max_tokens,
    )

    # Translate files
    batch_count = 0
    for filepath in files:
        rel = filepath.relative_to(project_root)
        output_path = get_output_path(filepath)

        if args.dry_run:
            pipeline.translate_file(filepath, project_root)
            continue

        # Track preserved block count for validation
        source_content = filepath.read_text(encoding='utf-8')
        _, source_blocks = preprocessor.extract_all(source_content)
        source_block_count = len(source_blocks)

        result = pipeline.translate_file(filepath, project_root)
        if result is not None:
            try:
                output_path.write_text(result.rstrip('\n') + '\n',
                                       encoding='utf-8')
                logger.info('Written: %s', output_path.relative_to(project_root))

                # Validate
                warns = validate_translation(output_path, source_block_count)
                for w in warns:
                    logger.warning('Validation %s: %s', rel, w)

                batch_count += 1
            except Exception as e:
                logger.error('Error writing %s: %s', output_path, e)
                pipeline.files_failed += 1

        # Check batch limit
        if args.batch_size > 0 and batch_count >= args.batch_size:
            logger.info('Batch limit reached (%d files). Stopping.',
                        args.batch_size)
            break

    # Save manifest
    manifest.save()

    # Summary
    logger.info('=' * 50)
    if args.dry_run:
        logger.info('Estimated tokens: %s', f'{pipeline.tokens_used:,}')
        cost = pipeline._estimate_cost(pipeline.tokens_used)
        logger.info('Estimated cost:   ~$%.2f', cost)
        logger.info('Files:            %d', len(files))
    else:
        logger.info('Files translated: %d', pipeline.files_translated)
        logger.info('Files skipped:    %d', pipeline.files_skipped)
        logger.info('Files failed:     %d', pipeline.files_failed)
        logger.info('Tokens used:      %s', f'{pipeline.tokens_used:,}')
    logger.info('=' * 50)

    sys.exit(1 if pipeline.files_failed > 0 and not args.dry_run else 0)


if __name__ == '__main__':
    main()
