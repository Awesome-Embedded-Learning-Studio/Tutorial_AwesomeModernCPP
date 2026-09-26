#!/usr/bin/env python3
"""Scan documents/en for translations whose zh source has drifted.

A translated mirror is stale when sha256(zh source, all \r stripped) no
longer matches the source_hash recorded in its translation block. Run
this after zh-side PRs land on main to derive incremental retranslation
batches.

Usage:
    python3 scripts/en_stale_scan.py                # report stale mirrors
    python3 scripts/en_stale_scan.py --verbose      # also list healthy ones
    python3 scripts/en_stale_scan.py --paths a.md b.md   # restrict to zh paths
"""

import argparse
import hashlib
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    yaml = None

ROOT = Path(__file__).resolve().parent.parent
EN_ROOT = ROOT / 'documents' / 'en'
ZH_ROOT = ROOT / 'documents'


def source_hash(path: Path) -> str:
    """Mirror the pipeline convention: sha256 of content with every \r removed."""
    data = path.read_bytes().replace(b'\r', b'')
    return hashlib.sha256(data).hexdigest()


def parse_translation(fm: dict) -> dict:
    t = fm.get('translation')
    return t if isinstance(t, dict) else {}


def load_frontmatter(path: Path):
    content = path.read_text(encoding='utf-8')
    if yaml is None:
        # Fallback: pull the translation block fields with plain string ops
        fields = {}
        in_block = False
        for line in content.split('\n'):
            if line == '---':
                if in_block:
                    break
                in_block = True
                continue
            if in_block and line.startswith(('  source:', '  source_hash:')):
                key, _, val = line.strip().partition(':')
                fields[key] = val.strip()
        t = {}
        if 'source' in fields:
            t['source'] = fields['source']
        if 'source_hash' in fields:
            t['source_hash'] = fields['source_hash']
        return t
    import re
    m = re.match(r'^---\s*\n(.*?)\n---', content, re.DOTALL)
    if not m:
        return {}
    try:
        fm = yaml.safe_load(m.group(1)) or {}
    except Exception:
        return {}
    return parse_translation(fm)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--verbose', action='store_true',
                        help='also report up-to-date mirrors')
    parser.add_argument('--paths', nargs='*',
                        help='only check zh paths under these prefixes')
    args = parser.parse_args()

    stale, missing_src, no_block, fresh = [], [], [], 0
    for en_md in sorted(EN_ROOT.rglob('*.md')):
        t = load_frontmatter(en_md)
        src = t.get('source')
        recorded = t.get('source_hash')
        if not src or not recorded:
            no_block.append(en_md)
            continue
        zh = ROOT / src
        if not zh.exists():
            missing_src.append((en_md, src))
            continue
        rel = zh.relative_to(ZH_ROOT).as_posix()
        if args.paths and not any(rel.startswith(p.rstrip('/') + '/') or rel == p
                                  for p in args.paths):
            continue
        if source_hash(zh) != recorded:
            stale.append((en_md, src))
        else:
            fresh += 1

    for en_md, src in stale:
        print(f"STALE   {en_md.relative_to(ROOT).as_posix()}  <-  {src}")
    for en_md, src in missing_src:
        print(f"NO-SRC  {en_md.relative_to(ROOT).as_posix()}  ->  {src}")
    if args.verbose:
        print(f"-- {len(no_block)} file(s) without a translation block (not yet retranslated)")
        print(f"-- {fresh} mirror(s) up to date")

    print()
    print(f"stale={len(stale)}  fresh={fresh}  missing_src={len(missing_src)}  "
          f"no_translation_block={len(no_block)}")
    if not args.verbose:
        print("(no translation block = not yet retranslated; full-retranslation "
              "pending, not drift)")
    # Stale mirrors are the next incremental batch
    sys.exit(1 if stale or missing_src else 0)


if __name__ == '__main__':
    main()
