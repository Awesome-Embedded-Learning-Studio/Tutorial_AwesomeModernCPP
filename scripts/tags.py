"""共享的 frontmatter 标签白名单与分类(单一数据源:scripts/tags.json)。

被 validate_frontmatter.py 与 check_quality.py 共同 import;
site/.vitepress/config/tags-manifest.ts(标签索引页生成器)读同一份 JSON。
补 tag 或调整分类只改 tags.json,Python 校验器与站点生成自动同步——
历史上有过「两套白名单不同步导致 Content Quality CI 挂」的坑,
本模块 + tags.json 就是为了根治它。
"""

import json
from pathlib import Path
from typing import Dict, List, Set

_TAXONOMY = json.loads(
    (Path(__file__).parent / 'tags.json').read_text(encoding='utf-8')
)

# [{key, zh, en, role: topic|platform|audience, tags: [...]}]
CATEGORIES: List[Dict] = _TAXONOMY['categories']

VALID_TAGS: Set[str] = {t for cat in CATEGORIES for t in cat['tags']}

PLATFORM_TAGS: Set[str] = {
    t for cat in CATEGORIES if cat['role'] == 'platform' for t in cat['tags']
}
AUDIENCE_TAGS: Set[str] = {
    t for cat in CATEGORIES if cat['role'] == 'audience' for t in cat['tags']
}


def category_of(tag: str) -> Dict | None:
    """返回标签所属分类字典;不在白名单返回 None。"""
    for cat in CATEGORIES:
        if tag in cat['tags']:
            return cat
    return None
