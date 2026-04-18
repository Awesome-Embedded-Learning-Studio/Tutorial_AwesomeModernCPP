#!/usr/bin/env python3
"""
cppref_card_generator.py — 使用 Claude API 从爬取的 JSON 数据生成 C++ 特性参考卡。

用法:
    python scripts/cppref_card_generator.py                        # 生成全部已抓取的特性
    python scripts/cppref_card_generator.py --features unique_ptr  # 只生成指定特性
    python scripts/cppref_card_generator.py --dry-run              # 只打印 prompt 不调用 API

前置:
    1. 先运行 cppref_crawler.py 抓取内容
    2. 设置 ANTHROPIC_API_KEY 环境变量

依赖:
    pip install anthropic
"""

import argparse
import json
import os
import sys
from pathlib import Path

# 自动加载项目根目录的 .env 文件
_PROJECT_ROOT = Path(__file__).resolve().parent.parent
_ENV_FILE = _PROJECT_ROOT / ".env"
if _ENV_FILE.exists():
    with open(_ENV_FILE, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                key, _, value = line.partition("=")
                key = key.strip()
                value = value.strip()
                if key and key not in os.environ:
                    os.environ[key] = value

try:
    import anthropic
except ImportError:
    print("错误: 需要安装依赖 — pip install anthropic", file=sys.stderr)
    sys.exit(1)

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
DEFAULT_CACHE_DIR = SCRIPT_DIR / "cppref_cache"
DEFAULT_MANIFEST = SCRIPT_DIR / "cppref_manifest.json"
TEMPLATE_PATH = PROJECT_ROOT / ".templates" / "reference-card-template.md"
WRITING_STYLE_PATH = PROJECT_ROOT / ".claude" / "writting_style.md"

SYSTEM_PROMPT = """\
你是一个 C++ 参考文档生成助手。你的任务是根据 cppreference 的原始内容，生成符合以下模板的中文参考卡。

## 参考卡模板

{template}

## 生成规则

1. **一句话**：用自然的中文描述这个特性是什么、解决什么问题，不要翻译 cppreference 的英文描述
2. **头文件**：提取正确的 #include 指令
3. **核心 API 速查表**：提取最常用的 5-10 个操作/函数，格式为表格。签名要准确，直接取自 cppreference
4. **最小示例**：写一个不超过 20 行、完整可编译的代码示例，展示最典型的用法
5. **嵌入式适用性**：根据提供的评级（高/中/低），写 2-4 条要点分析在嵌入式开发中的适用性
6. **编译器支持**：从 cppreference 的 raw_text 中提取 GCC/Clang/MSVC 的最低支持版本
7. **另见**：
   - 如果有 tutorial_link，写：`[教程：XXX](相对路径)`
   - 写：`[cppreference: XXX](原始URL)`

## 重要约束

- API 签名必须与 cppreference 一致，不要编造
- 如果 raw_text 中找不到编译器支持信息，标注 "待补充"
- 不要写教程风格的叙事内容（不要踩坑叙述、不要"我们"人称）
- 保持参考卡的精炼结构化风格
- frontmatter 中的 tags 从 manifest 中的信息推导
"""


def build_frontmatter_tags(entry: dict) -> list[str]:
    """根据 manifest 条目生成合法的 tags 列表。"""
    tags = ["host", "cpp-modern"]

    # difficulty
    diff = entry.get("difficulty", "beginner")
    tags.append(diff)

    # 从 feature 名推导 tag
    feature_lower = entry["feature"].lower()
    tag_map = {
        "unique_ptr": "unique_ptr",
        "shared_ptr": "shared_ptr",
        "optional": "optional",
        "span": "span",
        "variant": "variant",
        "constexpr": "constexpr",
        "lambda": "lambda",
        "concepts": "concepts",
    }
    for key, tag in tag_map.items():
        if key in feature_lower:
            tags.append(tag)
            break

    # 智能指针类别
    if "ptr" in feature_lower and "智能指针" not in tags:
        tags.append("智能指针")

    # 类型安全类别
    if feature_lower in ["std::optional", "std::variant"]:
        if "类型安全" not in tags:
            tags.append("类型安全")

    return tags


def build_user_prompt(entry: dict, crawled: dict) -> str:
    """构建发送给 Claude 的用户 prompt。"""
    content = crawled

    tutorial_note = ""
    if entry.get("tutorial_link"):
        tutorial_note = f"对应教程文章相对路径: {entry['tutorial_link']}"

    embedded_rating = entry.get("embedded_rating", "medium")
    rating_desc = {
        "high": "高 — 直接在嵌入式开发中使用，零开销或开销可控",
        "medium": "中 — 可用但需注意开销，适合资源充足的场景",
        "low": "低 — 通常在裸机嵌入式开发中避免",
    }

    prompt = f"""\
请根据以下 cppreference 原始内容，生成特性参考卡 markdown 文件。

## 特性信息
- 特性名: {entry['feature']}
- 分类: {entry['category']}
- 序号: {entry['order']}
- C++ 标准: {entry['cpp_standard']}
- 难度: {entry['difficulty']}
- 嵌入式评级: {rating_desc.get(embedded_rating, embedded_rating)}
{tutorial_note}

## cppreference 原始内容

### 标题
{content.get('title', '')}

### 头文件
{content.get('header', '')}

### 描述
{content.get('description', '')}

### 表格
{chr(10).join(content.get('tables', []))}

### 示例代码
{chr(10).join('---' + chr(10) + ex for ex in content.get('examples', []))}

### 全文（截取）
{content.get('raw_text', '')}

### 另见链接
{json.dumps(content.get('see_also', []), ensure_ascii=False, indent=2)}

---
请输出完整的 markdown 文件内容（包含 frontmatter），不要有任何其他解释。
"""
    return prompt


def load_crawled_data(cache_dir: Path, feature_name: str) -> dict | None:
    """加载爬取的 JSON 数据。"""
    from cppref_crawler import slugify

    slug = slugify(feature_name)
    json_path = cache_dir / "raw" / f"{slug}.json"
    if not json_path.exists():
        return None
    with open(json_path, "r", encoding="utf-8") as f:
        return json.load(f)


def generate_card(
    client: anthropic.Anthropic,
    entry: dict,
    crawled_data: dict,
    template: str,
    model: str = "claude-sonnet-4-20250514",
) -> str:
    """调用 Claude API 生成参考卡。"""
    system_prompt = SYSTEM_PROMPT.format(template=template)
    user_prompt = build_user_prompt(entry, crawled_data.get("crawled_content", {}))

    message = client.messages.create(
        model=model,
        max_tokens=4096,
        system=system_prompt,
        messages=[{"role": "user", "content": user_prompt}],
    )

    return message.content[0].text


def main():
    parser = argparse.ArgumentParser(description="使用 Claude API 生成 C++ 特性参考卡")
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help="manifest 文件路径",
    )
    parser.add_argument(
        "--cache-dir",
        type=Path,
        default=DEFAULT_CACHE_DIR,
        help="爬取缓存目录",
    )
    parser.add_argument(
        "--features",
        nargs="*",
        help="只生成指定特性（按 feature 名匹配）",
    )
    parser.add_argument(
        "--model",
        default=None,
        help="Claude 模型（默认: 从 ANTHROPIC_DEFAULT_SONNET_MODEL 或 claude-sonnet-4-20250514）",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="只打印 prompt 不调用 API",
    )
    args = parser.parse_args()

    # 加载模板
    if not TEMPLATE_PATH.exists():
        print(f"错误: 模板文件不存在: {TEMPLATE_PATH}", file=sys.stderr)
        sys.exit(1)
    template = TEMPLATE_PATH.read_text(encoding="utf-8")

    # 加载 manifest
    with open(args.manifest, "r", encoding="utf-8") as f:
        manifest = json.load(f)

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

    # 检查缓存
    missing = []
    for entry in manifest:
        from cppref_crawler import slugify

        slug = slugify(entry["feature"])
        cache_path = args.cache_dir / "raw" / f"{slug}.json"
        if not cache_path.exists():
            missing.append(entry["feature"])

    if missing:
        print(f"警告: 以下特性尚未抓取，请先运行 cppref_crawler.py:")
        for name in missing:
            print(f"  - {name}")
        # 只处理已抓取的
        manifest = [
            entry for entry in manifest if entry["feature"] not in missing
        ]

    if not manifest:
        print("没有已抓取的特性可处理", file=sys.stderr)
        sys.exit(1)

    # dry-run 模式
    if args.dry_run:
        for entry in manifest:
            crawled = load_crawled_data(args.cache_dir, entry["feature"])
            if crawled:
                prompt = build_user_prompt(entry, crawled.get("crawled_content", {}))
                print(f"\n{'='*60}")
                print(f"特性: {entry['feature']}")
                print(f"{'='*60}")
                print(prompt[:2000])
                print("...(截断)")
        return

    # 初始化 Claude 客户端（支持代理环境变量）
    api_key = os.environ.get("ANTHROPIC_AUTH_TOKEN") or os.environ.get("ANTHROPIC_API_KEY")
    if not api_key:
        print("错误: 请设置 ANTHROPIC_AUTH_TOKEN 或 ANTHROPIC_API_KEY 环境变量", file=sys.stderr)
        sys.exit(1)

    base_url = os.environ.get("ANTHROPIC_BASE_URL")
    client_kwargs = {"api_key": api_key}
    if base_url:
        client_kwargs["base_url"] = base_url
    client = anthropic.Anthropic(**client_kwargs)

    # 确定模型
    model = args.model or os.environ.get("ANTHROPIC_DEFAULT_SONNET_MODEL", "claude-sonnet-4-20250514")
    print(f"准备生成 {len(manifest)} 张参考卡（模型: {model}）...")

    for entry in manifest:
        feature_name = entry["feature"]
        output_path = PROJECT_ROOT / entry["output_path"]

        print(f"\n生成: {feature_name}")

        crawled = load_crawled_data(args.cache_dir, feature_name)
        if not crawled:
            print(f"  跳过: 未找到缓存数据")
            continue

        try:
            card_md = generate_card(client, entry, crawled, template, model)
        except anthropic.APIError as e:
            print(f"  API 错误: {e}", file=sys.stderr)
            continue

        # 确保输出目录存在
        output_path.parent.mkdir(parents=True, exist_ok=True)

        # 清理可能的 markdown 代码块包裹
        card_md = card_md.strip()
        if card_md.startswith("```markdown"):
            card_md = card_md[len("```markdown"):]
        if card_md.startswith("```"):
            card_md = card_md[3:]
        if card_md.endswith("```"):
            card_md = card_md[:-3]
        card_md = card_md.strip()

        with open(output_path, "w", encoding="utf-8") as f:
            f.write(card_md + "\n")

        print(f"  已保存: {output_path.relative_to(PROJECT_ROOT)}")

    print(f"\n完成!")


if __name__ == "__main__":
    main()
