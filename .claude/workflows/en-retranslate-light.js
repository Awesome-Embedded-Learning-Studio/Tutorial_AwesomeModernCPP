export const meta = {
  name: 'en-retranslate-light',
  description: '英文站轻量重翻批次:标题表 → 翻译 → 四维快检 → 直修(无对抗验证;用于导航页与旧译欠账)',
  phases: [
    { title: 'Titles', detail: '整批标题对照表,统一本批所有文章的英文标题' },
    { title: 'Translate', detail: '每篇一个翻译 agent,按 translation-style.md 产出 en 文件' },
    { title: 'Review', detail: '每篇一个轻量审查 agent,四维快检' },
    { title: 'Fix', detail: 'confirmed 问题逐条落盘修复' },
  ],
}

if (!args || !Array.isArray(args.files) || !args.files.length || !args.batch) {
  throw new Error('需要 args: { batch: string, files: string[] }')
}

const FILES = args.files
const BATCH = args.batch
const enOf = (p) => p.replace(/^documents\//, 'documents/en/')
const short = (p) => p.split('/').pop()

const OUT_TITLES = {
  type: 'object',
  properties: {
    titles: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          zh_file: { type: 'string' },
          zh_title: { type: 'string' },
          en_title: { type: 'string' },
        },
        required: ['zh_file', 'zh_title', 'en_title'],
      },
    },
  },
  required: ['titles'],
}
const OUT_T = {
  type: 'object',
  properties: {
    ok: { type: 'boolean' },
    source_hash_written: { type: 'string' },
    notes: { type: 'array', items: { type: 'string' } },
  },
  required: ['ok', 'source_hash_written', 'notes'],
}
const OUT_R = {
  type: 'object',
  properties: {
    findings: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          dimension: { type: 'string' },
          severity: { type: 'string' },
          summary: { type: 'string' },
          detail: { type: 'string' },
        },
        required: ['dimension', 'severity', 'summary', 'detail'],
      },
    },
  },
  required: ['findings'],
}
const OUT_V = {
  type: 'object',
  properties: { isReal: { type: 'boolean' }, reason: { type: 'string' } },
  required: ['isReal', 'reason'],
}
const OUT_F = {
  type: 'object',
  properties: {
    applied: { type: 'number' },
    remaining: { type: 'array', items: { type: 'string' } },
  },
  required: ['applied', 'remaining'],
}

const listZh = FILES.map((p) => `- ${p}`).join('\n')

const titlesPrompt = `你是英文站翻译的标题统一 agent。本批(${BATCH})共 ${FILES.length} 篇,先读 .claude/style/translation-style.md 第 2/6 节与 documents/en/appendix/terminology.md,然后读出下列每个中文源的 frontmatter title(与正文 H1,若两者不同以 title 为准、H1 一并给出对应译法时保持同系列措辞):

${listZh}

产出整批统一英文标题对照表。要求:
- 同系列文章标题保持句式/前缀一致(如 "Why an RTOS · Part 1/2" 式的系列感)
- 精炼、口语化、信息守恒,术语对齐术语表
- 若某篇已有英文版(documents/en/ 镜像路径存在),读它的现有 title 作为基准(可沿用或小幅改进)
只返回对照表,不写任何文件。`

const translatePrompt = (zh, titleMap) => `你是 Tutorial_AwesomeModernCPP 的英文站翻译 agent。把一篇中文文章完整翻译成英文并写出英文文件。仓库根目录就是你的工作目录。

按顺序必读:
1. .claude/style/translation-style.md —— 全部规则逐条遵守,特别是 frontmatter 规则、translation 块、第 5 节正文规则(含 5.3 三类代码块处理:注释译英/字符串字面量与程序输出原样/示意性图块标签译英)、禁止项、第 9 节自检清单
2. documents/en/appendix/terminology.md —— 术语表,翻译必须对齐
3. 中文源: ${zh}

本批标题对照表(自己的 title 与 H1 必须用此表中本篇的 en_title;frontmatter 的 prerequisites/related 引用到本批其他文章时,也必须用对应 en_title):

${JSON.stringify(titleMap, null, 2)}

产出文件: ${enOf(zh)}(en 镜像路径,整文件 = frontmatter + 正文 + translation 块)

要点:
- source_hash 用命令 tr -d '\\r' < ${zh} | sha256sum 计算,取第一列十六进制
- translated_at 用命令 date -u +%Y-%m-%dT%H:%M:%S+00:00 取值,YAML 里值加单引号
- engine: anthropic;token_count 为中文源 token 粗估(中文约 1.5 字符/token)
- 链接与图片的 target 相对路径与中文源逐字符相同;代码块按规范 5.3 三类处理
- prerequisites/related 引用本批之外的文章:若其 en 镜像文件已存在,必须 grep 该文件 frontmatter 用其实际 title;若不存在,合理翻译并在 notes 里记 "future-title: <zh> => <en>" 供后续批次对齐
- 写完后执行规范第 9 节自检清单,逐项通过后才算完成

不要改动中文源。发现中文源疑似技术错误时不要修,记入 notes 返回。
返回:ok(自检是否全过)、source_hash_written(实际写入的哈希)、notes(疑点/future-title,无则空数组)。`

const reviewPrompt = (zh) => `你是英文站翻译的轻量审查 agent(四维快检,适用于导航页/参考卡等),不要修改任何文件。

读 .claude/style/translation-style.md 后对照:
中文源 ${zh}  vs  英文 ${enOf(zh)}

四维快检:
1 结构: H2/H3/段落/列表一一对应,无明显整段漏译;正文无中文残留(frontmatter tags 与代码内字符串除外)
2 代码块: 数量一致,非注释内容逐字符相同(注释应已译英)
3 frontmatter: title/description 已译英;数值字段与中文源一致;source_hash 等于命令 tr -d '\\r' < ${zh} | sha256sum 的输出(自己跑一遍核对)
4 链接: 相对 target 与中文源逐字符相同;外链 URL 原样

每条问题输出: dimension("1"-"4")、severity(critical|minor)、summary、detail。无问题返回空 findings。`

const fixPrompt = (zh, confirmed) => `你是翻译修复 agent。英文文件 ${enOf(zh)} 存在以下确认的问题,逐条修复:

${JSON.stringify(confirmed, null, 2)}

先读 .claude/style/translation-style.md 与中文源 ${zh},再修 ${enOf(zh)}。要求:
- 只修列出的问题,不顺手改别的
- 修后不违反规范任何条款(信息守恒、代码块 5.3 规则、链接 target 不变)
- translation.source_hash 描述的是中文源,不要动它
返回 {applied: 实际修复条数, remaining: 无法修复或需人工判断的条目说明(空数组=全部修完)}。`

phase('Titles')
log(`${BATCH}: ${FILES.length} 篇,先生成整批标题对照表`)
const titleResult = await agent(titlesPrompt, { label: 'titles:map', phase: 'Titles', schema: OUT_TITLES })
const titleMap = (titleResult && titleResult.titles) || []
if (!titleMap.length) {
  log('警告: 标题对照表为空,翻译 agent 将各自定标题')
}

// 429 限流应对:分波推进,每波 5 篇,波间串行,把瞬时并发压下来
const WAVE = 5
const results = []
for (let w = 0; w < FILES.length; w += WAVE) {
  const wave = FILES.slice(w, w + WAVE)
  log(`${BATCH}: wave ${Math.floor(w / WAVE) + 1}/${Math.ceil(FILES.length / WAVE)} (${wave.length} 篇)`)
  const waveResults = await pipeline(
    wave,
  (zh) => agent(translatePrompt(zh, titleMap), { label: `tl:${short(zh)}`, phase: 'Translate', schema: OUT_T }),
  (t, zh) =>
    t && t.ok
      ? agent(reviewPrompt(zh), { label: `rv:${short(zh)}`, phase: 'Review', schema: OUT_R }).then((r) => ({ zh, t, r }))
      : { zh, t: t || { ok: false, source_hash_written: '', notes: ['translate agent died'] }, r: { findings: [] } },
  async (x) => {
    if (!x.t.ok) {
      return { file: x.zh, status: 'TRANSLATE_FAILED', notes: x.t.notes }
    }
    const fs = (x.r && x.r.findings) || []
    if (!fs.length) {
      return { file: x.zh, status: 'CLEAN', hash: x.t.source_hash_written, findings: 0, fixed: 0, notes: x.t.notes }
    }
    const crit = fs.filter((f) => f.severity === 'critical')
    if (!crit.length) {
      return { file: x.zh, status: 'CLEAN', hash: x.t.source_hash_written, findings: fs.length, fixed: 0, notes: x.t.notes }
    }
    const fx = await agent(fixPrompt(x.zh, crit), { label: `fx:${short(x.zh)}`, phase: 'Fix', schema: OUT_F })
    return {
      file: x.zh,
      status: 'FIXED',
      hash: x.t.source_hash_written,
      findings: fs.length,
      fixed: fx ? fx.applied : 0,
      notes: x.t.notes,
    }
  }
  )
  results.push(...waveResults.filter(Boolean))
}

const ok = results
const produced = ok.filter((r) => r.status !== 'TRANSLATE_FAILED').length
log(`${BATCH} 完成: ${produced}/${ok.length} 篇产出`)
return ok
