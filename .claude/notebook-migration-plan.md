# NoteBookProject 迁移计划(2026-06-22 重扫重建)

> 原计划文档已丢失,本轮用 workflow(36 agent / 1.23M tokens)对 `~/NoteBookProject` 三领域重新逐篇扫描 + 对照本仓库现状判定,重建本计划。
> 完整逐篇 JSON(含 key_points / risks 全文):`/tmp/claude-1000/-home-charliechen-Tutorial-AwesomeModernCPP/e75cfacb-1bae-43ff-b16f-610f309d302b/tasks/wb20h9am9.output`

## 总览

| 领域 | 目标卷 | 可迁 | 预估行 | skip | 价值 |
|---|---|---|---|---|---|
| 网络编程 | `vol8-domains/networking/`(纯空占位) | 10 | ~4470 | 1 | ★★★ 全仓最大空白 |
| STL 算法/迭代器 | `vol3-standard-library/` | 3 | ~1570 | 6 | ★★ 填 `<algorithm>`/ranges 系统空白 |
| 内存安全 ASan | `vol6-performance/` + `vol7-engineering/` | 7 | ~3150 | 0 | ★★★ sanitizer 工具链整块空白 |
| **合计** | | **20** | **~9190** | **7** | |

**已落地**:批次 1(vol4 设计模式 21 篇 + 21 配套工程)—— 不在本计划内,详见记忆 `notebook-migration-plan`。

---

## 领域一:网络编程 → `documents/vol8-domains/networking/`

### 仓库现状(gap)
`networking/` 仅 `index.md` 占位("规划中/预计 13 篇/内容编写中"),实际 0 篇。全仓无网络编程系统性内容。**与 vol5 分工**:vol5-concurrency/ch06 已系统覆盖 epoll+协程事件循环主线,故 vol8 聚焦「基础 API + 工业级库(Asio)+ 设计模式 + 跨平台对照」,不做 epoll API 细节重复,须 cross-link vol5。

### 迁移篇目(10 篇,建议编号 01–10)

| # | 源标题(简) | 价值 | 难度 | 行数 |
|---|---|---|---|---|
| 01 | Libevent 学习小记:同步 C/S 基础 | A | medium | 400 |
| 02 | Socket 封装系列(1):系统 API 详解 | A | medium | 450 |
| 03 | Boost.Asio(2):TCP CS 样例 | A | medium | 450 |
| 04 | Boost.Asio(3):UDP 无连接 | B | medium | 420 |
| 05 | 用 Linux API 获取网卡信息(getifaddrs) | A | medium | 450 |
| 06 | 异步 IO 编程:IO 多路复用与 Epoll 入门 | A | medium | 450 |
| 07 | Socket 封装系列(3):Reactor 设计模式 | A | medium | 420 |
| 08 | Socket 封装系列(2):ServerSocket/Client 抽象接口 | B | medium | 420 |
| 09 | Socket 封装系列(4):基于 Epoll 的框架思路 | A | medium | 480 |
| 10 | 深入理解 Windows IOCP(完成端口) | A | medium | 480 |

**逐篇要点 / 风险**:

- **01-sync-socket-basics** — 同步阻塞两条流水线(server socket→bind→listen→accept→rw、client socket→connect→rw),sockaddr_in 三件套(族/htons/inet_pton),每连接一线程的扩展性瓶颈引出事件驱动动机。⚠ 代码纯 C(gcc/裸 fd/atoi 无检查),需 RAII fd + `std::from_chars` 替 atoi + `std::expected`。
- **02-socket-api-foundations** — 服务器五步、socket() 三参数语义、字节序、TIME_WAIT/SO_REUSEADDR。**核心纠偏点**:`listen()` backlog 控制的是全连接(accept)队列上限,非半连接 SYN 队列;三次握手内核完成,accept 只取已完成。⚠ `socket()==0` 判断失败应为 `<0`、`inet_ntoa` 线程不安全换 `inet_ntop`;backlog 语义/SOMAXCONN(511)需 web search 核实。
- **03-boost-asio-tcp-basics** — TCP CS 骨架(resolver→connect→read_some 处理 EOF),同步→异步演进(`enable_shared_from_this`+`async_accept` 回调链),`std::chrono`+`std::format` 拼 daytime。⚠ 源第 59 行 `std::string res = buf.data()` 忽略 read_some 返回 len 是真 bug,改写必须 `(buf.data(), len)` 截断;异步 lambda 未 self-pin 需注明。
- **04-boost-asio-udp** — UDP 无连接模型(open+send_to/receive_from),异步 `async_receive_from`+`make_shared` 保活+递归维持接收。⚠ 端口 13 特权需 sudo 换 >1024;异步 `remote_endpoint` 是共享成员,多客户端 data race 需指正;依赖 Boost.Asio 非 std(注明现状)。
- **05-local-interface-probing** — `getifaddrs`/`freeifaddrs` 遍历 `ifaddrs` 链表取 IP/掩码/标志,IFF_* 判读,`inet_ntop` 处理 v4/v6;MAC 需另开 socket+ioctl SIOCGIFHWADDR(数据链路 vs 网络层分界)。⚠ 事实 bug:else 分支把 `sa_data` 原始字节塞 std::string 对 AF_PACKET 是垃圾;需 `freeifaddrs` 封进 `unique_ptr` deleter。
- **06-linux-io-multiplexing-epoll** — 阻塞轮询动机→两种异步实现(用户态事件循环 select/poll/epoll vs 内核完成 IOCP/io_uring)→epoll 注册/通知分离(兴趣红黑树+就绪队列避 O(n))。ET 须非阻塞+循环读写到 EAGAIN。Echo Server 实战。⚠ **须重定位为 vol8 独立篇避 vol5 重复并 cross-link**;代码 C 风格需 RAII fd wrapper;`epoll_wait -1 是阻塞`非"退化为同步";EPOLLEXCLUSIVE 引用 SO 换 man7 一手来源;busy-poll/`epoll_params` 较新需核实内核版本+CAP_NET_ADMIN。
- **07-reactor-pattern-with-epoll** — Reactor 三角色(分发器/Handle/Event Handler)+ 四步流程;Reactor=同步非阻塞(内核通知就绪,应用 rw),`epoll_wait` 即 Synchronous Event Demultiplexer;Proactor 对照(异步,IOCP/POSIX AIO);对比表 + 为何 Linux 选 Reactor。⚠ 过时断言"Proactor 在 Linux 需 io_uring/POSIX AIO"——io_uring(5.1+)已成熟需更新;三角色命名对 POSA2 经典定义核对。
- **08-socket-wrapper-api-design** — `std::function` 回调簇解耦业务、pImpl(`unique_ptr<ServerSocket>`)+前向声明隐藏跨平台实现、`= delete` 拷贝构造独占所有权。⚠ 代码不可直接编译(依赖未随文头);设计槽点:回调里 `dynamic_cast` 下转取 fd 破坏 pImpl 抽象,改写需重设计;论述浅需补 ABI/线程安全论证。
- **09-epoll-reactor-framework** — epoll 集成进自建 Socket 库骨架(配置→监听→init_epolls→workloop→handle_new_connections/react_clients/close);工程要点 LT/ET、非阻塞 fd、EINTR 重试/EAGAIN 非错/EPOLLONESHOT 需 MOD/close 前先 DEL;客户端非阻塞 connect+EINPROGRESS。⚠ "红黑树查询 O(1)"表述不严谨(树增删 O(log n),就绪链表取事件才 O(1));广播复用 receiving_callback 职责混淆需作设计讨论点;裸 POSIX fd 需 RAII;需补配套 `code/` 工程。
- **10-windows-iocp-proactor** — IOCP=Completion 完成驱动(发起 Overlapped I/O 不阻塞,OS 完成投递到端口,工作线程 GetQueuedCompletionStatus 取),Proactor vs Reactor 对照组;五步核心 + PostQueuedCompletionStatus 关停。⚠ 代码非现代 C++(裸 new/delete、inet_addr 过时、无 WSAGetLastError);多客户端 Demo 有真 bug(running=false 后 accept 仍阻塞、listen socket 未关致 join 挂起)需修优雅关闭;Windows 平台需交代编译方式(cl/MinGW)。

### 待补评估(2 篇,429 未完成)
本轮 deep-read 因 429 失败,迁移前需补精读:
- `Boost ASIO 库深入学习（1）.md` — 很可能是(2)TCP 篇的前置(resolver/connect 基础),写 03 篇时一并参考。
- `理解CC++异步编程——但是是同步的Socket编程基础.md` — 疑与 01-sync-socket-basics 重叠,核实后定合并或 skip。

### skip(1 篇)
| 源 | 原因 |
|---|---|
| 协程迷你 Echo Server | 与 `vol5/ch06/05-coroutine-echo-server`(1269 行)+`04-async-io-and-event-loop`(574 行)高度重复,且源硬依赖 4 个 CSDN 外链会断。归 vol5 已覆盖。 |

---

## 领域二:STL 算法/迭代器 → `documents/vol3-standard-library/`

### 仓库现状(gap)
容器全套(01–10)+char8_t(30)+迭代器基础(40)共 1 篇迭代器。**零篇 `<algorithm>`/ranges 系统章节**:无 ranges 算法/Niebloid/fold 家族/C++23 适配器(zip/chunk/slide/stride/repeat)/contains/find_last 系统讲解,也无迭代器适配器专题(40 篇末尾预告未交付)。vol1/ch11/03-algorithms-intro 仅入门级,vol4 ranges 两篇聚焦通用概念属泛型 track,均不填 vol3「标准库算法」空白。

### 迁移篇目(3 篇)

| # | 源标题(简) | 价值 | 难度 | 行数 |
|---|---|---|---|---|
| 41 | STL CookBook 7:迭代器简单论 | B | heavy | 350 |
| 42 | STL CookBook 6:保持 vector 有序的插入 | B | medium | 320 |
| 43 | C++优质博客总结:Ranges 算法+fold+适配器 | A | medium | 900 |

- **41-iterator-adapters** — 兑现 vol3/40 末尾承诺。三类适配器:插入(`back_inserter`/`front_inserter`/`inserter`)、流(`ostream/istream_iterator` 配哨兵)、反向(`rbegin/rend`);`copy+back_inserter` 把"算法不扩容"转 push_back;适配器本质(赋值即 push_back)。务必用 `ranges::copy+back_inserter` 或 views 重写老式 `copy(beg,end,...)`。⚠ 大量事实/代码错误须修正:`vector<vector<int,string>>` 非法(第二参数是分配器)、`istream_adapter` 应为 `istream_iterator`、FibGenerator `end()` 返回局部引用是悬垂 UB、zip_iterator 代码不完整;4 张截图须用 mermaid/代码表重建。
- **42-keep-vector-sorted-on-insert** — `ranges::lower_bound` 找位+`insert` 把 O(n) 扫描压成 O(log n) 查找(移位不可避免),配 `is_sorted` 前置断言;无序场景"末尾 move 到待删位+pop_back" O(1) 删任意下标(破坏顺序)。⚠ 源码错:`opmap.find(op)` 误写 m、`hash<Coord>` 与 struct Position 不一致、`<algorithms>` 应 `<algorithm>`;list 有成员 insert 无需此技法,表述需改。
- **43-ranges-algorithms-and-adaptors-cpp23** — 大头篇(900 行)。Ranges 算法三进步(Range 参数/Concept 拒绝/Niebloid 阻 ADL)+`<numeric>` Range 化(reduce 为何暂不);**fold 家族六名十二重载**(first/last 返 optional、无 projection、修复 accumulate 返回类型缺陷);C++23 适配器 15 个(zip/adjacent/chunk/slide/stride/repeat/as_rvalue/join_with/chunk_by 等)设计取舍;contains/find_last 方便 wrapper。⚠ **源文 2022 落定期,GCC/Clang/MSVC 实现状态标注已严重过时,必须按 `/verify-claim` 实测当前编译器**;2900 行口语长文需重构;zhihu 链换 cppreference/wg21;ranges::to 与 vol3/10 重叠须裁剪;cpp_standard 字段注意本仓不支持 26(核对 tags.py)。

### skip(6 篇——主动去重,判断准确)
| 源 | 与谁重复 |
|---|---|
| STL CookBook 4:range 创建 view | vol4 ranges 两篇(545+609 行)+vol10 cppcon ranges |
| STL CookBook 9:常见算法 | vol1/ch11/03(且源 copy_if 预分配是反模式) |
| STL CookBook 10:copy/merge/join | vol1/ch11/03 + vol4/18-iterator(join_with 已解决其增量) |
| STL CookBook 5:span/结构化绑定/折叠表达式 | vol3/08-span + vol2 结构化绑定/CTAD + templates/fold 全覆盖 |
| STL CookBook 8:Lambda 系列 | vol2/ch03-lambda 全章 5 篇更严谨 |
| C++八股:STL 容器与算法 | vol3/40 迭代器 + vol1/ch11 Introsort + vol3/03 迭代器失效 |

---

## 领域三:内存安全 ASan → `vol6-performance/` + `vol7-engineering/`

### 仓库现状(gap)
vol6/vol7 对 asan/sanitizer/内存安全/valgrind **grep 零命中**。现有提及均浅层顺带:vol1/ch12-02、c_tutorials/14、11 只给一行 `-fsanitize=address`;vol5/01 只深论 TSan。无一篇系统讲 shadow memory 编码、红区、三段式报告、工具家族选型、ASan vs Valgrind vs UBSan 对照、MSVC 侧检测。

> ⚠ 目标卷现有 order 段位未核实,下表编号(slug 前缀)迁移前需核对 vol6/vol7 现有文件后确定。

### 迁移篇目(7 篇)

| 建议 slug | 源标题(简) | 价值 | 难度 | 行数 | 目标卷 |
|---|---|---|---|---|---|
| asan-family-and-memory-safety | Linux 学习之路4(KASAN/UBSAN 节) | A | heavy | 420 | vol6 |
| address-sanitizer-in-practice | ASan 实践:几个常见错误 | B | medium | 450 | vol7 |
| msvc-memory-safety-asan | VS 下的内存安全检测 | A | medium | 480 | vol7 |
| memory-safety-asan-valgrind | 嵌入式 C:valgrind 子教程 | B | heavy | 450 | vol6 |
| sanitizer-toolchain-and-memory-safety | Linux 学习之路3(KASAN 工具集节) | C | heavy | 450 | vol6 |
| undefined-behavior-and-sanitizers-deep-dive | Linux Debug 学习之路(sanitizer 深论节) | B | heavy | 450 | vol7 |
| unit-testing-with-catch2 | 系统入门测试1:Catch2 | B | medium | 450 | vol7 |

- **asan-family-and-memory-safety**(认知地基,先写)— ASan 起源(Google 为 GCC/Clang 实现)+Heartbleed 案例(抓 buffer overread);原理三件套(CTI 插桩/shadow memory 1:8 映射/2-4x 开销 vs Valgrind 20-50x);工具家族(ASAN/LSAN/MSAN/TSAN/UBSAN 各管一类)+互斥关系(ASan↔TSan,呼应 vol5 TSan 篇);UBSan 捕获 UB 清单。⚠ 源 90% kernel-space(KASAN/KUnit)须大幅裁剪为 host 用户空间 C++ 视角;机翻术语错乱("免费使用"=UAF);5 张截图须重制;版本数字(clang 11 全局 OOB、KASAN ARM32 5.11)需核验。
- **address-sanitizer-in-practice**(实战,紧随)— 三段式报告解读(出错代码/内存区间/检测器状态);shadow 编码(00 可访问/fa 红区/fd 已释放/f5 栈返回/f9 全局红区);红区机制(malloc 左右各 16B);三大经典错误(heap-buffer-overflow/UAF/stack-use-after-return)。⚠ 代码全 C 风格须平行补 C++ 示例;`detect_stack_use_after_return=1` 源未提需补;shadow legend 逐字贴旧版 GCC 须核对当前。
- **msvc-memory-safety-asan**(MSVC 对照)— 三条渠道:CRT Debug Heap(`_CRTDBG_MAP_ALLOC`+`_CrtSetDbgFlag`+`_CrtDumpMemoryLeaks`+`_CrtSetBreakAlloc`)/Diagnostic Tools 堆快照 Diff/MSVC ASan(`/fsanitize=address`)。⚠ 截图须重制;"VS2026 必须强制 report mode"需 verify-claim;与 GCC 篇协调避免重复 ASan 基础原理;`_CrtSetBreakAlloc` 序号跨运行漂移需提示。
- **memory-safety-asan-valgrind** — Valgrind 五工具职责(memcheck/callgrind/cachegrind/helgrind+drd/massif);memcheck A-bit/V-bit 双表原理;六类经典错误;**真正 gap:补 ASan**(源完全无),做编译期插桩 vs JIT 解释对照。⚠ 6 案例全是 .webp 截图无源码须全重写;valgrind 3.12 安装流程过时应删;与 vol1/c_tutorials/14 Valgrind 段重叠须收敛为交叉引用;helgrind"实验阶段"断言过时须改。
- **sanitizer-toolchain-and-memory-safety**(C 级,最后)— 用户态 `-fsanitize=*` ↔ 内核 KASAN/KMSAN/UBSAN/KCSAN 映射;KFENCE(采样,可上生产)+DAMON(5.15)分层防御。⚠ 纯叙述零代码须全重写为 C++ 用户态;版本(KFENCE 5.12/KMSAN 实际 5.14)需核验;工具汇总表是外部 PNG 须自画;kernel tooling 偏离 C++ 定位只能作延伸。
- **undefined-behavior-and-sanitizers-deep-dive**(⚠ 版权高风险,末批)— Regehr/Cuoq《UB in 2017》框架:UB 分类(空间/时间内存安全/整数溢出/严格别名/对齐/竞争/unsequenced)三段式;ASan vs Valgrind 红区(Valgrind 无法给栈变量插红区);**核心陷阱:ASan 被 -O2 击穿**(temporal.c -O0 能抓、-O2 UB 被优化掉 sanitizer 哑火,sanitizer 没报≠没 UB)。⚠ **源是名文近乎全文照搬(连评论区/署名块保留),必须重写 + 显式标注原作者/原文链接,不可搬运**;编译器版本(clang-4.0/gcc-4.8)严重过时;`detect_stack_use_after_return` clang 11+ 已默认;代码全 C 须 C++17+ 重写。
- **unit-testing-with-catch2**(弱相关,末尾)— divide(a,b) 从 assert 演进到框架;测试三方面(功能/边界异常/语义契约);Catch2 v3(TEST_CASE/SECTION/REQUIRE/GENERATE);TDD 反向思考逼出接口设计。⚠ 语法瑕疵(`int a == divide(3,2)`、形参类型写进调用)须修;与 ASan 主题弱相关,ASan 专属批次优先级最低。

### skip(0 篇)
领域内无重复(整块空白)。

---

## 跨领域批次顺序(推荐)

| 批次 | 内容 | 篇数 | 理由 |
|---|---|---|---|
| **B1** | vol3 算法迭代器 | 3 | 前置依赖优先(ranges/迭代器是网络服务器代码与容器协作的通用前置),3 篇成体系一次性清最经济。41-iterator-adapters 兑现承诺优先;43-ranges 价值最高但须 verify-claim 实测当前编译器。 |
| **B2** | vol8 networking 基础+生态 | 8 | 由浅入深:同步 socket→API 语义纠偏→Asio TCP/UDP→网卡探测→epoll 多路复用→Reactor 模式→封装接口。填空白价值大、彼此构成认知链。 |
| **B3** | vol8 networking 框架+跨平台 | 2 | 依赖 B2:epoll Reactor 框架骨架 + IOCP Proactor 对照,形成 Reactor/Proactor 闭环。 |
| **B4** | vol6/vol7 内存安全体系 | 4 | 工具链地图先行:ASan 家族→实战→MSVC 对照→Valgrind 对照。构成 sanitizer 主线。 |
| **B5** | vol7 深论+延伸+单测 | 3 | UB 深论(版权高风险重写)+kernel tooling 延伸(C 级)+Catch2(弱相关)。风险高/价值低故靠后。 |

---

## 通用风险与改写纪律(三领域一致)

1. **事实过时(最大共性风险)**
   - algorithm:ranges 篇源 2022,GCC/Clang/MSVC 实现状态标注已严重过时 → `/verify-claim` 实测当前编译器
   - asan:编译器版本(clang-4.0/gcc-4.8)、内核版本(KASAN/KFENCE/DAMON/KMSAN 合入)、`detect_stack_use_after_return` 默认行为(clang 11+ 已默认)→ 逐条核验
   - networking:`epoll_params`/busy-poll、EPOLLEXCLUSIVE 引用 SO → 换 man7/内核文档一手;backlog 语义核实 2.6+ 现况
2. **来源质量**:口语/八股/机翻/截图依赖/Regehr 名文照搬 → 按 writing-style 重构(实验口吻、可编译可跑、贴真实终端输出);截图全本地重制或改 mermaid/终端输出;外链(zhihu/CSDN)换 cppreference/wg21/man7
3. **代码现代化**:全部上 C++17/20/23 —— RAII(`unique_fd`/`ScopedFd`)、`std::span`、`std::expected`、`std::byte`、ranges;networking 几乎全 C 风格需 RAII 化(依赖 vol1/vol3 智能指针/RAII 章节,cross-link)
4. **版权署名**:UB 深论篇(Regehr/Cuoq 照搬)必须重写 + 显式标注原作者/原文链接
5. **跨卷重复**:networking 与 vol5 严防重复(cross-link vol5,不做 epoll 细节重复);algorithm 主动 skip 6 篇已去重
6. **frontmatter**:`cpp_standard` 字段本仓仅支持 `[11,14,17,20,23,26]` 子集(26 曾踩坑,核对 `scripts/tags.py`);slug 编号需与目标卷现有 order 协调

## 单篇执行流水线(沿用)

`/new-article`(按骨架生成 frontmatter + 章节)→ 改写(项目声音 + 实验口吻 + 演进式叙事)→ `/tmp` cmake 编译验证贴真实输出 → `/verify-claim`(核实过时断言)→ `/audit`(事实+严谨双审)→ 钉 `.cache/translations/manifest.json` `engine: manual`(暂不做 EN)→ `/preflight`(frontmatter/lint/链接/索引)

## 决策记录

- **暂不做 EN**:新增 CN 文章在 manifest 钉 `engine: manual`(参照 roadmap 先例),避免 `translate.py --all` 误翻。
- **批次 1(vol4 设计模式)已完成**:21 篇 + 21 配套工程,不在本计划。
