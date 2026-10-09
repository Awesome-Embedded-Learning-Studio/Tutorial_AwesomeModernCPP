# 02-error-paradigm 配套实验

《错误处理范式:从 errno 到 expected》(`documents/vol8-domains/systems-programming/thinking/02-error-paradigm.md`,文章已写)的实验代码与原始输出存档。四组实验对应文章四条主线:errno 装箱与线程局部性、sys_call 的 EINTR 重试、expected 双出口链、errno 读取时机陷阱。

## 环境

| 项 | 值 |
|---|---|
| 内核 | 6.18.33.2-microsoft-standard-WSL2(WSL2) |
| CPU | AMD Ryzen 7 9700X 8-Core Processor |
| g++ | 16.2.1 20260810(GCC) |
| glibc | 2.44 |
| strace | 7.2 |

编译标准:**E1/E4 用 `-std=c++20`**,**E2/E3 用 `-std=c++23`**——`std::expected` 需要 C++23,实测 g++ 16.2.1 在 `-std=c++20` 下 `<expected>` 头能 include,但 `std::expected` 本体报 `'std::expected' is only available from C++23 onwards`。这不是选型偏好,是硬边界。

## 目录与实验对照

| 目录 | 实验 | 内容 |
|---|---|---|
| `01-errno-threadlocal/` | E1 | `errno_code()` 装箱(ENOENT/EBADF)+ 双线程 atomic 编排交错,各自保值 |
| `02-eintr-retry/` | E2 | `sys_call` 异常版/expected 版 + EINTR 自动重试;SA_RESTART 带/不带对比;strace 证据 |
| `03-expected-chain/` | E3 | `open_checked` 起、and_then/or_else 三层链、main 顶层转 system_error;附 `-O2` 汇编旁证 |
| `04-errno-clobber/` | E4 | 11 种"失败后的中间操作"逐一实测 errno 是否被冲掉,双基线跑两遍;附 getaddrinfo 污染收窄实验 |

## 每实验结论(摘录原始输出)

### E1:errno 是线程局部的,失败后立刻装箱才安全

`open` 不存在路径 → `errno_code().value()==2`(ENOENT),`message()=="No such file or directory"`,且 `ec == std::errc::no_such_file_or_directory` 成立;`write` 已关 fd → `value()==9`(EBADF)。双线程交错(A 失败 → B 失败 → 双方再取值):

```
[thread A] value = 2  (expect ENOENT=2) -> kept own value
[thread B] value = 9  (expect EBADF=9) -> kept own value
```

复跑 3 次一致。若 errno 是普通全局变量,A 在 B 失败之后取值必然串味。

### E2:EINTR 重试(重头戏)

两种模式信号都送达(handler 计数 = 1)、最终都读到数据,唯一差别是用户态见没见过 EINTR:

| 模式 | stdout 证据 |
|---|---|
| 不带 SA_RESTART | `signal delivered = 1, EINTR retries inside sys_call = 1` |
| 带 SA_RESTART | `signal delivered = 1, EINTR retries inside sys_call = 0` |

strace 证据链(`eintr_strace_norestart.txt` 第 15-24 行,内核把慢速 read 打断 → handler → 用户态拿到 -1/EINTR → sys_call 重试 → 拿到数据):

```
kill(106137, SIGUSR1 ...)                # 子进程 200ms 后发信号
<... read resumed>, 0x7ffc..., 64) = ? ERESTARTSYS (To be restarted if SA_RESTART is set)
--- SIGUSR1 {si_signo=SIGUSR1, si_code=SI_USER, si_pid=106181, ...} ---
rt_sigreturn({mask=[]})                  = -1 EINTR (Interrupted system call)   ← 用户态看到的 read 返回值
read(3 <unfinished ...>                  ← sys_call 的重试入口
write(4, "ping\n", 5) = 5                # 子进程 400ms 时投喂数据
<... read resumed>, "ping\n", 64) = 5
```

带 SA_RESTART 的对照(`eintr_strace_restart.txt`):`rt_sigreturn` 返回 0,紧随的 read 入口是内核的自动重启(rewind 重执行),不是用户态的新调用——程序计数器 `EINTR retries = 0` 与之互证。

注意两点:①strace 下被打断的 read 显示为内核内部的 `ERESTARTSYS`,该值不会逃逸到用户态;用户态实际拿到的是 `rt_sigreturn` 行显示的 `-1 EINTR`。程序 stdout 与 trace 同一次运行采集,证据链闭合。②两份 trace 的差别在 `rt_sigaction` 的 `sa_flags`(带不带 `SA_RESTART`)与 `rt_sigreturn` 返回值,read 序列形状相同——单看"read 重新进入"分不清是内核重启还是用户态重试,必须靠程序自己的计数器区分。

### E3:工具层 expected、应用顶层 system_error

四场景全通:`eintr_*.out` 之外的 `expected_chain.out`:正常文件走完 and_then 链;缺文件(ENOENT=2)、目录(EISDIR=21,错误生在链中层 read)、空文件(invalid_argument=22,非 errno 错误)三种失败都原样传到 main,or_else 层只记日志不改写,顶层一次性转 `std::system_error`:

```
caught at top: config '/tmp/errpar/e3_missing.conf': No such file or directory
    code: value = 2, category = generic
```

`-O2` 汇编旁证(`asm_probe_diff.txt`):纯值域场景里 `expected<int, error_code>` + `and_then` + `value_or` 与手写分支生成**逐条相同**的 5 条指令(`leal/testl/movl/cmovle/ret`),装箱、category 寻址、monadic 机制全部蒸发。边界照实记:`sizeof(std::expected<int, std::error_code>)=24`(error_code 本体 16),不可内联的 ABI 边界上它走内存返回(sret),不是零开销——零开销的说法只在内联可见的纯值域成立。

### E4:errno 读取时机(对照 Windows 篇 GetLastError)

11 种插入操作 × 双基线(基线值 ENOENT=2 与 EBADF=9 各跑一遍,防"污染了但值恰好相同"的盲区)。**glibc 2.44 实测**:

| 插入操作 | 结果 |
|---|---|
| `fprintf(stderr, ...)`(stderr 健康) | unchanged |
| 成功的裸 `write(2)` | unchanged |
| `strerror(ENOMEM)` | unchanged |
| 缓冲中的 `printf` | unchanged |
| `std::cout <<` | unchanged |
| `malloc` 1 MiB + free | unchanged |
| `std::string` 4 KiB 堆分配 | unchanged |
| `fopen("/tmp") + fclose` 成功 | unchanged |
| **`getaddrinfo("localhost")` 成功** | **POLLUTED,errno=6(ENXIO),确定性复现** |
| **又一次失败的 `open`** | **POLLUTED**(只在 ebadf 基线下可见:9→2;enoent 基线下 2→2 测不出) |
| **stderr 已被关时的 `fprintf`** | **POLLUTED**(2→9,EBADF;只在 enoent 基线下可见) |

getaddrinfo 收窄实验(`getaddrinfo_pollution.out`):数值地址(`AI_NUMERICHOST` 或纯 IP)路径 errno 保持 0,只有走名字解析(NSS)路径才留 ENXIO;strace 全程**零失败 syscall**,且本机 `/etc/gai.conf` 存在、systemd-resolved 应答成功——即 errno 是 glibc/systemd NSS 模块(`libnss_mymachines.so.2`/`libnss_resolve.so.2`,nsswitch 行 `hosts: mymachines resolve [!UNAVAIL=return] files myhostname dns`)在用户态代码里直接赋的值。**机器相关:nsswitch 配置不同结果可能不同,这正是文章的措辞依据——别赌"成功的库调用不动 errno",POSIX 从没承诺过。**

## 复现命令

```sh
# E1
g++ -std=c++20 -Wall -Wextra -pthread 01-errno-threadlocal/errno_threadlocal.cpp -o /tmp/e1 && /tmp/e1

# E2(先编译,再两种模式各跑一遍 + strace)
g++ -std=c++23 -Wall -Wextra 02-eintr-retry/eintr_retry.cpp -o /tmp/e2
/tmp/e2 norestart
/tmp/e2 restart
strace -f -tt -e trace=read,write,kill,rt_sigaction,rt_sigreturn,pipe,close,exit_group,exit,wait4,clone3,clock_nanosleep \
       -o /tmp/e2_strace.txt /tmp/e2 norestart

# E3(四场景:正常文件/缺文件/目录/空文件)
g++ -std=c++23 -Wall -Wextra 03-expected-chain/expected_chain.cpp -o /tmp/e3
printf 'resolution = 1920x1080\ntheme = dark\n' > /tmp/e3.conf && : > /tmp/e3_empty.conf
/tmp/e3 /tmp/e3.conf; /tmp/e3 /tmp/e3_missing.conf; /tmp/e3 /tmp; /tmp/e3 /tmp/e3_empty.conf
# 汇编旁证
g++ -std=c++23 -O2 -S 03-expected-chain/asm_probe.cpp -o /tmp/asm_probe.s

# E4(双基线各一遍)
g++ -std=c++20 -Wall -Wextra 04-errno-clobber/errno_clobber.cpp -o /tmp/e4
/tmp/e4 enoent; /tmp/e4 ebadf
g++ -std=c++20 -Wall -Wextra 04-errno-clobber/getaddrinfo_pollution.cpp -o /tmp/gi2 && /tmp/gi2
```

E2 的编排:父进程阻塞 `read` 管道读端(慢速 fd),子进程睡 200ms 后 `kill(SIGUSR1)`、再睡 200ms 后写 `"ping\n"`。复现对 WSL2 与裸 Linux 均成立,只要求 `/dev/null`、`/tmp` 可用。

## 备注(采证时的坑)

- **`.out` 与 strace 的配对**:`eintr_norestart.out`/`eintr_restart.out` 是 strace 运行同一次的 stdout(与 trace 文件同一进程),未加 strace 直跑的输出与其逐字节相同(已对拍)。
- **E4 输出交错**:`errno_clobber.out` 里部分 stderr 插入行出现在下一行 stdout 前,是 stderr 无缓冲与 stdout 行缓冲的正常交错,非乱序。
- **E3 输出交错**:`expected_chain.out` 里 `loading config...` 出现在 or_else 日志行之后,同理——经 `2>&1` 合流进管道时 stdout 整段缓冲,退出时才落盘。
- **E4 场景顺序**:"stderr 已被关闭"放最后,它永久关掉 fd 2,之后的 stderr 输出全部丢失。
- E4 的实验对象是裸 `errno`,故该文件刻意不定义 `errno_code()`——装箱会把读取时机固定住,恰好破坏本实验要观察的东西。
