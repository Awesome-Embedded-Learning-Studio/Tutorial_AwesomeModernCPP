# 深入理解C/C++编译技术——动态库A4：链接时符号缺失行为与运行时动态加载

这一篇博客会更加重要一些，这里我们计划讨论的是各个平台上（Windows和GNU/Linux），当我们的可执行文件生成或者是其他库文件依赖的符号存在未定义时，不同平台的表现；以及比较重要的动态库动态加载编程。

## 链接时符号缺失行为的平台差异

这个很有趣，我们讨论的时在链接发生的时候，平台之间对存在未定义符号的容忍程度分析。在Windows上，动态库生成的时候，我们就已经要求不允许存在未定义符号，一旦发生未定义的符号，我们的工具链就会抱怨道找不到符号。

而在Linux上不会存在这样的事情。事实上，Linux的策略更加宽容，默认的情况下，我们允许符号未定义，直到上进程的时候，加载器会检查所有的依赖确保所有的重要符号都是被正确编址的。直到那个时候才会确认我们的程序是否真的存在重要的问题。

当然，如果您希望这种很严格的检查，有办法的：那就是在编译可重定位文件的时候传递`-Wl,-no-undefined`选项，来指导后续的链接器的报错行为即可。

## 运行时动态加载是什么？

官方的说，运行时动态链接（dynamic loading）指程序**在运行时**按需加载一个共享库（shared object / dynamic library / DLL），并查找需要的符号（函数、变量）后调用。笔者认为，**这是插件系统的一个重要的实现机制。**因为现在：

- 我们可以动态的加载进入插件，在运行时根据配置加载不同功能模块（国际化、渲染后端、驱动等）。
- 上述特性允许我们可以按照需求加载我们需要的依赖，节约一部分空间
- 并且可以在运行时就支持热替换/扩展，至少，我们无需重编译主程序就可以扩展功能了。

## 好处多多，有麻烦嘛？

还真有，我们的错误处理要更加的小心了，毕竟，我们会有类似——符号对不上，加载失败了等一系列麻烦的问题，以及建议搞一个统一的管理类处理这些导出的符号，这是有原因的——插件好就好在随时可以安装和卸载，卸载之后，我们一定不能继续调用其函数或访问其静态资源。笔者认为可以搞一个类似QPointer那种带有Expire机制的函数包装对象访问之。

## 一些系统层次的API

这里枚举一部分系统层次的API

- `void *dlopen(const char *filename, int flag);`
  - `flag` 常用：`RTLD_LAZY`（延迟解析符号）、`RTLD_NOW`（立即解析所有需要符号）、`RTLD_LOCAL`（符号本地）、`RTLD_GLOBAL`（符号可被随后加载的库解析）
- `void *dlsym(void *handle, const char *symbol);` 返回指向函数/变量的指针
- `int dlclose(void *handle);` 卸载
- `char *dlerror(void);` 获取错误说明（非线程安全的实现可能返回静态字符串）

Windows 对应：

- `HMODULE LoadLibrary(LPCSTR lpFileName);`当然还有EX版本，这里笔者建议您移步到Microsoft的MSDN文档一探究竟：[LoadLibraryExW 函数 （libloaderapi.h） - Win32 apps | Microsoft Learn](https://learn.microsoft.com/zh-cn/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw)
- `FARPROC GetProcAddress(HMODULE hModule, LPCSTR lpProcName);`
- `BOOL FreeLibrary(HMODULE hModule);`
- `DWORD GetLastError(void);` + `FormatMessage` 获得可读字符串

## 最小 C 动态库 + 程序（Linux） — C 风格函数导出

举个例子，笔者编写了一个简单的动态库

```c
// mylib.c
#include <stdio.h>

int add(int a, int b) {
    return a + b;
}

const char *hello(void) {
    return "Hello from mylib";
}
```

在Linux下，我们这样构建动态库

```bash
# 生成共享库
gcc -fPIC -shared -o libmylib.so mylib.c
# 编译主程序（下面会用 dlopen）
gcc -o main main.c -ldl
```

随后编写一个使用的main.c来处理之：

```c
// main.c
#include <stdio.h>
#include <dlfcn.h>

int main(void) {
    /* Pass here a valid path */
    /* So place the dynamic library same place */
    void *h = dlopen("./libmylib.so", RTLD_NOW); 
    if (!h) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }

    // 查找 symbol
    int (*add)(int,int) = (int(*)(int,int))dlsym(h, "add");
    const char *(*hello)(void) = (const char*(*)(void))dlsym(h, "hello");
    char *err = dlerror();
    if (err) {
        fprintf(stderr, "dlsym error: %s\n", err);
        dlclose(h);
        return 1;
    }

    printf("add(2,3) = %d\n", add(2,3));
    printf("%s\n", hello());

    dlclose(h);
    return 0;
}
```

**运行**

```bash
# 确保当前目录可被加载（或设置 LD_LIBRARY_PATH）
export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH
./main
```

------

## Windows 下的 DLL 和 LoadLibrary（MinGW / MSVC）

### mylib.c（Windows DLL）

```c
// mylib.c
#include <windows.h>

__declspec(dllexport) int add(int a, int b) {
    return a + b;
}

__declspec(dllexport) const char* hello(void) {
    return "Hello from mylib.dll";
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    return TRUE;
}
```

**构建（MSVC Developer Command Prompt）**

```cmd
cl /LD mylib.c /Fe:mylib.dll
```

**构建（MinGW）**

```bash
gcc -shared -o mylib.dll -Wl,--out-implib,libmylib.a -Wl,--export-all-symbols -fPIC mylib.c
```

### main.c（使用 LoadLibrary）

```c
// main_win.c
#include <windows.h>
#include <stdio.h>

typedef int (*add_t)(int,int);
typedef const char* (*hello_t)(void);

int main(void) {
    HMODULE h = LoadLibraryA("mylib.dll");
    if (!h) {
        DWORD e = GetLastError();
        printf("LoadLibrary failed: %lu\n", e);
        return 1;
    }

    add_t add = (add_t)GetProcAddress(h, "add");
    hello_t hello = (hello_t)GetProcAddress(h, "hello");
    if (!add || !hello) {
        printf("GetProcAddress failed\n");
        FreeLibrary(h);
        return 1;
    }
    printf("add(10,20) = %d\n", add(10,20));
    printf("%s\n", hello());

    FreeLibrary(h);
    return 0;
}
```

**运行（在 DLL 同目录下或把 DLL 加到 PATH）**

```cmd
set PATH=%CD%;%PATH%
main_win.exe
```

------

## C++ 插件接口与 extern "C" 工厂（推荐做法）

当需要导出 C++ 对象或类时，常见策略是导出一个工厂函数（`extern "C"`）返回不透明指针，或导出一张 `struct` 的函数表（接口表），避免 C++ 名字修饰影响。

```c
// plugin.h
#ifdef __cplusplus
extern "C" {
#endif

typedef struct PluginAPI {
    int (*init)(void);
    void (*shutdown)(void);
    int (*do_work)(int arg);
} PluginAPI;

// 导出工厂：返回函数表指针
PluginAPI* create_plugin_api(void);

#ifdef __cplusplus
}
#endif
```

### plugin_impl.c（插件实现）

```c
// plugin_impl.c
#include "plugin.h"
#include <stdio.h>

static int my_init(void) { printf("plugin init\n"); return 0; }
static void my_shutdown(void) { printf("plugin shutdown\n"); }
static int my_do_work(int arg) { printf("plugin do work %d\n", arg); return arg*2; }

static PluginAPI api = {
    .init = my_init,
    .shutdown = my_shutdown,
    .do_work = my_do_work
};

PluginAPI* create_plugin_api(void) {
    return &api;
}
```

主程序只需通过 `dlsym(h, "create_plugin_api")` 拿到 `PluginAPI*`，就能无缝调用插件函数，无需关心 C++ 名字修饰。

## 笔者遇到的一些问题，和笔者使用的排查手段积累

#### **为什么 `dlsym` 拿不到我在 C++ 中的函数？**

笔者当时手搓PDF浏览器，然后准备做插件系统的时候，被干过，我在之前的博客中谈到C++ 编译器会对符号名进行修饰（name mangling）。自然解决方案就是用 `extern "C"` 导出 C 风格接口，或者是笔者说的上面的方案。

#### **Windows 的 `GetProcAddress` 失败怎么排查？**

检查导出名称（使用 `dumpbin /EXPORTS` 或 `nm`），检查调用约定是否匹配（`__stdcall` 会改变导出名），或是否使用了 C++ 名称修饰。建议 `__declspec(dllexport)` + `extern "C"`。











