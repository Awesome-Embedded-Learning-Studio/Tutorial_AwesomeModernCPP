# 06-argv:E7 命令行与 argv(GetCommandLineW / CommandLineToArgvW)

文件:`e7_argv.cpp` / `e7_argv.out`(模式:`argv`=子模式,打印三方对账;无参=驱动)。链接 `-lshell32`。

## 两节结论

**[1] 花式引号三方对账**
父进程发:`"…exe" argv plain "two words" "quoted ""inner"" text" tail`。孩子打印 GetCommandLineW 原文、CommandLineToArgvW 拆解、main 的 argv,逐参数对账:

- 空格参数 `"two words"`:三方一致,一个参数
- **内嵌引号 `"quoted ""inner"" text"`:CRT 与 CommandLineToArgvW 拆得不一样**——MinGW CRT(按 MSVC 规则:`""` 产出一个引号且留在引号态)拆成单个参数 `quoted "inner" text`;CommandLineToArgvW 拆成 `quoted "inner` 和 `text tail` 两个。判定行 `NO (content differs)`
- 教训:**"main 的 argv 就是 CommandLineToArgvW 的结果"这句话在这套工具链上不总成立**,两份解析器在边界(内嵌引号)上分歧;写需要精确引号语义的代码(shell、参数转发)要么自己实现规则,要么指明用哪一份

**[2] argv[0] 造假**
app=真身路径,cmdline 首 token 写 `TOTALLY_NOT_ME.EXE`:孩子的 argv[0] 与 CommandLineToArgvW[0] **双双是假名**(双方一致,判定 YES)。argv[0] 只是父进程写的第一个 token,没有任何"它是本尊路径"的保证——Linux execve 的 argv[0] 同样可造假,这一行两边同构。

## 复现

```sh
cd 06-argv
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -municode e7_argv.cpp -o e7_argv.exe -lshell32
chmod +x e7_argv.exe && ./e7_argv.exe > e7_argv.out 2>&1
```

注意:子进程复用本二进制(`argv` 模式),不需要额外产物。
