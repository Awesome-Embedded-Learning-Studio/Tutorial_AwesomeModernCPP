# 01-anatomy:E1 CreateProcessW 全解剖

文件:`e1_anatomy.cpp` / `e1_anatomy.out`(单二进制多模式:无参=驱动,`attr`/`echo`/`config`/`env` 为子模式,其余参数走 dump 分支)。

## 五节结论(行号见 e1_anatomy.out)

**[0] STARTUPINFOEX + PROC_THREAD_ATTRIBUTE_LIST(句柄白名单继承)**
两个文件都开成可继承,但 `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` 只列 log_a。子进程实测:`WriteFile(h_a)=1`,`WriteFile(h_b)=0 err=5`——白名单外的可继承句柄根本没进子进程句柄表,那个数值在子进程里是野值(首轮跑出 err=6,这轮 err=5,报 5 还是 6 不稳定,稳定的是"写不进"),文件读回 log_a 有内容、log_b 空。`wShowWindow/dwFlags` 只设字段示范(子进程共享父控制台,无视觉效果,不冒充验证)。

**[1] lpApplicationName vs lpCommandLine 搜索矩阵**
- (a) app=NULL:走"应用目录→CWD→系统目录→Windows→PATH"搜索,CWD 与父目录都没 fakeecho.exe 时 PATH 命中;孩子 argv[0] 还是裸 token,真实模块要问 GetModuleFileNameW
- (b) app 给相对名 `fakeecho.exe`:**只相对 CWD 解析,不搜索** → err=2(同一串 (a) 里能找到)
- (c) app=绝对路径 + cmdline 首 token 写 TOTALLY_FAKE_ARGV0.EXE:加载的是真身,argv[0] 是假名——**给内核看的和给 argv[0] 的是两个参数**
- (d) **诱饵局**:app=NULL、路径带空格不加引号,目录里埋了截断名 `with.exe`:首 token `...\with` 补 `.exe` 直接命中诱饵——Program.exe 攻击完整复刻;孩子的 GetCommandLineW 原样(未改写)
- (d2) 撤掉诱饵同一命令行:系统拼接 token 找到真身,且**改写了子进程命令行**——GetCommandLineW 显示带引号版本,孩子自己都看不出原始命令行没引号
- (e) 引号包住:argv[0]=完整路径,原样

**[2] 双句柄 + CREATE_SUSPENDED 两段式**
挂起出生(此刻 token 文件不存在)→ 父进程写 token → `ResumeThread` 返回 prev suspend count=1 → 孩子读到 `TOKEN=0xC0FFEE` 退 7。hThread 用完即关,进程照跑,最后 hProcess 等到退出码——**线程句柄管执行,进程句柄管生死/等待**。

**[3] lpEnvironment:NULL 继承 vs 自建**
- (a) NULL:孩子看到父的 46 项环境(含 VOL8_E1_MARKER=inherited-from-parent)
- (b1) 极简自建块(marker+SYSTEMROOT,没带 PATH):CreateProcessW 本身成功,但孩子**死于 0xC0000135(STATUS_DLL_NOT_FOUND)**——动态链接的 exe 找 ucrt 运行库 DLL 也吃 PATH
- (b2) 补带 PATH:孩子环境恰好 3 项(我们放进去的),marker 是新值——**整块替换,没有合并**;想留什么必须自己搬

## 复现

```sh
cd 01-anatomy
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -municode e1_anatomy.cpp -o e1_anatomy.exe
chmod +x e1_anatomy.exe && ./e1_anatomy.exe > e1_anatomy.out 2>&1
```

注意:搜索实验把 exe 自拷贝进 `%TEMP%\vol8_e1\{plain,with space}\fakeecho.exe` 与诱饵 `with.exe`,起手清理可反复跑;(d) 的诱饵只在 (d) 前存在,(d2) 前删除,顺序别倒。
