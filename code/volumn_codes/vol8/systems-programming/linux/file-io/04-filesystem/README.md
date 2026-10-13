# 04-filesystem 配套实验

《std::filesystem:目录与元数据》一文的实验代码与原始输出存档(文章施工中,先入册实验)。六个目录对应六组实验(E1~E6),`common/fsio.hpp` 是系列契约工具(`errno_code` 沿用 01-posix-file-io.md 的形态,新增 `unique_dir`:opendir 拿到的 `DIR*` 交给 RAII 管)。

## 环境(全部 .out 由此环境捕获)

| 项 | 值 |
|---|---|
| CPU | AMD Ryzen 7 9700X 8-Core Processor |
| 内核 | 6.18.33.2-microsoft-standard-WSL2(WSL2) |
| 编译器 | g++ (GCC) 16.2.1 20260810,`-std=c++20`(filesystem 已入 std,无需 `-lstdc++fs`) |
| 数据盘 | ext4(`/home` 所在 VHDX,实测 `df -T`:ext4) |
| strace | 7.2 |
| 用户 | 非 root(uid 1000,E3 的 EACCES 用例依赖这一点) |

## 每实验一句话结论

| 目录 | 实验 | 结论 |
|---|---|---|
| `01-iteration/` | E1 三件套遍历 | libstdc++ 完全不排序:fs 迭代序 == 手搓 readdir 序(ext4 按名字散列的确定序,非创建序非字典序,跨进程跨工具稳定) |
| | E1 符号链接环 | 默认不跟随目录链接,环进不去;`follow_directory_symlink` 下 libstdc++ 不检测环,同一目录转 41 圈、keep.txt 被访问 41 次,最后靠内核 40 层符号链接上限(ELOOP,且被库吞掉不报 ec)才停 |
| | E1 属性缓存 | `directory_entry` 只缓存类型(readdir 的 d_type,零 stat);`file_size()` 不缓存,每条一次 newfstatat |
| `02-error-forms/` | E2 双形态 | 同一 ENOENT:抛异常版给 `filesystem_error`(what/code/path1 全套),ec 版给 `ec==2` + 收尾迭代器;非成员函数里 `exists` 连 ec 都清零、`is_directory` 返 false 但留 ec、`status` 给 not_found 类型、`file_size` 视为错误返哨兵 —— 四种态度 |
| | E2 迭代中删目录 | 平铺迭代:readdir 用户态缓冲里的陈旧名单照吐(1022/3001,increment 零失败),不缓存的属性查询 938 条 ENOENT,缓冲尽后 getdents64 返回 EOF 静默截断;递归迭代:下降打开已删子目录时 `increment(ec)` 真报错 ENOENT |
| `03-perms-space/` | E3 权限与空间 | `permissions(p, owner_exec, add)` 生效(`access(X_OK)` 对拍);`symlink_status` 看 0777 的链接自身、`status` 跟随看目标;`nofollow` 改链接权限在 Linux 报 EOPNOTSUPP(ec=95),默认跟随则会改到目标;`space().free-available` = 5.09% 恰是 ext4 root 预留 |
| `04-bench/` | E4 性能 | 10000 文件:手搓 readdir 2.16 ms,directory_iterator 3.42 ms(path 构造的包装成本),+`is_regular_file` 3.43 ms(d_type 缓存免费),+`file_size` 16.22 ms(每条一次 stat,strace 计数 10005);directory_iterator 的 syscall 序列与手搓完全同形:openat + getdents64×11 + close |
| `05-path-pitfalls/` | E5 path 坑 | 右侧绝对路径顶掉左侧(`"base"/"/abs"` == `"/abs"`);尾单斜杠吸收、双斜杠残留(`"base//"/"leaf"` == `"base//leaf"`);拼空串添尾斜杠;`lexically_normal("./a/../b")=="b"` 纯词法;Linux 上 `value_type=char`、`c_str()` 是 `const char*`(Windows 是 wchar_t,镜像篇对拍点);迭代 `"a/b/"` 产尾部空元素 |
| `06-tree-diff/` | E6 双树 diff | 存在性+size+mtime 三判据的清单与 `diff -rq` 一一对应(整目录折叠同粒度);keep.txt 暴露经典假阳性(内容同、mtime 漂 -> 误报 M,diff 不报)—— rsync 快速检查同款代价 |

## 复现

路径是烧死的:数据文件都在 `/home/charliechen/l04_scratch` 下(ext4,别放 /tmp——tmpfs 会干扰语义对比)。复跑前 `mkdir -p ~/l04_scratch`,或把各 `.cpp` 开头的 `kRoot`/`kDir`/`kLive` 等常量改成自己的目录。在本目录(`04-filesystem/`)下:

```sh
# E1
g++ -std=c++20 -Wall -Wextra -O2 -I common 01-iteration/e1_trio.cpp  -o /tmp/e1_trio  && /tmp/e1_trio
g++ -std=c++20 -Wall -Wextra -O2 -I common 01-iteration/e1_loop.cpp  -o /tmp/e1_loop  && /tmp/e1_loop
g++ -std=c++20 -Wall -Wextra -O2 -I common 01-iteration/e1_cache.cpp -o /tmp/e1_cache
/tmp/e1_cache setup && /tmp/e1_cache type && /tmp/e1_cache size && /tmp/e1_cache fresh

# E2
g++ -std=c++20 -O2 02-error-forms/e2_forms.cpp  -o /tmp/e2_forms  && /tmp/e2_forms
g++ -std=c++20 -O2 02-error-forms/e2_vanish.cpp -o /tmp/e2_vanish
/tmp/e2_vanish solo && /tmp/e2_vanish      # solo 是不删的基线,裸跑是外部 rm -rf 双进程编排

# E3 / E5 / E6
g++ -std=c++20 -O2 03-perms-space/e3_perm_space.cpp -o /tmp/e3 && /tmp/e3
g++ -std=c++20 -O2 05-path-pitfalls/e5_paths.cpp    -o /tmp/e5 && /tmp/e5
g++ -std=c++20 -O2 06-tree-diff/e6_diff.cpp         -o /tmp/e6 && /tmp/e6 setup && /tmp/e6

# E4(先 setup 建万文件目录;计时不建议在 ASan 下跑)
g++ -std=c++20 -Wall -Wextra -O2 -I common 04-bench/e4_bench.cpp -o /tmp/e4_bench
/tmp/e4_bench setup && /tmp/e4_bench && /tmp/e4_bench one a
```

strace 证据的采集口径(存档里就是这么来的):

```sh
strace -e trace=openat,getdents64,close,newfstatat,statx /tmp/e4_bench one b   # 见 04-bench/e4_strace_b.txt
strace -e trace=openat,getdents64,close,newfstatat,statx /tmp/e1_loop         # 见 01-iteration/e1_loop_strace.txt
```

## strace 证据文件索引

| 文件 | 内容 |
|---|---|
| `01-iteration/e1_loop_strace.txt` | 链接环 runaway 的终止机制:下降用 `openat(父fd, 名字)` 单组件相对打开,属性判定对完整路径 newfstatat,路径内嵌符号链接 ≥40 时 `-1 ELOOP` |
| `01-iteration/e1_cache_strace_type.txt` | 200 条目只拿类型:零 per-entry stat |
| `04-bench/e4_strace_a.txt` / `e4_strace_b.txt` | 手搓 readdir 与 directory_iterator 的 syscall 序列逐行同形(openat + getdents64×11 + close) |
| `04-bench/e4_strace_d.txt`(+ `_full.txt.gz`) | +`file_size` 后 newfstatat 10005 次 |

## 复跑注意

- **顺序敏感**:`e1_cache` 先 `setup` 再跑三档;`e4_bench` 先 `setup`;`e6_diff` 先 `setup`。各实验自建自删测试树,重复跑结果确定(e1_trio 每次重建树,遍历序仍稳定——ext4 散列序只看名字集合)。
- **`ls` 是 eza 别名的机器**:对拍目录原序要用 `/usr/bin/ls -U`,`eza` 的 `-U` 语义不同(01-iteration/e1_trio_lsu.txt 踩过)。
- **E3 的 EACCES 用例要非 root**:root 不受权限位约束,000 目录照样能开。
- **E2 的计时编排是软实时**:子进程每条 sleep 的节奏在快机器上可能与存档略有出入,但三类现象(陈旧名单、属性 ENOENT、EOF 静默截断/下降报错)都会出现。
- **E4 计时口径**:预热后 3 轮取中位,warm cache(比较的是 syscall 与包装成本,不是磁盘冷读)。
