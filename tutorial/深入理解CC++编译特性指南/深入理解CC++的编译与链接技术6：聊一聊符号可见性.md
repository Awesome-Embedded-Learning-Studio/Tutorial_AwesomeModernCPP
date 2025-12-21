# 深入理解C/C++编译技术——动态库A3：聊一聊符号可见性

有笔者朋友可能感到奇怪——符号的可见性是什么呢？是不是咱们的C++中的关键字：`public`或者是`private`呢？值得指出的是，并不是，前者是语言语法和编译器检查一并提供的基础功能，这里，我们讨论的符号可见性更加的激进，是指代在符号ABI层次的可见性。

#### Tips:如何查看ABI符号

> 老手可以直接略过

介于一些朋友可能是第一次看到这篇文章，可能会尚不清楚如何完成“查看给定可重定位文件或者是由重定位文件组成的可执行文件或者是库文件所包含的可见符号”这个要求，笔者这里计划专门补充一下在主要的Windows平台或者是Linux平台上如何完成这个基本操作。

##### GNU/Linux平台

很简单，我们只需要使用nm工具即可。假设我们有一个库文件`libsome_helpers.so`准备检查，那么输入如下指令就OK了。

```
[charliechen@Charliechen runaable_dynamic_library]$ nm -D libsome_helpers.so 
00000000000010e9 T add
                 w __cxa_finalize@GLIBC_2.2.5
                 w __gmon_start__
                 w _ITM_deregisterTMCloneTable
                 w _ITM_registerTMCloneTable
00000000000010fd T minus
```

##### Windows平台

这个好说，假设笔者打算检查的是CCWidget.dll，查看导出的符号就是`dumpbin /EXPORTS CCWidgets.dll`

```
D:\NewQtProjects\CCWidgetLibrary\build\Desktop_Qt_6_10_0_MSVC2022_64bit-Release\widgets>dumpbin /EXPORTS CCWidgets.dll
Microsoft (R) COFF/PE Dumper Version 14.44.35217.0
Copyright (C) Microsoft Corporation.  All rights reserved.


Dump of file CCWidgets.dll

File Type: DLL

  Section contains the following exports for CCWidgets.dll

    00000000 characteristics
    FFFFFFFF time date stamp
        0.00 version
           1 ordinal base
         481 number of functions
         481 number of names

    ordinal hint RVA      name

          1    0 00002F50 ??0AnimationConfig@animation@CCWidgetLibrary@@QEAA@$$QEAU012@@Z
          2    1 00002F80 ??0AnimationConfig@animation@CCWidgetLibrary@@QEAA@AEBU012@@Z
          3    2 00002FB0 ??0AnimationConfig@animation@CCWidgetLibrary@@QEAA@XZ
          4    3 00002FD0 ??0AnimationSession@animation@CCWidgetLibrary@@QEAA@$$QEAU012@@Z
          5    4 00003010 ??0AnimationSession@animation@CCWidgetLibrary@@QEAA@AEBU012@@Z
          6    5 00003050 ??0AnimationSession@animation@CCWidgetLibrary@@QEAA@XZ
          7    6 00012E00 ??0AppearAnimation@animation@CCWidgetLibrary@@QEAA@PEAVQWidget@@@Z
          8    7 000184E0 ??0CCBadgeLabel@@QEAA@PEAVQWidget@@@Z
          9    8 00014130 ??0CCButton@@QEAA@AEBVQIcon@@AEBVQString@@PEAVQWidget@@@Z
         10    9 000141F0 ??0CCButton@@QEAA@AEBVQString@@PEAVQWidget@@@Z、
         ...
```

## 主流工具链是如何控制符号的可见性的？

所以回归正题，主流工具链是如何控制符号的可见性的呢？我们分开讨论

#### GNU Linux下如何控制符号的可见性

##### 方式1：直接对编译器传递-fvisibility控制所有符号的导出

第一种方式是最粗暴的方式，假设我们存在一个私有的依赖工程完全不想暴露任何符号，这个时候，我们就可以在编译的时候向gcc/g++传递-fvisibility。默认的讲，对于GNU C/C++工具链**对于任何未加以任何可见性修饰或者是指定可见性的符号**都是公开的。也就是`-fvisibility=default`，假设我们想要隐藏，在生成动态库的步骤中，需要我们指定为`-fvisibility=hidden`，所有的符号都会不被导出。不过笔者是没有使用过的，仅仅是查询到存在用法。

##### 方式2：最常用的方式：利用`__attribute__((visibility(< "default" | "hidden" >)))`

笔者很喜欢这样指定，以笔者自己充当玩具编写的一个简单的日志库为例子，对于所有计划在ABI层次公开的API，笔者强制指定`__attribute__((visibility("default")))`，反之，任何不应该被使用的符号，施加以`__attribute__((visibility("hidden")))`

```
#ifdef CCLOG_BUILD_SHARED
#define CCLOG_API __attribute__((visibility("default")))
#define CCLOG_PRIVATE_API __attribute__((visibility("hidden"))) 
#else
#define CCLOG_API
#define CCLOG_PRIVATE_API 
#endif
```

##### 方式3：对于一组聚合符号的修饰`#pragma visibility push/pop`

假如说，您实在是要处理手头巨量的符号的可见性修改，但是不想一个一个符号的添加以笔者上面提到的为例子的宏，您可以采用编译器的预处理指令。

```cpp
#pragma visibility push("hidden")

int private_api_add(int a, int b);
int api_minus(int a, int b);

/* Remember to pop for preventing the leak of unwanted visibility decorations */
#pragma visibility pop
```

#### Windows MSVC是如何操作的

很不幸，Windows DLL动态库导出符号，是存在一套相对复杂的修饰机制的。也就是说，对于计划导出的符号，需要被修饰以`__declspec(dllexport)`导出；然后使用这些符号的时候，我们还需要标记`__declspec(dllimport)`

```cpp
#ifdef CCLOG_BUILD_SHARED
/* If we plan to exports sysbols to DLL, we need to decorate symbols by this */
/* Others in case can use the symbols */
#define CCLOG_API __declspec(dllexport)
#else
/* If we plan to import sysbols from DLL, we need to decorate symbols by this */
#define CCLOG_API __declspec(dllimport)
#end
```

