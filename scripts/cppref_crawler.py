#!/usr/bin/env python3
"""
cppref_crawler.py — 从 cppreference.com 抓取 C++ 特性页面内容，保存为 JSON 中间产物。

用法:
    python scripts/cppref_crawler.py                          # 抓取 manifest 中全部特性
    python scripts/cppref_crawler.py --features unique_ptr    # 只抓取指定特性
    python scripts/cppref_crawler.py --output-dir /tmp/cppref # 自定义输出目录

输出:
    <output-dir>/raw/<feature-slug>.json  — 每个 feature 一个 JSON 文件

依赖:
    pip install requests beautifulsoup4
"""

import argparse
import json
import re
import sys
import time
from pathlib import Path
from urllib.parse import urlparse

try:
    import requests
    from bs4 import BeautifulSoup
except ImportError:
    print("错误: 需要安装依赖 — pip install requests beautifulsoup4", file=sys.stderr)
    sys.exit(1)

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
DEFAULT_MANIFEST = SCRIPT_DIR / "cppref_manifest.json"
DEFAULT_OUTPUT_DIR = SCRIPT_DIR / "cppref_cache"

# 请求间隔（秒），避免对 cppreference 造成压力
REQUEST_DELAY = 1.5

HEADERS = {
    "User-Agent": (
        "TutorialAwesomeModernCPP/1.0 "
        "(https://github.com/charliechen114514/Tutorial_AwesomeModernCPP; "
        "educational C++ tutorial project) "
        "python-requests"
    ),
}


def slugify(feature_name: str) -> str:
    """将特性名转为文件名友好的 slug。"""
    name = re.sub(r"^std::", "", feature_name.lower())
    return re.sub(r"[^a-z0-9]+", "-", name).strip("-")


def fetch_page(url: str, timeout: int = 30) -> str:
    """抓取页面 HTML。"""
    resp = requests.get(url, headers=HEADERS, timeout=timeout)
    resp.raise_for_status()
    return resp.text


def extract_content(html: str, url: str) -> dict:
    """
    从 cppreference 页面 HTML 提取结构化内容。

    提取内容:
    - title: 页面标题
    - header: 涉及的头文件
    - description: 开头描述段落
    - member_table: 成员函数/操作表格
    - example_code: 示例代码
    - see_also: 另见链接
    - raw_text: 全文纯文本（供 Claude API 作为上下文）
    """
    soup = BeautifulSoup(html, "html.parser")

    # 移除导航、侧边栏、脚本等干扰内容
    for tag in soup.find_all(["nav", "script", "style", "footer", "aside"]):
        tag.decompose()

    # 标题
    title = ""
    h1 = soup.find("h1")
    if h1:
        title = h1.get_text(strip=True)

    # 头文件 — cppreference 有两种模式：
    # 1) 独立 <code>#include <...></code> 标签
    # 2) 表格行中的 "Defined in header <xxx>"
    header = ""
    for code_tag in soup.find_all("code"):
        text = code_tag.get_text(strip=True)
        if text.startswith("#include"):
            header = text
            break
    if not header:
        # 从表格文本中提取 "Defined in header <xxx>"
        for td in soup.find_all("td"):
            td_text = td.get_text(strip=True)
            match = re.search(r"Defined in header\s*<([^>]+)>", td_text)
            if match:
                header = f"#include <{match.group(1)}>"
                break

    # 描述 — 取第一个标题后面的段落（cppreference 的标题可能是 h1 或其他层级）
    description_parts = []
    start_heading = h1
    if not start_heading:
        start_heading = soup.find(["h1", "h2", "h3"])
    if start_heading:
        for sibling in start_heading.find_next_siblings():
            if sibling.name in ("h1", "h2", "h3"):
                break
            if sibling.name == "p":
                text = sibling.get_text(strip=True)
                if text and len(text) > 15:
                    description_parts.append(text)

    # 表格 — 提取成员函数/操作表格为文本
    tables = []
    for table in soup.find_all("table", class_="t-dcl-begin"):
        rows = []
        for tr in table.find_all("tr"):
            cells = [td.get_text(strip=True) for td in tr.find_all(["td", "th"])]
            if any(cells):
                rows.append(" | ".join(cells))
        if rows:
            tables.append("\n".join(rows))

    # 也提取 wikitable 类的表格
    for table in soup.find_all("table", class_="wikitable"):
        rows = []
        for tr in table.find_all("tr"):
            cells = [td.get_text(strip=True) for td in tr.find_all(["td", "th"])]
            if any(cells):
                rows.append(" | ".join(cells))
        if rows:
            tables.append("\n".join(rows))

    # 示例代码
    examples = []
    for pre in soup.find_all("pre"):
        code = pre.get_text()
        if len(code) > 30 and ("int main" in code or "auto" in code or "std::" in code):
            examples.append(code.strip())

    # 另见 — 搜索标题含 "see also" 或 "另见" 的段落
    see_also = []
    for heading in soup.find_all(["h2", "h3"]):
        heading_text = heading.get_text(strip=True).lower()
        if "see also" in heading_text or "另见" in heading_text:
            for sibling in heading.find_next_siblings():
                if sibling.name in ("h1", "h2", "h3"):
                    break
                for a in sibling.find_all("a", href=True):
                    href = a["href"]
                    text = a.get_text(strip=True)
                    if text and href.startswith("/w/"):
                        see_also.append(
                            {"text": text, "url": f"https://en.cppreference.com{href}"}
                        )
            break  # 只取第一个 "see also" 段

    # 全文纯文本 — 截取前 8000 字符，足够 Claude API 理解上下文
    main_content = soup.find("div", id="mw-content-text")
    raw_text = ""
    if main_content:
        raw_text = main_content.get_text(separator="\n", strip=True)
        raw_text = re.sub(r"\n{3,}", "\n\n", raw_text)
        raw_text = raw_text[:8000]

    return {
        "title": title,
        "header": header,
        "description": "\n\n".join(description_parts[:5]),
        "tables": tables[:5],
        "examples": examples[:3],
        "see_also": see_also[:10],
        "raw_text": raw_text,
        "source_url": url,
    }


def load_manifest(manifest_path: Path) -> list[dict]:
    """加载 manifest 文件。"""
    if not manifest_path.exists():
        print(f"错误: manifest 文件不存在: {manifest_path}", file=sys.stderr)
        sys.exit(1)
    with open(manifest_path, "r", encoding="utf-8") as f:
        return json.load(f)


def crawl_feature(feature_entry: dict, output_dir: Path) -> Path:
    """抓取单个特性的 cppreference 页面并保存为 JSON。"""
    feature_name = feature_entry["feature"]
    url = feature_entry["cppref_url"]
    slug = slugify(feature_name)

    output_file = output_dir / "raw" / f"{slug}.json"
    output_file.parent.mkdir(parents=True, exist_ok=True)

    print(f"  抓取: {feature_name} — {url}")
    html = fetch_page(url)
    content = extract_content(html, url)

    # 合入 manifest 元数据
    result = {
        **feature_entry,
        "crawled_content": content,
    }

    with open(output_file, "w", encoding="utf-8") as f:
        json.dump(result, f, ensure_ascii=False, indent=2)

    print(f"  已保存: {output_file.relative_to(PROJECT_ROOT)}")
    return output_file


def main():
    parser = argparse.ArgumentParser(
        description="从 cppreference.com 抓取 C++ 特性页面内容"
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help="manifest 文件路径（默认: scripts/cppref_manifest.json）",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="输出目录（默认: scripts/cppref_cache/）",
    )
    parser.add_argument(
        "--features",
        nargs="*",
        help="只抓取指定特性（按 feature 名匹配，支持子串匹配）",
    )
    args = parser.parse_args()

    manifest = load_manifest(args.manifest)

    # 过滤特性
    if args.features:
        keywords = [k.lower() for k in args.features]
        manifest = [
            entry
            for entry in manifest
            if any(kw in entry["feature"].lower() for kw in keywords)
        ]
        if not manifest:
            print("没有匹配的特性", file=sys.stderr)
            sys.exit(1)

    print(f"准备抓取 {len(manifest)} 个特性...")

    results = []
    for i, entry in enumerate(manifest):
        try:
            output = crawl_feature(entry, args.output_dir)
            results.append(output)
        except requests.RequestException as e:
            print(f"  抓取失败: {entry['feature']} — {e}", file=sys.stderr)
        if i < len(manifest) - 1:
            time.sleep(REQUEST_DELAY)

    print(f"\n完成: 成功 {len(results)}/{len(manifest)} 个特性")
    print(f"输出目录: {args.output_dir / 'raw'}")


if __name__ == "__main__":
    main()
