# 02-error-paradigm 配套实验(Windows 侧)

《错误处理范式:从 errno 到 expected》(`documents/vol8-domains/systems-programming/thinking/02-error-paradigm.md`,文章已写,Windows 侧素材)的真机实验与原始输出存档:GetLastError 的时机陷阱、错误描述的三个来源、`last_error_code()` 装箱的两种路线、`std::expected` + `std::system_error` 双出口约定。`last_error_code` 的定义处随公共工具迁移落到了文章本篇;`check_win32` 沿用 [01-win32-file-io.md](../../../../../../../documents/vol8-domains/systems-programming/windows/file-io/01-win32-file-io.md) 一篇。

环境与 interop 编译运行命令与 [../01-raii-paradigm/README.md](../01-raii-paradigm/README.md) 相同(Win11 10.0.26200.9457 / MSYS2 UCRT64 g++ 16.1.0 / WSL2,捕获日 2026-10-02)。唯一差别:`e4` 用 `-std=c++23`(std::expected 是 C++23 组件,`-std=c++20` 下 GCC 16 直接报错,`.out` 归档里有这桩事实的编译记录在案)。

## 文件与实验对照

| 文件 | 实验 | 内容 |
|---|---|---|
| `e2_last_error.cpp/.out` | e2 | 时机五连测 + FormatMessageW(UTF-8)文本 + `system_category().message()` 原始字节/GBK 转码 + generic 对照 + default_error_condition 映射 |
| `e2b_clobber_probe.cpp/.out` | e2 补充 | 哪些"成功"的调用会动 last-error 槽位:电池 30 行(29 个成功调用 + 1 行故意撞锁失败的行内对照)另加对照组 2 行,哨兵法逐项实测 |
| `e3_error_code.cpp/.out` | e3 | `last_error_code()` 立即装箱(system_category 版)+ 自定义 `win32_category`(message 走 FormatMessageW+UTF-8)+ 装箱即冻结 + 相等性宇宙 |
| `e4_expected_system_error.cpp/.out` | e4 | `read_file_size` 返回 `std::expected<DWORD64, std::error_code>`,顶层把 error_code 包成 `std::system_error` 抛出/接住,两个 category 各演示一遍 |

## 关键结论(均有 .out 对应行)

**1. GetLastError 时机:e2 的常规插入(GetProcessId/fprintf(stderr)/printf/new)全部没冲掉 err=2——"插个成功调用就丢码"不是每个 API 都成立。** 电池实验(e2b)把三种形态都抓到了:

| 成功的调用 | 槽位 | 形态 |
|---|---|---|
| CreateFileW 真成功 | 2 → **0** | 成功路径清零(头号实锤:插一个它,错误码就没了) |
| GetComputerNameW 成功 | 2 → **203** | 成功路径留杂音(内部探测环境变量吞掉的失败) |
| WriteFile/ReadFile/FormatMessageW/Sleep(0)/new/delete 等 27 项 | 2 | 保留(不动槽位) |

失败的调用必覆盖(对照:野句柄 GetProcessId → 6;第二个不存在文件 → 2)。三种形态并存,正是官方"call GetLastError immediately"的实证——"大多数成功调用不清零,但**有些清零、有些留杂音**",不能赌。

**2. 错误描述的三个来源(同一错误码 2):**

| 来源 | 实测输出 | 编码 |
|---|---|---|
| FormatMessageW + WideCharToMultiByte(CP_UTF8) | `系统找不到指定的文件。` | UTF-8,正路 |
| `std::system_category().message(2)`(MinGW libstdc++) | `cf b5 cd b3 ...`(直读乱码,GBK 转码后同为上句) | ANSI 代码页(本机 GBK) |
| `std::generic_category().message(2)` | `No such file or directory` | errno 宇宙 |

机制佐证:`objdump -p libstdc++-6.dll` 的导入表里有 `FormatMessageA` 与 `GetLastError`——libstdc++ 在 Windows 上就用 A 版 FormatMessage 产 system_category 文本,天然是 ANSI 代码页字节。**结论:** `system_category` 的值语义可靠、文本要自己转码;要 UTF-8 直读,用自定义 category 包 FormatMessageW(e3 的 `win32_category`)。

**3. MinGW 的 system_category 内置 win32→errno 映射(default_error_condition):** system(2)→generic 2(ENOENT)、system(13)→generic 22(EINVAL)、system(32)→generic 16(EBUSY)、system(87)→generic 22。因此 `ec == std::errc::no_such_file_or_directory` 在 Win32 错误码上**直接可用**(e3/e4 实测为 1);但同数字不同宇宙要小心:Win32 13(ERROR_INVALID_DATA)不等于 errc::permission_denied(errno 13 才是 EACCES),实测为 0。同值不同 category 的 error_code 永远不相等。

**4. 装箱即冻结(e3 [2]):** 装箱后再插一次会清零槽位的成功 CreateFileW,`GetLastError()` 已变 0,`ec.value()` 仍是 2——"失败分支头一行就装箱"的实证。

**5. 双出口约定(e4):** 工具层 `read_file_size` 返回 expected,句柄交给 unique_handle,所有 return 路径自动关;顶层把 `s.error()` 包成 `std::system_error` 抛出。catch 站点:`e.code().value()`=2,`e.code() == errc::no_such_file_or_directory` 为 1。what() 的可读性跟着 category 走:system_category 版 what() 是 `read_file_size: <GBK 字节>`(乱码,转码后可读);win32_category 版 what() 直接是 UTF-8,无需转码。

## 复跑注意

- `e2_last_error.out` 开头那行 stderr 出现在最前面,是 `2>&1` 合流 + stdout 管道全缓冲的交错现象,不是输出错了。
- `.out` 里的 GBK 乱码字节是证据本体,别"修复"它;要看可读文本,程序里已各带一份转码行。
- e2b 电池的行为是 Win11 26200 这一版的实测(CreateFileW 成功清零、GetComputerNameW 留 203),系统升级后可能漂移,复跑以电池输出为准。
- pid、句柄值每次不同;e4 的 123456 是程序自造文件的字节数,稳定。
