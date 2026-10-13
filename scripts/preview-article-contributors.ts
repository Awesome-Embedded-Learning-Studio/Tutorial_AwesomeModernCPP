// Generate a standalone HTML + CSS preview from the real Vue component, with no runtime dependencies.
import { createRequire, stripTypeScriptTypes } from 'node:module'
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'
import { parse, compileScript } from 'vue/compiler-sfc'
import { createSSRApp, h } from 'vue'
import { renderToString } from 'vue/server-renderer'
import matter from 'gray-matter'
import { formatArticleContributions, type ContributionPerson } from '../site/.vitepress/theme/utils/article-contributors'
import { getArticleHistory } from '../site/.vitepress/config/article-history'

const root = fileURLToPath(new URL('../', import.meta.url))
const require = createRequire(import.meta.url)
const componentPath = join(root, 'site/.vitepress/theme/components/ArticleContributorCard.vue')
const { descriptor } = parse(readFileSync(componentPath, 'utf8'))
const compiled = compileScript(descriptor, { id: 'article-contributor-card', inlineTemplate: true, templateOptions: { ssr: true } })
const code = stripTypeScriptTypes(compiled.content).replace(/from (["'])(vue(?:\/server-renderer)?)\1/g, (_, quote, name) => `from ${quote}${pathToFileURL(require.resolve(name)).href}${quote}`)
const { default: Card } = await import(`data:text/javascript;base64,${Buffer.from(code).toString('base64')}`)
const css = readFileSync(join(root, 'site/.vitepress/theme/article-contributors.css'), 'utf8')
const source = matter(readFileSync(join(root, 'documents/getting-started/07-wsl2-environment.md'), 'utf8')).data
const hypothetical: ContributionPerson[] = [
  { github: 'Charliechen114514', roles: ['author'], note: { zh: '编写初版与串口示例（假想）', en: 'Initial article and UART examples (fictional)' } },
  { github: 'xiaoshuaijie', roles: ['revision'], note: { zh: '补充练习参考答案（假想）', en: 'Added exercise solutions (fictional)' } },
  { github: 'owollz4', roles: ['revision'], note: { zh: '修复代码注释与边界说明（假想）', en: 'Fixed comments and boundary explanations (fictional)' } },
  { github: 'YukunJ', roles: ['translation'], note: { zh: '翻译英文版并统一术语（假想）', en: 'English translation and terminology (fictional)' } },
]
const escape = (s: string) => s.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;')
const samples = [
  { title: source.title, people: source.contributors, lang: 'zh' as const, path: 'getting-started/07-wsl2-environment.md', text: '真实样例 · 原稿投稿与技术校订', id: 'wsl-zh' },
  { title: 'Want Real Linux — WSL2 + VS Code Environment Setup', people: source.contributors, lang: 'en' as const, path: 'en/getting-started/07-wsl2-environment.md', text: 'Real example · English interface and inherited source credits', id: 'wsl-en' },
  { title: '串口通信：从轮询到中断', people: hypothetical, lang: 'zh' as const, path: '', text: '假想样例 · 四人协作，用于验证布局与多角色扩展', id: 'four-people' },
]
const html: string[] = []
for (const sample of samples) {
  const data = formatArticleContributions(sample.people, sample.path ? getArticleHistory(sample.path) : [], sample.lang)
  data.contributors.forEach(person => { person.anchor = `${sample.id}-${person.anchor}` })
  const meta = (await renderToString(createSSRApp({ render: () => h(Card, { data, variant: 'meta' }) }))).replaceAll('card-contributors-heading', `${sample.id}-heading`)
  const full = await renderToString(createSSRApp({ render: () => h(Card, { data }) }))
  html.push(`<article lang="${sample.lang}"><p class="preview-label">${escape(sample.text)}</p><h1>${escape(sample.title)}</h1>${meta}<div class="preview-prose"><p>${sample.lang === 'zh' ? '这里是教程正文。页头只显示小头像，点击可跳到文末对应的贡献者。' : 'Article content goes here. Small avatars link to each contributor’s signature below.'}</p><pre><code>$ code .</code></pre><p>${sample.lang === 'zh' ? '正文结束后，署名区显示每个人的具体贡献。历史记录来自实际 Git 提交。' : 'The signature below records each person’s contribution. History comes from actual Git commits.'}</p></div>${full.replaceAll('card-contributors-heading', `${sample.id}-heading`)}</article>`)
}
const output = join(root, 'design/article-contributors/preview.html')
mkdirSync(dirname(output), { recursive: true })
writeFileSync(output, `<!doctype html>
<html lang="zh-CN"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>文章贡献卡预览</title><style>
:root{color-scheme:light;--vp-c-bg:#FCFAF6;--vp-c-bg-soft:#F4F0E9;--vp-c-bg-elv:#FFFDF9;--vp-c-text-1:#2A2420;--vp-c-text-2:#63574e;--vp-c-divider:#ddd6cc;--vp-c-brand-1:#2563EB;--vp-c-brand-soft:rgba(37,99,235,.1)}
.dark{color-scheme:dark;--vp-c-bg:#17120E;--vp-c-bg-soft:#1F1812;--vp-c-bg-elv:#241C15;--vp-c-text-1:#E8D8CA;--vp-c-text-2:#c0ab98;--vp-c-divider:#493a2d;--vp-c-brand-1:#60A5FA;--vp-c-brand-soft:rgba(96,165,250,.16)}
*{box-sizing:border-box}body{margin:0;background:var(--vp-c-bg);color:var(--vp-c-text-1);font-family:system-ui,sans-serif;line-height:1.7}.preview-toolbar{display:flex;align-items:center;justify-content:space-between;gap:12px;max-width:808px;margin:auto;padding:20px 24px;border-bottom:1px solid var(--vp-c-divider);font-size:13px}.preview-toolbar button{border:1px solid var(--vp-c-divider);border-radius:6px;background:var(--vp-c-bg-soft);color:var(--vp-c-text-1);padding:6px 12px;cursor:pointer}article{max-width:760px;margin:36px auto 64px;padding:0 24px}h1{font-size:clamp(22px,4vw,30px);line-height:1.35;margin:12px 0 20px}.preview-label{font-size:12px;color:var(--vp-c-text-2)}.preview-prose{font-size:15px}.preview-prose pre{padding:16px;border-radius:8px;background:var(--vp-c-bg-soft);overflow:auto}a{color:var(--vp-c-brand-1)}
${css}
</style></head><body><div class="preview-toolbar"><span>文章贡献卡 · HTML + CSS 静态成品</span><button type="button" id="theme" aria-pressed="false">切换暗色</button></div>${html.join('\n')}<script>document.getElementById('theme').addEventListener('click',function(){const dark=document.documentElement.classList.toggle('dark');this.setAttribute('aria-pressed',String(dark));this.textContent=dark?'切换亮色':'切换暗色';});</script></body></html>`)
console.log(output)
