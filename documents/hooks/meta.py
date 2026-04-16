"""MkDocs hook: auto-generate meta description from body text."""
import re


def on_page_markdown(markdown, page, **kwargs):
    """Auto-extract description when frontmatter has none."""
    if page.meta.get("description"):
        return
    # Strip markdown syntax and extract plain text
    text = re.sub(r"[#*`\[\]()!>|~^=]", "", markdown)
    text = re.sub(r"\n+", " ", text).strip()
    if len(text) > 160:
        text = text[:160].rsplit(" ", 1)[0] + "..."
    if text:
        page.meta["description"] = text
