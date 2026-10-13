# 04-dir-enum 配套实验(Windows 侧)

《目录枚举与 NTFS 家族》一文的真机实验与原始输出存档(文章施工中,先入册实验)。五个目录对应五组实验(e1~e5),`common/win_dir.hpp` 是本篇契约工具(`last_error_code`/`check_win32`/`unique_handle` 沿用 [01-win32-file-io.md](../../../../../../../documents/vol8-domains/systems-programming/windows/file-io/01-win32-file-io.md) 的 win_util.hpp 形态,新增 `unique_find`:FindFirstFileW 发回的查找句柄交给 RAII 管,`~unique_find` 里 FindClose,失败值哨兵与 CreateFileW 同款 INVALID_HANDLE_VALUE)。Linux 侧镜像篇是 [04-filesystem](../../../linux/file-io/04-filesystem/),本文各实验产出的都是对照数据。

## 文件与实验对照

| 目录 | 文件 | 内容 |
|---|---|---|
| `01-findfirst/` | `e1_trio.cpp/.out` | 三件套遍历:乱序建树 → NTFS 字典序枚举;`.` 与 `..` 特殊项;`*.txt`/`*.`/`*.htm` 过滤(8.3 短名匹配);字段全解(attributes 位/三时间/大小高低位拼 64 位,4.5 GiB 账面文件实证 High 有货);根目录无点项;FindClose 对 INVALID_HANDLE_VALUE 是 FALSE+err(与 CloseHandle 对 -1 的静默 TRUE 对照) |
| | `e1_wide_out.cpp/.out` + `run_wide_out.sh` | 宽字符名输出坑:9 种姿势逐 mode 独立进程跑,stdout 落文件 + od 十六进制留证(见下表) |
| | `e1_errors.cpp/.out` | 错误路径:目录不存在=3 / 模式无匹配=2 / 空目录 `\*` 成功吐两点项 / 尽头=18 / 文件挂 `\*`=267 / 尾杠无模式=2 |
| `02-recursive/` | `e2_recursive.cpp/.out` | 手搓递归(setup/default/follow/clean 四模式):5 层树 DFS 前序;junction 默认不下钻;follow 模式环转 22 圈,MAX_PATH 墙先于 40 层闸拦住 |
| `03-ntfs-family/` | `e3_family.cpp/.out` | 家族矩阵:硬链接(同 FileIndex、nNumberOfLinks 2→1、内容共享、拆名存活)/ 符号链接 1314 被拒 / junction(attr、枚举穿透、悬空两姿势、reparse tag 0xA0000003、跨卷 C:→F:、删链接目标无损)/ 稀疏(1 GiB 账面 128 KiB 实占,allocated ranges 画洞)/ exFAT 负对照(硬链接、稀疏、junction 全拒 err=1) |
| | `e3_capture.sh` → `e3_mklink_capture.txt` | mklink 四条命令原话:文件符号链接拒 / `/D` 拒 / `/J` 成 / `/H` 成(cmd 侧造法存证,GBK→UTF-8) |
| | `e3_capture.sh` → `e3_dir_family.txt` | `dir /a` 的 `<JUNCTION>` 类型列与跳转目标;`fsutil hardlink list`(一条 MFT 记录两个名字);`fsutil sparse queryflag`(均免管理员) |
| `04-bench/` | `e4_bench.cpp/.out` | 10000 文件遍历计时:A FindFirstFileW / B Ex flags=0 / C Ex+LARGE_FETCH,暖缓存每腿预热+3 轮取中位 |
| `05-path-pitfalls/` | `e5_paths.cpp/.out` | `\\?\` 前缀与 260 墙(裸 CreateDirectoryW 第 5 层 274 字符倒下,前缀链 404 字符建完,查询/枚举同款对照);正反斜杠(普通路径等效、设备路径混 `/`=123);尾部反斜杠四种 API 姿势;`std::filesystem::path` 与 Linux E5 对拍 |

## 环境(全部 .out 由此环境捕获,2026-10-02)

| 项 | 值 |
|---|---|
| Windows | Win11 `10.0.26200.9457`,中文系统(ANSI/OEM 代码页 936=GBK) |
| 编译器 | MSYS2 UCRT64 g++ 16.1.0(Rev5),`/mnt/c/msys64/ucrt64/bin/g++.exe`,C++20 |
| 编译运行链 | WSL2(内核 6.18.33.2)→ Win32 interop,做法与 thinking/01 篇 README 相同:cd 到源码目录(WSL 文件系统)用相对路径编译,`chmod +x` 后直接跑 |
| 数据盘 | 全部在 `C:\Users\CharlieChen114514\AppData\Local\Temp\sysprog-direnum\`(NTFS 系统盘);跨卷用 F:(NTFS),负对照用 D:(exFAT) |
| 权限 | 非管理员(medium integrity);开发者模式未开(注册表无 AllowDevelopmentWithoutDevLicense) |
| 长路径政策 | HKLM `...\FileSystem\LongPathsEnabled=1`,但 exe 无 longPathAware 清单,裸路径仍被裁(实测见 e5) |
| 卷序列号 | C: = 0xDA3672FA(e3 的 FileIndex 对照用) |

## 每实验一句话结论

| 实验 | 结论 |
|---|---|
| e1 三件套 | **NTFS 枚举序 = 大小写折叠后的字典序**(乱序建树,枚举出来 alpha.txt → Beta.dat → longname.html → Mike.TXT → noext → zeta.txt → 中文 → 子目录,与创建顺序无关)——Linux 侧 ext4 是名字散列序,Windows 侧是 B+ 树排序序,两个都不是创建序,但 Windows 这个"看起来有序" |
| e1 点项 | `\*` 枚举永远先吐 `.` 与 `..`(attr=DIRECTORY,时间就是目录自己的元数据);**根目录 `C:\*` 没有点项**;非 `*` 模式(`*.txt`)不吐点项——"目录空不空"的判定要跳过两点项再看,`ERROR_NO_MORE_FILES(18)` 只是 FindNextFileW 的正常收尾 |
| e1 过滤坑 | `*.` 匹配的是"无扩展名"的名字——文件 noext、目录 子目录、连 `.` `..` 都算;`*.htm` 会捎上 `longname.html`,替它匹配的是 8.3 短名 `LONGNA~1.HTM`(cAlternateFileName 字段就是证据)——按扩展名过滤会多收,这是 DOS 通配符的遗产 |
| e1 字段 | 大小 = `nFileSizeHigh<<32 \| nFileSizeLow`(SetEndOfFile 拉出 High=0x1/Low=0x20000000 的 4.5 GiB 账面文件实证);attributes/三时间在枚举时一并带回——对照 Linux 侧 readdir 拿不到属性、要补 stat,Windows 枚举自带全家福 |
| e1 错误路径 | 目录不存在(挂任何模式)= **3 ERROR_PATH_NOT_FOUND**;目录在、模式无匹配 = **2 ERROR_FILE_NOT_FOUND**;空目录 `\*` = 成功(. .. 两项)→ 区分"目录不存在"与"目录空"看的不是返回值,是错误码 3 vs 枚举内容;文件路径挂 `\*` = 267 ERROR_DIRECTORY;尾杠无模式 = 2 |
| e1 宽字符输出 | 见下表:C locale 下 printf 静默丢字、wprintf 替换成 `?`;GBK locale 中文对、emoji 丢;`.UTF8` locale 中文对、**代理对 emoji 还是丢**;唯一全对的是手动 `WideCharToMultiByte(CP_UTF8)`;传说中 wprintf/printf 混用互相弄哑对方的坑在 UCRT 上**不复现**(两个方向都能接着打) |
| e2 递归 | DFS 前序(撞到子目录立刻下钻,同层按 NTFS 字典序)与 Linux `recursive_directory_iterator` 同构;REPARSE_POINT 目录默认报一行不下钻(7 文件/6 真目录/2 链接叶子);follow 下钻时环无内核兜底——Linux 有 40 层 ELOOP 自动刹车,Windows 只能自备闸,实测转 22 圈后是 **259~266 字符的 MAX_PATH 墙先拦住的**(文件 187/真目录 127/链接叶子 44) |
| e3 硬链接 | CreateHardLinkW 后两个名字 `GetFileInformationByHandle` 同 FileIndex(0x00120000000ab77f)、同 nNumberOfLinks=2;一边续写另一边读得到("hello world");DeleteFileW 掉一个名字,另一个内容完好、计数回落 1、FileIndex 不变——同一条 MFT 记录的多份名字,Linux 侧完全同构 |
| e3 符号链接 | 本机(开发者模式关 + 非管理员)CreateSymbolicLinkW 无 flag 与带 `ALLOW_UNPRIVILEGED_CREATE` 都返回 FALSE + **1314 ERROR_PRIVILEGE_NOT_HELD**(那个 flag 需要开发者模式背书);cmd `mklink /D` 同样被拒(原话存证)——字段位(`FILE_ATTRIBUTE_REPARSE_POINT`)从 junction 侧读,创建门槛如实记录 |
| e3 junction | mklink /J 免特权;attr=0x410(DIRECTORY\|REPARSE);**枚举 `jn\*` 打开即穿透**看到目标内容;悬空 junction 两种姿势都活着(GetFileAttributesW 与 FindFirstFileW 单名查询都不跟随目标,attr 照常 0x410);FSCTL_GET_REPARSE_POINT 读出 tag=**0xA0000003 MOUNT_POINT** 与目标 `\??\C:\...`;可跨卷(C: → F:\ 实测);RemoveDirectoryW 只删链接,目标无损 |
| e3 稀疏 | FSCTL_SET_SPARSE 免特权(同款免管理员的还有 fsutil sparse setflag/queryflag;fsutil 别的子命令很多要管理员);同样在 0 与 1 GiB 各写 1 字节:普通文件逻辑=实占=1073741825,稀疏文件逻辑同、实占 131072(两簇),allocated ranges 一个 [0..64K)+[1G..1G+1) 一个连片 [0..1G);枚举属性 SPARSE(0x200) 可见 |
| e3 exFAT | 硬链接 err=1、FSCTL_SET_SPARSE err=1、junction 造不出——链接家族是 **NTFS** 的,不是"文件系统"的 |
| e4 计时 | 10000 文件(暖缓存,3 轮中位):A FindFirstFileW 1.66 / B Ex+0 1.76 / C Ex+LARGE_FETCH **1.35 ms**——LARGE_FETCH 在这台机器稳定快 ~19%(复跑 4 次都在 1.35~1.68 区间,A/B 都在 1.66~1.96);提示位不是承诺位,别处另算。Linux 侧对照:readdir 2.16 / directory_iterator 3.42 ms,而且 Windows 枚举自带属性,没有"再补一发 stat"的腿 |
| e5 长路径 | 注册表 LongPathsEnabled=1 也没放行裸 exe(政策只认带 longPathAware 清单的应用):裸 CreateDirectoryW 233 字符过、**274 字符倒(err=3)**;`\\?\` 前缀同一条链 404 字符建完;深层的查询/枚举/删除裸路径全灭(err=3)、带前缀全通——`\\?\` 把路径交给文件系统设备直读,跳过 Win32 归一化,上限 32767 |
| e5 斜杠/尾杠 | 普通路径 `/` 与 `\` 等效(CreateFileW 全正斜杠、FindFirstFileW 混用都收);**`\\?\` 设备路径只认反斜杠**,混 `/` = 123 ERROR_INVALID_NAME;尾杠:CreateDirectoryW/GetFileAttributesW/CreateFileW(开目录)收,FindFirstFileW 无通配符的尾杠当模式匹配=err 2,双杠+通配符被容忍 |
| e5 fs::path | `value_type` 2 字节 wchar_t(Linux 1 字节 char);右侧带根名顶掉左侧(`"base"/"C:/abs"`==`"C:/abs"`);尾杠吸收、双杠残留、拼空串添分隔符、`lexically_normal` 纯词法——与 Linux E5 全部同款;`"a/b/"` 迭代同样产尾部空元素 `[a][b][]`;`C:/x == C:\x` 成立(比较前归一化) |

## 宽字符输出坑总表(e1_wide_out,stdout 重定向到文件,字节是证据)

| mode | 姿势 | 中文文件.txt | emoji😀.txt(代理对) | 备注 |
|---|---|---|---|---|
| a | C locale + `printf("%ls")` | **静默丢字**(`cn=[]`),返回值还报成功 | emoji 丢 | 最危险:不报错、少内容 |
| b | C locale + `wprintf(L"%ls")` | 替换成 `????`(0x3F 字节) | 替换成 `??` | 与 a 不同的失败长相 |
| c | `setlocale(LC_ALL,"")`(GBK) | 正确(GBK 字节 d6 d0 ce c4 …) | **丢** | GBK 装不下 U+1F600 |
| d | `setlocale(LC_ALL,".UTF8")` + printf | 正确(UTF-8 e4 b8 ad …) | **丢** | UCRT+MinGW 下 %ls 转换丢代理对 |
| e | `.UTF8` + wprintf | 正确(UTF-8) | **丢** | 同上 |
| f | 手动 `WideCharToMultiByte(CP_UTF8)` + `%s` | 正确 | **正确**(f0 9f 98 80) | 唯一全对,契约工具 to_utf8 的路线 |
| orient | 先 wprintf 再 printf | 两个都成功 | — | 经典"混用弄哑"坑在 UCRT **不复现** |
| orient2 | 先 printf 再 wprintf | 两个都成功 | — | 反方向也不复现 |

附:wprintf 返回宽字符数、printf 返回字节数(同一行中文 18 vs 26);本表 stdout 是管道/文件不是控制台,`GetACP/GetOEMCP/GetConsoleOutputCP` 全 936;重定向下 stdout 走文本模式,`\n` 落成 `0d 0a`(od 里可见)。

## 复现

路径是烧死的:数据都在 `C:\Users\CharlieChen114514\AppData\Local\Temp\sysprog-direnum\` 下,换机器把各 `.cpp` 开头的 `kRoot`/`kDir`/`kFam` 等常量改成自己的目录。在本目录(`04-dir-enum/`)下:

```sh
# e1(自建自删测试树,可反复跑)
cd 01-findfirst
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -I ../common e1_trio.cpp    -o e1_trio.exe    && chmod +x e1_trio.exe    && ./e1_trio.exe
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -I ../common e1_errors.cpp  -o e1_errors.exe  && chmod +x e1_errors.exe  && ./e1_errors.exe
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -I ../common e1_wide_out.cpp -o e1_wide_out.exe && chmod +x e1_wide_out.exe
./e1_wide_out.exe setup && sh run_wide_out.sh > e1_wide_out.out   # .out 是脚本汇编的(od 留证)

# e2(先 setup 建树;default 裸跑;follow 看环)
cd ../02-recursive
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -I ../common e2_recursive.cpp -o e2_recursive.exe && chmod +x e2_recursive.exe
./e2_recursive.exe setup && ./e2_recursive.exe && ./e2_recursive.exe follow && ./e2_recursive.exe clean

# e3(一键全矩阵;cmd 侧证据另跑捕获脚本)
cd ../03-ntfs-family
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -I ../common e3_family.cpp -o e3_family.exe && chmod +x e3_family.exe && ./e3_family.exe
sh e3_capture.sh    # 产出 e3_mklink_capture.txt / e3_dir_family.txt(WSL 里跑,GBK 过 iconv)

# e4(先 setup 建万文件目录 ~5 s;计时不建议在 ASan 下跑)
cd ../04-bench
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -O2 -I ../common e4_bench.cpp -o e4_bench.exe && chmod +x e4_bench.exe
./e4_bench.exe setup && ./e4_bench.exe && ./e4_bench.exe clean

# e5
cd ../05-path-pitfalls
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -I ../common e5_paths.cpp -o e5_paths.exe && chmod +x e5_paths.exe && ./e5_paths.exe
```

## 复跑注意

- **编码口径**:程序自己的输出全是 UTF-8(`.out` 直接可读);cmd/fsutil 子进程要么整体静默(e3_family 里 mklink 的回显由 FindFirstFileW 属性验证后用 UTF-8 自己报),要么过 `iconv -f GBK -t UTF-8`(e3_capture.sh)。给 cmd 传 UNC cwd 会吐 GBK 横幅且重定向拦不住——程序里先 `SetCurrentDirectoryW(L"C:\\")`,脚本里先 `cd` 进 `/mnt/c` 下的目录。
- **e3_family 幂等**:开场自带清理,重复跑结果确定;FileIndex 高 16 位是 MFT 记录号里的序号位,跨机器无意义,同机器同文件才可比。
- **e3 跨卷/负对照**:跨卷 junction 目标用的是 F: 盘根(只建链接不动 F: 的内容);exFAT 负对照要一块非 NTFS 盘,本机是 D:,换机器改 `kd` 常量。
- **e4 计时口径**:暖缓存(预热一轮后才计时)、3 轮中位、QPC;setup 建万文件 ~5 s;跑完 `clean` 清掉。e5 的 normal.bin 会在 C: 上瞬时实占 ~1 GiB(非稀疏腿的对照就是要这个数),跑完即删。
- **长路径手工清理**:e5 若中途崩,深层目录在资源管理器里都够不着,用 `\\?\` 前缀从最深层往外 `RemoveDirectoryW`(程序收尾就是这么干的)。
- **e3_family 收尾残渣**:程序末尾的清理对 `jn_crossvol`(目标 F:\ 根)与 `jn_dangling`(目标不存在)这两个 junction 没删干净(DeleteFileW 对它们没生效),复跑幂等不受影响(开场 rm_tree 会再清);要手动清就在 WSL 里对链接本体 `rm`(9P 视角 junction 是符号链接,rmdir 反而报"Not a directory"),目标侧不会被碰。
- **符号链接门槛**:开发者模式开(设置→隐私和安全性→开发者选项)或提权后,CreateSymbolicLinkW 才能过;届时 [2] 节的两行会变成成功,attr 与 tag(0xA000000C SYMLINK)补上即可,其余结论不受影响。
- **W01 引用**:Windows 删除打开文件的默认行为 vs `FILE_SHARE_DELETE` 的对照,数据在 01-win32-file-io 篇(dwShareMode 一节:不给 FILE_SHARE_DELETE 时 DeleteFileW 对被打开文件 ERROR_SHARING_VIOLATION,POSIX 的 unlink 则永远成功、名字当场消失),本篇引用不重做。

## 意外发现(写作时可当钩子)

1. `*.` 连 `.`/`..`/子目录 都匹配(不只无扩展名文件);`*.htm` 靠 8.3 短名捎带 `longname.html`——过滤模式的世界比 glob 直觉宽。
2. wprintf/printf 混用互相弄哑的"经典坑"在 UCRT + MinGW g++ 上两个方向都不复现(老 MSVCRT 的传说,拿数据说话)。
3. `.UTF8` locale 下 `%ls` 对代理对(emoji)静默丢字——中文对、emoji 丢,"UTF-8 了"不等于"全对"。
4. follow 模式的 junction 环,先拦住递归的不是深度闸而是 MAX_PATH(259~266 字符 err=3)——两个限制机制叠着,和 Linux 的 40 层 ELOOP 是三种故事。
5. 空目录 `\*.txt` 直接 err=2(点项不匹配非 `*` 模式),所以"空目录判定"用 `\*` 数非点项最稳。
6. 建 10000 个 16 字节文件花 5 s(NTFS 元数据Journal 的代价),遍历却只要 1.35~1.93 ms——写和读的代价差 3 个数量级。
