---
id: 006
title: "更新 GitHub Actions CI 工作流适配新目录结构"
category: architecture
priority: P0
status: pending
created: 2026-04-15
assignee: charliechen
depends_on: [002, 003]
blocks: []
estimated_effort: medium
---

# 更新 GitHub Actions CI 工作流适配新目录结构

## 目标

更新 `.github/workflows/` 下的 GitHub Actions 工作流文件，使其在新的 `documents/` + `code/` 目录结构下正常运行。主要涉及 deploy.yml（站点部署）和 lint.yml（文档检查）两个工作流。

## 验收标准

- [ ] `deploy.yml` 工作流成功触发并完成：检出代码 -> 安装依赖 -> 构建 -> 部署到 gh-pages
- [ ] `lint.yml` 工作流的 `markdown-lint` job 检查 `documents/**/*.md`（而非 `tutorial/**/*.md`）
- [ ] `lint.yml` 工作流的 `validate-frontmatter` job 成功运行 `validate_frontmatter.py`
- [ ] 部署后的 GitHub Pages 站点可以正常访问，导航和页面均正确
- [ ] CI 运行日志中无任何路径相关的错误或警告

## 实施说明

### 1. deploy.yml

**当前配置** (`.github/workflows/deploy.yml`)：

```yaml
steps:
  - name: 检出仓库
    uses: actions/checkout@v4
    with:
      fetch-depth: 0  # 获取完整历史，显示文章修改时间

  - name: 设置 Python
    uses: actions/setup-python@v5
    with:
      python-version: "3.11"
      cache: 'pip'

  - name: 安装依赖
    run: pip install -e ./scripts

  - name: 构建网站
    run: mkdocs build --clean

  - name: 部署到 GitHub Pages
    uses: peaceiris/actions-gh-pages@v4
    with:
      github_token: ${{ secrets.GITHUB_TOKEN }}
      publish_dir: ./site
```

**分析**：`deploy.yml` 中没有直接引用 `tutorial/` 或 `codes_and_assets/` 路径。`mkdocs build --clean` 会读取 `mkdocs.yml` 中的 `docs_dir: "documents"`（已在 003 中更新），自动从 `documents/` 目录读取源文件。`./site` 输出目录不受影响。

**结论**：`deploy.yml` 不需要修改路径。但需要确认：
- `pip install -e ./scripts` 安装的依赖中包含新增的插件（mkdocs-redirects 等），这取决于 `scripts/pyproject.toml` 是否已更新（见 003）
- `fetch-depth: 0` 仍然需要，因为 `git-revision-date-localized` 插件需要完整历史

**可选优化**：在构建步骤前增加一个验证步骤，确保 `documents/` 目录存在：

```yaml
- name: 验证文档目录
  run: |
    if [ ! -d "documents" ]; then
      echo "Error: documents/ directory not found"
      exit 1
    fi
    echo "Found $(find documents -name '*.md' | wc -l) markdown files"
```

### 2. lint.yml - markdown-lint job

**当前配置** (`.github/workflows/lint.yml` 第 22-28 行)：

```yaml
- name: Run markdownlint
  uses: DavidAnson/markdownlint-cli2-action@v17
  with:
    config: '.markdownlint.json'
    globs: |
      tutorial/**/*.md
      !tutorial/**/index.md
```

**修改方案**：

```yaml
- name: Run markdownlint
  uses: DavidAnson/markdownlint-cli2-action@v17
  with:
    config: '.markdownlint.json'
    globs: |
      documents/**/*.md
      !documents/**/index.md
```

变更说明：
- `tutorial/**/*.md` -> `documents/**/*.md`：检查新的文档目录
- `!tutorial/**/index.md` -> `!documents/**/index.md`：继续排除 index.md 文件

### 3. lint.yml - validate-frontmatter job

**当前配置** (`.github/workflows/lint.yml` 第 29-48 行)：

```yaml
validate-frontmatter:
  name: Validate Frontmatter
  runs-on: ubuntu-latest

  steps:
    - name: Checkout repository
      uses: actions/checkout@v4

    - name: Set up Python
      uses: actions/setup-python@v5
      with:
        python-version: '3.11'

    - name: Install dependencies
      run: |
        pip install pyyaml

    - name: Validate frontmatter
      run: |
        python scripts/validate_frontmatter.py
```

**分析**：此 job 运行 `validate_frontmatter.py`，该脚本内部硬编码了 `project_root / 'tutorial'` 路径。在 005 中，脚本将更新为 `project_root / 'documents'`。因此本 job 本身不需要修改运行命令。

但需要注意：`pip install pyyaml` 仅安装了 pyyaml，而 `validate_frontmatter.py` 只依赖 pyyaml，所以无需更改依赖安装步骤。

**结论**：`validate-frontmatter` job 的 YAML 配置不需要修改。只要 `validate_frontmatter.py` 已更新（见 005），此 job 就会自动使用新路径。

### 4. 可选增强

#### 缓存优化

`deploy.yml` 已经使用了 `cache: 'pip'`，这会缓存 pip 下载的包。由于新增了几个 mkdocs 插件依赖，首次构建时缓存会失效，后续构建会恢复缓存。无需额外操作。

#### 构建产物检查

可以在 `deploy.yml` 的构建步骤后增加一个检查，确保关键页面存在于输出中：

```yaml
- name: 验证构建产物
  run: |
    test -f site/index.html || (echo "Error: site/index.html not found" && exit 1)
    test -f site/404.html || (echo "Warning: site/404.html not found")
    echo "Build output size: $(du -sh site/ | cut -f1)"
```

#### lint.yml 触发条件

当前 lint.yml 在 `push` 到 `main` 和 `pull_request` 到 `main` 时触发。这不需要更改。但如果后续添加了新的工作流（如代码编译测试 `code/` 目录下的 C++ 代码），可以在此文件中新增 job 或创建新工作流。

### 5. 部署验证清单

部署成功后，在 GitHub Pages 上验证以下内容：

1. 首页 (`https://<username>.github.io/<repo>/`) 正常加载
2. 导航栏显示所有顶级域（核心嵌入式C++教程、C++模板教程等）
3. 随机选择 3-5 个页面，确认内容正确渲染
4. 搜索功能正常工作
5. 暗色/亮色模式切换正常
6. 如果配置了 redirects，测试旧 URL 是否正确重定向

## 涉及文件

- `.github/workflows/deploy.yml`（可选优化，非必须修改）
- `.github/workflows/lint.yml`（修改 markdownlint 的 `globs` 配置，从 `tutorial/` 改为 `documents/`）

## 参考资料

- 当前 `.github/workflows/deploy.yml` 和 `.github/workflows/lint.yml`
- [GitHub Actions: peaceiris/actions-gh-pages](https://github.com/peaceiris/actions-gh-pages)
- [GitHub Actions: DavidAnson/markdownlint-cli2-action](https://github.com/DavidAnson/markdownlint-cli2-action)
- [GitHub Actions: actions/setup-python](https://github.com/actions/setup-python)
