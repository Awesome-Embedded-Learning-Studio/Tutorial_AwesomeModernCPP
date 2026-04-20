"""MkDocs hook: inject 'not translated' banner for English pages without .en.md source.

Uses Jinja2 template override instead of on_page_markdown hook,
because mkdocs-static-i18n runs on_page_markdown internally per-locale
and does not expose locale to external hooks.
"""
