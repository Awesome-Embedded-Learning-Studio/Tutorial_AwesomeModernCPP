# 深入理解C/C++的编译与链接技术8：库文件检索逻辑

## 前言

现在，我们需要讨论的是定位库文件的事情。定位库文件说的是——一个依赖了针对于本体而言的其他动态库文件的可执行文件，是如何找到这些其他动态库文件的？

这个问题并非小问题，仔细一想，在现代的软件工程中，我们几乎逃脱不了库文件的使用。比如说，我们制作的一些软件或者是使用的软件会将第三方的库集成到产品中使用，或者是以包管理模式为代表的，为了让一个给定的软件正确运行，我们需要在运行的时候定位正确的库文件。

几乎就是如此。

## 定位规则

Linux下的动态库是存在命名规范的，如果您注意的话，是可以发现所有的静态库都满足`lib + <library_name> + .a`的，这个时候，我们只需要告诉链接器`<library_name>`的部分，链接器会自动按照其他规则查找`lib<library_name>.a`的

动态库要更加复杂一丁点，因为动态库具备热替换属性（即无需从头编译软件发布），所以实际上命名规则还要复杂一点，简单的说是：

`lib + <library_name> + .so + <library version information>`

还是一样，我们只用提供`<library_name>`的部分，链接器会自动按照其他规则查找的。

`<library version information>`值得单独谈谈，一般而言，版本号就足以：`<M>.<m>.<p>`，也就是主版本号，次版本号和补丁版本号。这个是具体的名称，还有一个东西叫做soname，是只留下主版本号信息的动态库名称，即：`libz.so.1.2.3.4`的soname是libz.so.1。这个是《高级C/C++编译技术》的例子。

## 说一说运行启动时的动态库定位规则

现在我们需要聊聊动态库文件的运行定位规则。特别的，大家可能关心Linux运行时的动态库定位规则。这里说一下。在 Linux 上运行动态链接程序时，一个叫**动态链接器 / loader**（通常是 `ld-linux.so` / `ld.so`）的组件负责找到并加载可执行文件所需的共享库（`.so`）。动态库的查找规则看起来复杂，但其实是有明确优先级和几条常见“控制点”：`LD_PRELOAD`、可执行文件内嵌的 `RPATH`/`RUNPATH`、环境变量 `LD_LIBRARY_PATH`、系统配置（`/etc/ld.so.conf.d` + `ldconfig`）以及系统默认路径（如 `/lib`、`/usr/lib`）。

下面的部分，是各位需要了解的：**当动态链接器需要解析某个依赖**（即依赖名没有包含 `/`）时，通常按以下顺序查找（已简化）：

1. `LD_PRELOAD` 指定的库（优先加载，用于符号覆盖／注入）。
2. 如果可执行文件包含 `DT_RPATH` 并且没有 `DT_RUNPATH`，则使用 `DT_RPATH` 路径（注意：`DT_RPATH` 已被弃用，但仍被支持）。
3. 环境变量 `LD_LIBRARY_PATH`（**非 setuid/setgid 可执行文件会被忽略**）。
4. 如果可执行文件包含 `DT_RUNPATH`，使用 `DT_RUNPATH`（并且当存在 `DT_RUNPATH` 时，`DT_RPATH` 一般被忽略）。
5. ldconfig 维护的缓存 `/etc/ld.so.cache`，以及 `/lib`、`/usr/lib`（以及架构相关的 `/lib64`、`/usr/lib64`）这些“trusted directories”。
6. （如果前面都没找到）最终会失败并报错（如 `ld.so: cannot find ...`）。

> 注意：上面顺序的细节（特别是 `RPATH` 与 `RUNPATH` 的交互）由 linker 的实现与链接器选项（如 `--enable-new-dtags`，这个标识符是启用-R或者是-rpath链接器指导选项）影响。

------

## 详细说明（每项展开）

#### LD_PRELOAD（按需“注入”或覆盖符号）

`LD_PRELOAD` 是一个环境变量，可指定一个或多个共享库，**在正常搜索之前**被强制加载到进程中，从而可以用于拦截/替换符号（函数）。不过这个很少见，一般是不建议使用的，除非你知道你在做什么 :)

------

#### DT_RPATH 与 DT_RUNPATH（即 “rpath / runpath”）

在链接时，可以把一个或多个运行时库搜索路径写进可执行文件或共享库的动态段（`.dynamic`），对应 ELF tag 分别是 `DT_RPATH` 和 `DT_RUNPATH`。历史上的 `DT_RPATH` 早期引入，用法是“优先于环境变量”，但后来引入了 `DT_RUNPATH`（new-dtags），`DT_RUNPATH` 的含义是：**它在 `LD_LIBRARY_PATH` 之后被搜索**，即 `LD_LIBRARY_PATH` 可以覆盖 RUNPATH 中的路径；而 `DT_RPATH` 在某些实现/历史上会优先于 `LD_LIBRARY_PATH`（即更难被重写）。

另一个重要行为差别：**DT_RPATH 对传递依赖（transitive dependencies）有效**，而 **DT_RUNPATH 可能不会用于查找传递依赖**（即当可执行 -> libA -> libB 时，RUNPATH 的行为在某些情况下不会为 libB 的查找提供路径，而 RPATH 会）。这导致某些在旧链接器下以 RPATH 可运行的组合，在使用 RUNPATH（new-dtags）后会出现“找不到间接依赖”的情况。

笔者目前的Linux经验中的确很少接触到，所以建议更多的测试环境下，采用下面这个方案是合适的

------

#### LD_LIBRARY_PATH（这个是环境变量）

`LD_LIBRARY_PATH` 是运行时的库搜索路径列表，会被动态链接器在特定阶段使用（见顺序）。非常常用作临时覆盖系统路径或测试新版本库。**同样的**，setuid / setgid 可执行文件会忽略此变量（安全原因）。

环境变量的麻烦在于很容易干扰到所有的由设置了这个环境变量的shell不建议把生产环境长期依赖 `LD_LIBRARY_PATH`，因为它会影响所有通过该 shell 启动的子进程，并且不如系统配置（ldconfig）可维护。

```bash
export LD_LIBRARY_PATH=/opt/foo/lib:/home/you/sw/lib:$LD_LIBRARY_PATH
./myapp
```

------

#### ldconfig、/etc/ld.so.conf.d、以及 ld.so.cache

系统管理员通常通过把库目录放到 `/etc/ld.so.conf` 或 `/etc/ld.so.conf.d/*.conf` 来告诉 `ldconfig` 哪些目录要被系统动态链接器信任。`ldconfig` 会扫描这些目录并生成一个二进制缓存 `/etc/ld.so.cache`（提高查找速度），同时创建符号链接（libXXX.so -> libXXX.so.VERSION）。动态链接器会读取该缓存来加速查找。

常见操作：

```bash
# 把新目录加入配置（以 root）
echo "/opt/foo/lib" > /etc/ld.so.conf.d/foo.conf
# 重建缓存
sudo ldconfig
# 查看缓存内容
ldconfig -p | grep foo
```

------

#### 系统默认目录（trusted directories）

动态链接器通常会默认搜索 `/lib`、`/usr/lib`（以及在 64-bit 系统上的 `/lib64`、`/usr/lib64`），这些目录被称为“trusted directories”。`ldconfig` 也会处理这些目录。即使没有把一个路径写入 `ld.so.conf`，把库放在这些目录通常也能被找到（但要注意架构位、ABI、版本匹配）。

## 那咱们Windows呢？

Windows 的可执行/装载器与 API（`LoadLibrary` / `LoadLibraryEx` / 自动加载 via import table）定义了一套搜索顺序和安全改进。

一般而言，Windows的方式有两种：隐式（导入表）与显式（运行时 API）

**隐式加载（implicit）**指的是可执行文件的导入表（Import Table）在进程启动或模块加载时由系统装载器解析，系统会为每个 `DLL` 尝试找到并映射到进程地址空间。开发者在链接阶段指定依赖（例如 `kernel32.dll`、`mydll.dll`），加载由系统在进程启动时期自动完成。

**显式加载（explicit）**指的是代码在运行时使用 `LoadLibrary` / `LoadLibraryEx` 等 API 手工加载 DLL，然后用 `GetProcAddress` 取得函数指针。显式加载能通过参数控制搜索行为（例如使用 `LOAD_LIBRARY_SEARCH_USER_DIRS` 等标志）。

#### 默认搜索顺序（概念化顺序）

> 注意：Windows 的搜索顺序在不同 OS 版本与配置下有细微差别，且系统提供了影响该顺序的设置（下文会说明）。这里先给出一个概念化的常见顺序（理解优先级即可）：

当进程请求加载名为 `foo.dll`（未指定绝对路径）时，系统通常按以下顺序查找（概念顺序）：

1. **调用方显式指定的完整路径**（如果调用 `LoadLibrary("C:\\path\\foo.dll")`，则直接加载该路径，不走搜索）。
2. **加载器首先查看是否为“KnownDLLs”中的条目**（KnownDLLs 是注册在系统中的一组受信任的系统库，优先使用系统已存在版本）。
3. **应用程序目录（Executable directory）**：可执行程序（.exe）所在目录（通常优先于系统目录，具体受 SafeDllSearchMode 等设置影响）。
4. **系统目录**（通常为 `%SystemRoot%\System32`）。
5. **Windows 目录**（通常为 `%SystemRoot%`）。
6. **当前工作目录（Current Directory）**（取决于 SafeDllSearchMode；若启用“安全搜索模式”，current directory 的位置会被推后）。
7. **PATH 环境变量中列出的目录**（按顺序）。
8. **如果启用了应用配置或 Side-by-side（SxS）/manifest 特性**，会优先解析 manifest 中声明的绑定版本或来自 WinSxS 的并行程序集。

重点是：**如果你使用了绝对路径或相对可执行文件路径，系统不会去 PATH 搜索**；反之如果只给了裸名 `foo.dll`，就会按上面顺序尝试。

