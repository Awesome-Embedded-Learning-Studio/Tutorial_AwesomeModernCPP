# en-retranslate —— 英文站重翻通用批次 Workflow

参数化脚本在同目录 `en-retranslate.js`。每个翻译批次一次调用:

```
Workflow({
  scriptPath: '.claude/workflows/en-retranslate.js',
  args: { batch: 'P2', files: ['documents/vol8-domains/embedded/f103/00-env-setup/index.md', ...] }
})
```

- `args.files`:该批中文源文件列表(en 镜像路径由脚本推导)
- `args.batch`:批次名,仅用于日志与标签

五阶段:**Titles**(整批标题对照表,消灭跨文件标题漂移)→ **Translate**(每篇一个 agent)→ **Review**(六维对照中文源)→ **Verify**(每条 finding 对抗验证)→ **Fix**(confirmed 落盘修复)。

规则单一来源:`.claude/style/translation-style.md`(翻译/审查 agent 必读,改规范改它,不改脚本)。

批次收尾(主会话负责,不入脚本):
1. `PYTHONUTF8=1 .venv/Scripts/python.exe scripts/check_links.py`
2. `npx markdownlint-cli "documents/en/<本批路径>/**/*.md"` + `PYTHONUTF8=1 .venv/Scripts/python.exe scripts/validate_frontmatter.py`(只看本批文件是否报错,主分支存量 zh 错误忽略)
3. 抽查 1-2 篇 `source_hash`
4. 绿了才 commit(`docs(en): ...` ,严禁 Co-Authored-By)
