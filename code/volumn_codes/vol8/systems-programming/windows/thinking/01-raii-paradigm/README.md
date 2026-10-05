# 01-raii-paradigm 配套实验(Windows 侧)

拟新增文章《OS 资源的 RAII 范式》(Windows 侧,thinking 系列)的真机实验与原始输出存档。工具箱 `unique_handle` 的正式定义沿用 [01-win32-file-io.md](../../../../../../../documents/vol8-domains/systems-programming/windows/file-io/01-win32-file-io.md) 一篇,这里是对它的行为验证与两个失败值语义坑的实测。

## 文件与实验对照

| 文件 | 实验 | 内容 |
|---|---|---|
| `e1_unique_handle.cpp/.out` | E1 | unique_handle 骨架(与 win_util.hpp 同构)+ 失败值矩阵 + CloseHandle 哨兵反应 + GetProcessHandleCount 前后计数证明生命周期 + move 全套 |
| `e1b_closehandle_sentinel.cpp/.out` | E1 补充 | 隔离复核 CloseHandle 对 NULL / -1 / -2 / 野值的返回值与 last-error(e1 的 [2] 节测到意外结果后单独取证) |

## 环境(全部实测口径,捕获日 2026-10-02)

- Windows:Win11,`cmd.exe /c ver` 报 `10.0.26200.9457`(中文系统,ANSI 代码页 GBK)
- 编译器:MSYS2 UCRT64 g++ 16.1.0(Rev5),`/mnt/c/msys64/ucrt64/bin/g++.exe`
- 宿主:WSL2,内核 `6.18.33.2-microsoft-standard-WSL2`;编译运行全走 WSL→Win32 interop

## WSL interop 编译运行的确切做法(文章环境交代用)

```sh
# 1) 编译:cd 到源码目录(WSL 文件系统上),参数一律用相对路径
cd code/volumn_codes/vol8/systems-programming/windows/thinking/01-raii-paradigm
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e1_unique_handle.cpp -o e1_unique_handle.exe

# 2) 运行:binfmt interop 直接跑;产物在 WSL 文件系统上不带执行位,先补一次
chmod +x e1_unique_handle.exe && ./e1_unique_handle.exe
```

路径机制(实测,容易踩):

- interop **不做参数翻译**:`cmd.exe /c "echo /tmp/x"` 原样回显 `/tmp/x`。
- 绝对路径 `/tmp/...` 之所以碰巧能用,是因为 cwd 在 WSL 文件系统上时,Windows 把根路径 `/tmp` 解析到 cwd 所在 UNC 共享的根 `\\wsl.localhost\<distro>\tmp`;`wslpath -w /tmp/w32raii` 的输出就是它。把 cwd 挪到 C: 盘(`/mnt/c/Users`)再跑同一命令,立刻报 `No such file or directory`(它去找 `C:\tmp\...`)。
- 结论:要么 `cd` 到源码目录用相对路径(本 README 的做法),要么 `wslpath -w` 显式转换。
- 动态链接的 exe 能在 interop 下直接跑,依赖 `C:\msys64\ucrt64\bin` 在 Windows PATH 里(本机在);要通用就加 `-static`。
- 在 `/mnt/c` 下建工作目录编译也可以,产物自带执行位不用 chmod;本批统一走 /tmp + chmod。
- WSL 里 `cmd.exe` 不在 PATH,用全路径 `/mnt/c/Windows/System32/cmd.exe /c ver`;它会抱怨 UNC cwd 不支持,但仍正常执行。

## 关键结论(均有 .out 对应行)

**失败值矩阵(两套哨兵并存,判错前查文档 Return value 段):**

| API | 失败返回 | GetLastError |
|---|---|---|
| CreateFileW(OPEN_EXISTING 不存在) | `INVALID_HANDLE_VALUE`(ffffffffffffffff) | 2 ERROR_FILE_NOT_FOUND |
| CreateEventW(与互斥体撞名) | `NULL` | 6 ERROR_INVALID_HANDLE |
| CreateFileMappingW(与互斥体撞名) | `NULL` | 6 ERROR_INVALID_HANDLE |
| OpenProcess(野 pid 0xC0FFEE) | `NULL` | 87 ERROR_INVALID_PARAMETER |

注意对照行:`CreateFileMappingW(INVALID_HANDLE_VALUE, ...)` **不是失败**——传 -1 表示"页文件支持的映射",实测成功发回有效句柄。同一个 -1,在 CreateFileW 是失败值,在 CreateFileMappingW 是合法参数。

**CloseHandle 对哨兵值的反应(e1b,哨兵 1234 起手):**

| 输入 | 返回 | GetLastError |
|---|---|---|
| NULL | FALSE | 6(覆盖哨兵) |
| INVALID_HANDLE_VALUE(-1,即 GetCurrentProcess() 伪句柄) | **TRUE** | 1234(槽位没动) |
| (HANDLE)-2(GetCurrentThread() 伪句柄) | **TRUE** | 1234(槽位没动) |
| (HANDLE)0x1234 野值 | FALSE | 6 |
| 有效句柄关两次 | 第 1 次 TRUE / 第 2 次 FALSE | 6 |

即:CloseHandle 对两个伪句柄"关成功了"但其实什么都没关(伪句柄不占句柄表),这是实测口径(Win11 26200),文档没有把它写成承诺,文章引用时按"实测在 Win11 26200 上"表述。

**unique_handle 生命周期**(e1 [3]):GetProcessHandleCount 全程作证——构造接管 +1、move 构造/赋值不变计数且源被掏空、release() 交还不变计数、reset() 立即 -1、作用域析构回到 baseline,无泄漏。

**陷阱(e1 [4])**:unique_handle 以 -1 为空哨兵,直接接管 CreateEventW 家族的失败值 NULL 时,`operator bool` 判成 true(语义错),析构会对 NULL 调 CloseHandle(FALSE + err 6,不炸但错)。NULL 家族 API 必须在边界先判(check_win32 的做法),不能指望 RAII 类自己认。

## 复跑注意

- 句柄计数(baseline 61/62 这些数)随进程环境浮动,每次跑都不同,对得上"涨落规律"即可。
- 撞名实验用 `Local\sysprog-e1-<pid>` 命名,进程退出对象即消失,可反复跑。
- `.out` 里 err=2 等错误码稳定;句柄指针值(0xf4 之类)每次不同。
