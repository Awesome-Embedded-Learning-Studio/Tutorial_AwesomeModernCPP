# 教学动画目录

**播放器代码共用，动画内容按 JSON 数据分发，每次嵌入的播放进度独立。** 一篇文章可以没有动画，也可以放多个动画；不用为每篇文章复制 Vue 播放器。

```text
components/
├── Anim.vue                         # 所有文章的统一入口，按 id 选数据
└── animations/
    ├── README.md                    # 本说明
    └── generated/                   # animy_maker 分发目录，整体原样保留
        ├── AnimPlayer.vue           # 共用播放器，勿手改
        ├── data/<vol>/<id>.json     # 全部动画，按卷分组，id = 文件名
        └── README.md                # 编译器附带的通用说明
```

`data/` 下按教程卷分子目录（`vol1/`、`vol3/`、`vol8/`…），分组由 DSL 的 `group:` 字段决定，编译时自动落盘。**id 只看文件名**，与分组无关：文章里 `<Anim id="opp1-vector-growth" />` 不因挪动目录而变化；文件名必须全局唯一（dev 模式重复会告警）。

`Anim.vue` 递归收集 `generated/data/**/*.json`；同一份数据可以被多篇文章复用，每个播放器实例仍独立。切换 id 会重新创建播放实例，从头播放。请使用 PascalCase 的 `<Anim>` 标签。

## 在文章里使用

```html
<Anim id="opp1-vector-growth" />
```

## 新增或修改内容

改 DSL 后重新编译，输出目录指定为 `generated/`。从教程根目录运行，`ANIMY_SOURCE` 指向工具仓库的 `src`：

```bash
PYTHONPATH="$ANIMY_SOURCE" .venv/bin/python -m animation_maker compile \
  /path/to/scene.yaml --backend web \
  -o site/.vitepress/theme/components/animations/generated
```

编译器生成的 `posters/` 已被 Git 忽略。需要升级生成播放器时才加 `--force-player`；升级前后验证已有动画。`generated/README.md` 是上游通用模板，本仓库的嵌入方式以当前说明为准。

drawio 放在文档侧对应文章的资源目录，用相对链接引用：

```text
documents/vol1-fundamentals/ch00/
├── 00-preface.md
├── 03-first-program.md
└── assets/
    ├── 00-preface/learning-route.drawio
    └── 03-first-program/compilation-pipeline.drawio
```

当前已将首批 ch00 图迁入此结构，其他章节的旧图随对应文章维护时逐步整理。新增动画后检查播放、暂停、单步、窄屏以及 `pnpm build`；核对图、动画数据和正文的术语及命令一致。
