# 05-syskit 配套实验

《syskit 工具库工程化》(vol8 systems-programming ch07 第 02 篇,收卷篇)的实验代码与原始输出存档。核心问题一句话:**思维基石的工具(unique_fd/unique_handle/sys_call/errno_code/last_error_code)收进 syskit:: 命名空间做成库,它与 networking 子卷的近亲 UniqueFd/SysError 怎么统一命名与错误模型**。本篇的实验偏编译验证与逐项对照,跑分不是重点。

目录命名:跟随 cross-platform/ 站内序号(04/05 是 ch07 两篇)。

## 与相邻篇的分工

- thinking/01、02 是全子卷工具的**唯一定义处**;本篇 e2 的 syskit 骨架是「搬家收编」,头文件里逐个标注出处,语义一字不改。
- networking/01-modern-socket-wrapping.md 与其代码档(`code/volumn_codes/vol8/networking/01-modern-socket/`)是**对照的另一方**;vol3 的 expected/error_code 两篇是错误类型的标准库机制课,本篇只取用不重讲。
- syskit 完整工程(io/fs/process/ipc/signal/timer/tty/event 八模块)外置在 todo/020 的 syskit 项目;本篇给的是骨架与决策,不是成品。

## 环境口径

| 侧 | 系统 | 编译器 | 备注 |
|---|---|---|---|
| Linux | WSL2 6.18.33.2 | g++ 16.2.1 + cmake 4.4.3 | e1 用 `-std=c++23`(expected);e2 骨架 CMake 同口径 |
| Windows | Win11 26200 | MSYS2 UCRT64 g++ 16.1.0 | cmake 走 WSL 的 cmake + toolchain 文件(见 e3) |

## 两套近亲的逐项对照(e1 的结论,正文的核心素材)

对拍对象:networking 的 `UniqueFd`/`SysError`(01-modern-socket-wrapping.md 与 unique_fd.hpp)对本域的 `unique_fd`/`errno_code`(thinking/01、02)。探针原文在 `e1_recon_trait_probe.cpp`,原始输出在 `e1_recon_linux.out`。

| 维度 | net::UniqueFd(Linux-only) | 本域 unique_fd(双平台) | 评注 |
|---|---|---|---|
| 命名 | 帕斯卡 UniqueFd;**文件名却是 unique_fd.hpp(蛇形)**,且 networking/00 的预告写的是小写 `unique_fd`——三处已经漂移 | 蛇形 unique_fd,与子卷及标准库风格一致 | 统一方向的现成动机 |
| 尺寸/移动 | sizeof=4,nothrow 移动,默认构造 | 完全相同 | 骨架同构 |
| `reset` | 仅无参(关闭+置空) | 带参重载(关闭旧+接管新) | 本域多一条「原地换手」的路 |
| `swap` | 无成员 swap;std::is_swappable 仍为 true(走通用版=三次移动) | 成员直换 int;is_swappable 同 true(一次交换) | thinking/01 的 vector 实测正对应:直换让 shuffle 零移动 |
| noexcept | get/release/reset 未标注(探测为 0) | 全标注(探测为 1) | 语义上都不会抛,但不标注=trait 层面不承诺 |
| 错误模型 | `SysError{int errno_value; std::string context}`,sizeof=40,可能堆分配;**优点:上下文字符串跟着错误走** | `std::error_code`,sizeof=16;**优点:message() 标准文本、errc 判等、Windows 侧 system_category 桥接**;「哪一步」要靠 system_error 的 what 前缀或调用点日志补 | 互补取舍,见下方统一方案 |
| EINTR | 循环里手写 `if (errno==EINTR) continue` | sys_call 内部重试(带 strace 级证据链) | 本域收编度高 |
| 语言标准 | C++23(expected) | error_code 路线 C++20 可用 | — |

**统一方案(建议,正文可辩论)**:命名统一到蛇形 `syskit::unique_fd`(三处漂移是现成动机);错误模型统一到 `std::error_code`(工具层 expected<T, error_code>、顶层 system_error 的双出口,思维基石 02 已铺),SysError 的「上下文」信息量用两个已有机制补:system_error 的 what 前缀(sys_call 的第一个参数)与 or_else 调用点日志(thinking/02 的 E3 演过);networking 侧 01 篇正文与三个代码文件(echo_server/echo_client/hold_clients)引用面小,一次修订可收编。兼容性影响面:documents/networking/{00 预告一句,01 正文,lab0 脚手架清单}+ code/networking/01-modern-socket 四个文件;02-epoll/03-reactor 两篇不引用(已核)。

## 文件与结论对照

| 实验 | 文件 | 一句话结论 |
|---|---|---|
| e1 逐项对照探针 | `e1_recon_trait_probe.cpp` + `e1_recon_linux.out` | 两套近亲同处一个 TU,detection idiom+noexcept 探测+标准 trait 打出上表全部行;全链路对拍(同一个 ENOENT)显示 net 式交回「context=\"open\" errno=2」、本域式交回「value=2 message=\"No such file or directory\"」——两种失败报告的形态差一眼可见 |
| e2 syskit 骨架 | `e2_syskit/`(include/syskit 四个头 + tests/smoke.cpp + CMakeLists.txt + `smoke_linux.out` / `smoke_windows.out`) | INTERFACE 库 + 冒烟测试,**双平台同一份 CMakeLists 零平台分支**(差异全收在头文件 #ifdef);Linux 原生 cmake 与 Windows toolchain 配置各编各跑,四组断言全绿(错误路径值=2、往返读写、system_error 出口、move/swap 换手) |
| e3 CMake 平台分支三写法 | `e3_cmake_branches/`(variant_if、variant_genex、mingw.toolchain.cmake + `logs/cmake_branches_scenes.txt` 六场景) | **if(WIN32) 信平台身份,身份认错就选错分支**(裸 CXX 覆盖下 if(UNIX) 把 POSIX 源塞给 g++.exe,sys/epoll.h 缺失当场失败);**genex 生成期才展开**(configure 期 message 只能打出原文,展开结果在编译命令行的 -D 里,ninja -v 抓到两侧各自只有自己的那个宏),且同样被错误身份骗;**toolchain 文件把 CMAKE_SYSTEM_NAME 从「探测宿主」变「声明目标」**,是半交叉(WSL cmake + g++.exe)唯一配对的方法 |

## 独家发现

1. **e2 的错误语义分叉**:同一个「打开带目录前缀的缺失路径」,Linux 统一 ENOENT(2),Windows 按路径状态细分——目录不存在是 3(ERROR_PATH_NOT_FOUND),目录在而文件缺才是 2(ERROR_FILE_NOT_FOUND)。smoke 的断言因此收 2 或 3,这不是放宽,是两侧语义的真实形状。MinGW 的 `system_category().message()` 吐 ANSI 代码页字节(GBK 乱码)再次现身,与思维基石 02 的实测呼应——syskit 要 UTF-8 文本,得走自定义 category 的正路。
2. **e3 的 make + C: 冒号冲突**:toolchain 配对了、首次构建也过了,但 touch 源文件做增量重编时 make 报 `multiple target patterns`——make 把编译器路径里的 `C:` 当成了目标分隔符。解法:换 Ninja 生成器,增量重编正常;另 interop 产物每次重新链接都要补 `chmod +x`。两个现象都是 WSL cmake 调 Windows 编译器这条半交叉链路的独家实录。
3. **e1 的 is_swappable 双真**:net 版没有 swap 成员,is_swappable 却是 true(move-only 类型走 std::swap 通用实现=三次移动)——「有没有 swap」要探测成员而不是探测 trait,这也是对照表里最容易看错的一行。

## 复现

```bash
# e1(Linux)
g++ -std=c++23 -O2 -Wall -Wextra -D_FORTIFY_SOURCE=2 e1_recon_trait_probe.cpp -o e1 && ./e1

# e2 骨架(Linux 原生)
cmake -S e2_syskit -B e2_syskit/build-linux && cmake --build e2_syskit/build-linux && ./e2_syskit/build-linux/smoke
# e2 骨架(Windows 半交叉,toolchain 见 e3)
cmake -S e2_syskit -B e2_syskit/build-win -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/e3_cmake_branches/mingw.toolchain.cmake && \
cmake --build e2_syskit/build-win && chmod +x e2_syskit/build-win/smoke.exe && ./e2_syskit/build-win/smoke.exe

# e3 六场景,逐条命令与预期输出都在 e3_cmake_branches/logs/cmake_branches_scenes.txt
```
