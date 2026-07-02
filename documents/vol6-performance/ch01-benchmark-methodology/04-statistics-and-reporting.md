---
title: "统计与报告:把分布变成结论"
description: "测出来一组数之后怎么办——为什么性能数据报中位数+置信区间而非单均值、为什么几乎不正态所以 Mann-Whitney 比 t-test 靠谱、A/B 比较的正确姿势,以及为什么不能用 micro 结论给 macro 背书"
chapter: 1
order: 4
tags:
  - host
  - cpp-modern
  - intermediate
  - 优化
  - 测试
difficulty: intermediate
platform: host
reading_time_minutes: 7
cpp_standard: [11, 17]
prerequisites:
  - "怎么写一个可信的 microbenchmark"
  - "测量陷阱与环境就绪:16 条 checklist"
related:
  - "生产测量与 CI 性能回归"
---

# 统计与报告:把分布变成结论

## 测出来一堆数,然后呢

ch01-01 到 ch01-03 让你能拿到一组「不是空壳、姿势对、环境干净」的性能数字。但你拿到的从来不是一个数,而是一组数——一个分布。ch01-01 开篇就说了:性能不是布尔量,是分布。这一篇就回答最后一个问题:这组分布,怎么变成一个可信的结论——「A 比 B 快 X%,而且这个快是真的、不是噪声」?

这件事的本质是统计推断。听起来吓人,但你要掌握的其实就几条,而且每条都对应一个你会真实犯的错误。

## 报什么、不报什么

先立死规矩:

- **必报**:中位数(median)、离散度(IQR 四分位距,或 95% 置信区间 CI)、样本数、环境快照(内核 / CPU / governor / `perf_event_paranoid`)。GBench 用 `Repetitions` + `ReportAggregatesOnly(true)` 会自动给你 `mean` / `median` / `stddev` / `cv`,我们在 ch01-02 跑出来的那张表就是。
- **禁报**:单次均值。一次跑出来的 `mean` 不是结论,是一个样本。

为什么把「单次均值」单拎出来禁?因为性能数据是**右偏**的:正常的快,偶尔一次扩容、一次被调度走、一次 cache miss,就拖出一个长尾。均值被长尾往上拉,中位数岿然不动。两组数据明明「A 的典型表现更好」,却因为 B 的长尾更少,均值上反而 B 占优——结论就反了。

## 为什么中位数比均值靠谱

Bakhvalov 在《Performance Analysis and Tuning on Modern CPUs》§2.4 有一张很说明问题的图:两个版本 A 和 B 的性能测量**画成分布**,两条曲线高度重叠。A 的峰值(最可能出现的耗时)比 B 更靠左(更快),看起来 A 赢。但因为分布有重叠,**「A 比 B 快」只对某个概率 P 成立**——总有一些样本里 B 反而快。你采一个样本,有可能正好落在 B 快的那一段。

这件事的直接推论是:

- 不要凭一两个样本下结论。你要的是分布的对比,不是点估计的对比。
- 用**中位数**代表「典型表现」,用**离散度**(IQR / 95% CI / cv)代表「这个典型有多稳」。cv(`stddev/mean`)< 1% 很稳;> 5% 这组数本身不可信,先回去查噪声源(ch01-03),别急着下结论。
- 看分布形状本身。如果分布是**双峰**(两个峰),说明你的 benchmark 里混了两种行为——典型的比如缓存命中与未命中两条路,或者锁竞争与不竞争。Bakhvalov 提醒:双峰分布不是噪声,是信号,说明你该把两种场景拆开分别测,而不是糊在一起取中位数。

## 假设检验:t-test 还是 Mann-Whitney U

「A 比 B 快 12%,这个 12% 是真的吗?」——这是个统计问题,叫**假设检验**(hypothesis testing)。思路是:先假设「A 和 B 没差别」(零假设),然后看手里这组数据在零假设下有多不可能。如果足够不可能(p 值低于阈值,通常 0.05),就说「差别显著」,拒绝零假设。

选哪个检验,取决于数据分布长什么样:

- **Student's t-test**(参数检验):假设数据服从**正态分布**。算简单,教科书默认教这个。
- **Mann-Whitney U**(非参数检验):不假设分布形状,只比两组数据的秩(排序后谁大谁小)。

关键点来了,直接引 Bakhvalov 在 §2.4 的原话大意:**性能测量数据里,正态分布几乎从不出现**。性能数据通常是偏态的、有长尾的、甚至多峰的。所以那套假设正态的教科书公式(包括 t-test)在性能场景下要谨慎用——他特意点了这句,因为太多人默认套 t-test。

实践建议:做 A/B 显著性判断,**默认用 Mann-Whitney U**(非参数,稳);只有当你先做了正态性检验、确认数据确实近似正态时,才用 t-test。Python 的 `scipy.stats.mannwhitneyu` / R 的 `wilcox.test` 都能直接算。

## A/B 比较的正确姿势

把上面几条拼起来,一个可信的「A 比 B 快」结论,要满足:

1. **同一环境**:同一台机器、同一 governor、同一负载,只改你要比的那一处。别在两台机器上分别测 A 和 B。
2. **同一二进制**:理想情况是一个二进制里编译期开关切换 A/B,避免不同编译带来的布局偏置。
3. **多次重复**:A 测 N 次,B 测 N 次(N ≥ 30 最好),各得一个分布。
4. **报效应量,不只 p 值**:p 值只告诉你「差别是不是真的」,不告诉你「差别多大」。一个「p<0.05,快了 0.3%」的结论,统计显著但工程上没意义。要报「快 12%(95% CI [10%, 14%], p<0.01)」这种完整的说法。
5. **跑不止一轮**:跨天、跨 warmup 状态再跑一轮确认,防止这一轮环境漂了。

这一套在 CI 里自动化的事(ch01-05 讲),就是持续地做这种 A/B,自动判回归。

## micro vs macro:最后再说一遍

因为它最致命,值得这一章每一篇都拎出来讲一次。

**禁止用 microbenchmark 的结论去给生产性能背书。** 一个 microbenchmark 里测出 IPC=2、cache 全命中的函数,在真实负载里完全可能因为 cache miss 变成 IPC=0.3。这不是「micro 不准」,是 micro 测的本来就是「理想条件下能跑多快」,而生产里没有理想条件——这两件事是两种语言,不能直接换算。

Bakhvalov §2.4 的例子里,「分布对比」是在同一类场景下做的(都 micro 或都 macro)。跨场景对比时,你要么把 micro 的结论限定在「这个函数的相对改进方向」上,要么干脆去做宏观测量(生产 telemetry / 宏观负载 benchmark)。后者是 ch01-05 的事。

## 参考资源

- Bakhvalov, D. 《Performance Analysis and Tuning on Modern CPUs》§2.4 *Manual Performance Testing*(分布对比、双峰、假设检验、「性能数据几乎不正态」的原话)
- Feitelson, D. G. 《Workload Modeling for Computer Systems Performance Evaluation》(Bakhvalov 推荐的性能统计专门参考,模态分布、偏度等)
- Wikipedia:[Mann–Whitney U test](https://en.wikipedia.org/wiki/Mann%E2%80%93Whitney_U_test)、[Student's t-test](https://en.wikipedia.org/wiki/Student%27s_t-test)
- 本卷 ch01-01(micro vs macro 的根)、ch01-02(`ReportAggregatesOnly` 的 `mean`/`median`/`stddev`/`cv`)
