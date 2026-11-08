---
title: "QEMU 里的同款心跳:第二块板零改,兑现薄 port"
description: "8efe93a 给 ZerOS 搬来第二块板:QEMU 的 mps2-an385,13 个新文件的体量、445 行新增,内核的代码一行没动。去 CMSIS 的 arch 层让双板共用,CMSDK UART 的扁平四件套对着 STM32 USART 摆出另一种长相,内存型的代码区没有 Flash 等待;我用 run-mps2-heartbeat 在 QEMU 串口看到了心跳,两份 main.cpp 逐字 diff 下来只差文件头的注释"
chapter: 9
order: 1
tags:
  - stm32f1
  - intermediate
  - 嵌入式
  - CMake
  - 工具链
difficulty: intermediate
platform: stm32f1
cpp_standard: [23]
reading_time_minutes: 20
prerequisites:
  - "在栈顶伪造出生证明:PendSV、SVC 与第一次切换"
related:
  - "跑五秒,说出你的台词:CI 冒烟、include 防火墙与体积闸门"
  - "拔掉心跳做实验:NO_TICK、僵尸任务与以 patch 随库的探针"
---

# QEMU 里的同款心跳:第二块板零改,兑现薄 port

上一站的收尾停在真正的开发板上,三篇的战场咱们挨个报一遍:

- 《把秒表焊进硅片:时钟树爬上 72MHz,量第一刀切换》把时钟爬上了 72MHz,拿 DWT 量了切换的第一刀
- 《教科书输给预取缓冲器:.ramfunc 反转与一本诚实的实验册》让咱们看到教科书也会输给预取缓冲器
- 《拔掉心跳做实验:NO_TICK、僵尸任务与以 patch 随库的探针》给性能线收了官

硅片上的事告一段落了,咱们回头办一件更早许下的事:把同一份 ZerOS 搬上第二块板。

咱们要拆的 commit 是 `8efe93a`,subject 写的是 “feat: add qemu mps2_an385”,它交了 13 个新文件,新增了 445 行,又删了 6 行。主角是一块咱们没见过也没摸过的板:QEMU 的 mps2-an385。它是 QEMU 自带的 Cortex-M3 开发板模型,对应的是 Arm 的 MPS2+ 评估板,不过那块真板咱们没有碰过,任何厂商的库它都没带,寄存器的长相全由仿真器说了算。咱们挑它当第二块板,图的恰恰是它的干净:Cortex-M3 的核被它端端正正摆了出来,任何一家厂商的启动代码都没有跟过来。

“搬上第二块板”的事,咱们从拆 CMSIS 那会儿就盼着了。咱们在《注释写 Lowest,硅片判 Highest:拆掉 CMSIS 的优先级移位》里把厂商库从 port 里请了出去,薄 port 的念头那时就埋下了:port 薄一分的板,换起来就省一分的事。日子到了。咱们不听注释自夸,拿构建、拿 QEMU 串口的日志、拿逐字的 diff 来对,兑现了没有,看完咱们自己就知道。

## 内核一行没动,加号只有一个

咱们看 `8efe93a` 在 `src/CMakeLists.txt` 里留下的全部足迹:

```diff
 if(NOT ZEROS_BUILD_TESTS) # boards only exist for the cross build
     add_subdirectory(board/stm32f103_bluepill)
+    add_subdirectory(board/mps2_an385)
 endif()
```

`include/ZerOS/kernel/` 与 `src/arch/arm_cortex_m3/` 底下的文件,这个 commit 连它们的一个字节都没有动。新板子整个住进了 `src/board/mps2_an385/` 自己的包里,而 example 的注册文件里有一份公共源清单,咱们看它:

```cmake
# everything a demo links besides its own main.cpp: the arm bridge(与
# bluepill 共用同一套 arch 源)+ 板级启动与 UART
set(ZEROS_DEMO_COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/src/arch/arm_cortex_m3/fault.cpp
    ${CMAKE_SOURCE_DIR}/src/arch/arm_cortex_m3/launch.cpp
    ${CMAKE_SOURCE_DIR}/src/arch/arm_cortex_m3/system.cpp
    ${CMAKE_SOURCE_DIR}/src/arch/arm_cortex_m3/this_task.cpp
    ${CMAKE_SOURCE_DIR}/src/arch/arm_cortex_m3/time_kernel_cm3.cpp
    ${CMAKE_SOURCE_DIR}/src/arch/arm_cortex_m3/switch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../uart.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../startup.cpp
)
```

咱们对着上面的清单数过,bluepill 的 demo 链接的也是同一批 arch 文件。板包自己的 `CMakeLists.txt` 里,作者留了一句自评:

```cmake
# 同一份 src/arch/arm_cortex_m3/ 直接服务两块板——port 薄的红利首次兑现
```

红利兑现了没有,注释说了不算,咱们往后看构建与运行。不过有一个检查替咱们站了岗:《跑五秒,说出你的台词:CI 冒烟、include 防火墙与体积闸门》里讲过的 include 防火墙,拦的就是内核头文件里冒出来的任何板卡与厂商的名字。咱们在 `include/ZerOS/kernel/` 里翻不出 mps2,也翻不出 qemu 的字样,翻出来才叫出事了。

## CMSDK UART:另一种长相的串口

换板换的最多的就是串口。mps2 板上的 UART0 用的是 CMSDK APB UART,CMSDK 是 Arm 为 Cortex-M 系统定下的一套标准外设模板,APB UART 是其中的一种。咱们分三段看它的 `uart.cpp`,设备的家安在 0x40004000,头一段认的是寄存器的门牌:

```cpp
namespace {
// CMSDK APB UART 寄存器(字对齐,偏移即下标)
auto* const kData    = reinterpret_cast<volatile std::uint32_t*>(0x40004000);
auto* const kState   = reinterpret_cast<volatile std::uint32_t*>(0x40004004);
auto* const kCtrl    = reinterpret_cast<volatile std::uint32_t*>(0x40004008);
auto* const kBaudDiv = reinterpret_cast<volatile std::uint32_t*>(0x40004010);

constexpr std::uint32_t kStateTxBufferFull = (1u << 0);
constexpr std::uint32_t kCtrlTxEnable      = (1u << 0);
} // namespace
```

见过 bluepill 那套 USART 的您一眼就能看出差别有多深。STM32 的 USART 上电是睡着的,咱们得开时钟门控,得配引脚的复用,它的状态位藏在 SR 里,而使能位藏在 CR1 里,寄存器一家子各有各的住处。CMSDK APB UART 是另一副长相:DATA、STATE、CTRL、BAUDDIV 四个字对齐的寄存器一字排开,发送前看一眼 STATE 的 TXBF 位,空了就往 DATA 写一个字节,就算完事了。

门牌认完了,咱们看初始化:

```cpp
void uart0_init() {
    // 无引脚无时钟门控可配——QEMU 的外设上电即在;只喂分频与使能
    *kBaudDiv = 1;          // 全速:QEMU 模型对 chardev 直通,分频不构成节流
    *kCtrl = kCtrlTxEnable; // TXEN(接 RX 中断时再补 RXEN 与 NVIC)
}
```

`uart0_init` 里的两行把初始化说全了:咱们往 BAUDDIV 写 1 就够了,QEMU 的模型对字符设备直通,分频担不了节流的差事,再往 CTRL 置上了 TXEN,发送就算使能了。寄存器的型号与基址,注释里交代了出处:取自 QEMU 11.0.3 的 info qtree,设备的名号是 cmsdk-apb-uart。作者认的是仿真器给的地皮,不认任何手册上的假定,在它的世界里,最终的裁决就是 QEMU 自己。咱们写 QEMU 的板包,这套习惯咱们值得记着,手册上查不到的东西,仿真器的检查命令就是手册。

发送的一个字节怎么出去,咱们看 `uart0_putc`:

```cpp
void uart0_putc(char c) {
    while ((*kState & kStateTxBufferFull) != 0) {
    }
    *kData = static_cast<std::uint8_t>(c);
}
```

发送本身是轮询的,咱们忙着等 TXBF 清零再写。注释里写着它的适用范围,任务、ISR、遗言的现场它都在场。轮询是要占着 CPU 的,这就是它的代价,换来的好处是不依赖任何中断,fault 的现场照样能把最后几个字打印出去。《芯片死前立遗嘱:HardFault 现场、栈金丝雀与零分配日志》里装的遗言机制,靠的正是这样的通道。

通道怎么接进内核,咱们看文件的末尾:

```cpp
namespace {
// the channel body: everything log::print produces lands here, byte-polled
bool channel_body(const char* s) {
    while (*s != '\0') {
        uart0_putc(*s++);
    }
    return true;
}
} // namespace

void register_uart_channel() {
    ZerOS::log::RegisterGlobalChannel(&channel_body);
}
```

内核的日志只认 `RegisterGlobalChannel` 注册进来的通道,通道背后是 STM32 的 USART 还是 CMSDK 的 UART,内核对背后的硬件没有概念,而它也不该有。当初咱们把 UART 的抽象停在通道注册而不搬进内核,理由就在这里:抽象一旦进了内核,咱们今天就为一块板改上一处,明天又得为另一块板的细节改上一处,改到了最后,内核就成了堆放板级细节的地方。

## 内存当代码区:没有 Flash 等待的地皮

第二处大变样出在内存的地图上。咱们看 `link.ld` 的开头:

```ld
/* mps2_an385 链接脚本 —— QEMU mps2-an385(Cortex-M3 通用开发板模型,无真机)
 * 内存映射取自 QEMU 11.0.3 info mtree(最终裁决就是这台仿真器):
 *   0x00000000  ssram1  4MB(代码,内存型 SRAM——无 Flash 等待,写它即生效)
 *   0x20000000  ssram23 4MB(数据) */

ENTRY(reset_handler)

MEMORY
{
    CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
    RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}
```

bluepill 那边的 CODE 区是一块真 Flash,取指得按手册的表算等待周期,上一站咱们数过那些周期。mps2-an385 是没有真 Flash 的,QEMU 把代码区做成了内存型 SRAM,CPU 从里面取指是零等待的,写进去的内容即刻生效。于是有一件反过来的事值得您留意:`.ramfunc` 的段咱们保留着,注释却写明了它在这块板上没有启用的理由:

```ld
        /* RAM 常驻热路径开关段(与 bluepill 同构;F103 上实测负优化,
         * 本板内存型代码区无等待周期,更没有启用的理由) */
```

《教科书输给预取缓冲器:.ramfunc 反转与一本诚实的实验册》实测过,把热路径搬进 RAM 反而是 F103 上的负优化。到了零等待的代码区上,搬就更没有理由了。性能实测的册子反过来影响了板级设计,这样的流向咱们乐见。

链接期的反制四连也搬过来了,咱们看一眼,它与 bluepill 是同构的:

```ld
PROVIDE(__cxa_atexit = ASSERT(0, "global destructors banned"));
PROVIDE(_Znwj = ASSERT(0, "heap new banned"));
PROVIDE(_Znwm = ASSERT(0, "heap new banned"));
PROVIDE(__cxa_throw = ASSERT(0, "exceptions banned"));
```

咱们把四连的意思念一遍:谁在代码里引入了全局析构、堆 new、异常,链接当场就把构建拦了下来,`.init_array` 的长度断言跟在后面,运行期的全局构造同样进不来。裸机上 freestanding C++ 的四条拒绝,咱们把视角挪到这块新板上数一数,数出来的依然是整齐的四条。

启动代码 `startup.cpp` 是 bluepill 的同构版,不过它没有厂商库可用,向量表要咱们亲手写,`.data` 的拷贝、`.bss` 的清零也归咱们亲手写,连 `memset` 都得自己供(`-nostdlib` 的世界没有人替咱们准备它)。有两处细节我挑给您看。头一处是外设中断槽的填法,向量表的 16 到 47 号槽全填了兜底的 `fault_handler`,注释写的是兜底打印比 nullptr 的 lockup 好查,万一哪个外设误触发了,咱们看到的是一行 `[FAULT] halted` 而不是一声不响的死机。另一处在 HardFault 的填法旁边,注释写着 QEMU 对未映射访问直达 BusFault 的槽位,这是仿真器自己的路径,咱们在 QEMU 上排查越界的时候,头一个该看的就是 BusFault 槽。

QEMU 上跑 demo 的小约定,咱们看 `sim/qemu/run-qemu.cmake`:

```cmake
# demo 是常驻循环,timeout 到点收场:退出码 124 = 预期内正常,其余 = 真失败

execute_process(
    COMMAND timeout ${QEMU_SECONDS} qemu-system-arm -M mps2-an385 -cpu cortex-m3
            -nographic -monitor none -serial mon:stdio -kernel ${QEMU_ELF}
    WORKING_DIRECTORY ${QEMU_ROOT}
    RESULT_VARIABLE rc
)
if(rc EQUAL 124)
    message(STATUS "(timeout - normal exit for a resident demo)")
elseif(NOT rc EQUAL 0)
    message(FATAL_ERROR "qemu exited with ${rc}")
endif()
```

demo 的主体是死循环,一个永远跑不完的循环,timeout 到了点就把它收掉,退出码 124 是 timeout 自己的暗号,脚本认下了它,咱们把它当成正常的收场。启动参数里的 `-M mps2-an385` 选板,`-cpu cortex-m3` 选的是核,`-nographic` 把显示拆了,串口通向咱们的终端,咱们的输出就从这里来。

## 同一个 commit 里,CI 的日志过滤也拆了

13 个文件里混着三处与 QEMU 无关的改动,落脚的地方都在 `scripts/ci.sh` 里,咱们照实交代。咱们看 host 检查原来的样子:

```bash
-    cmake -B "$HOST_DIR" -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug >/dev/null
-    cmake --build "$HOST_DIR" -j"$NCPU" 2>&1 | grep -E 'error|warning' && fail "host build" || true
+    # logs are printed unconditionally — success or failure, full output
+    cmake -B "$HOST_DIR" -DZEROS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
+    cmake --build "$HOST_DIR" -j"$NCPU" || fail "host build"
```

size 检查里有同款的一行,删的时候作者留了一句英文注释,我译过来摆在您面前:grep 过滤曾把真正的报错行吞掉,却拿单纯的警告当了失败本身。构建的输出几百行,过滤的用意是省日志,可构建真死掉的时候,报错的上下文跟着进了过滤器的下水道,排查的人手里只剩几行残句。改法是无条件的全量打印,成败的日志都打,用真实的退出码判失败。咱们在《跑五秒,说出你的台词》里对过这套过滤的形状,当时的原话是咱们不替它圆场,到了这个 commit,作者自己把它拆了。第三个落点在 smoke 检查里:Renode 的输出原来被重定向进了 /dev/null,咱们什么都看不见,这个 commit 把重定向拆了,demo 的现场就直通日志了。

## 上机:构建 mps2 目标,串口里看心跳

代码导览完了,咱们把它跑起来。我的复现链路照旧:拿 `git archive 8efe93a` 把树导到 /tmp 的副本里(third_party 下是 gitfile 的老处理,从首切那篇起咱们一直在用),工具链我本机是 arm-none-eabi-g++ 16.2.0,QEMU 是我本机的 11.1.2。注释里的内存映射取自 11.0.3 的 info mtree,两个版本的映射有没有逐位出入,我没法当场对,因为我的电脑上只有 11.1.2,下面贴的输出都是它跑出来的。

构建与运行的两步,仓库给咱们备好了目标:

```shell
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arch/arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --target zeros-mps2-demo-heartbeat
cmake --build build --target run-mps2-heartbeat
```

我跑的头一个目标把固件编了出来,链接器当场报了内存的使用量:

```text
Memory region         Used Size  Region Size  %age Used
            CODE:        3928 B         4 MB      0.09%
             RAM:        2588 B         4 MB      0.06%
size report: demo-heartbeat (mps2)
   text	   data	    bss	    dec	    hex
   3440	    488	   2100	   6028	   178c
```

4MB 的地皮,固件用了不到 4KB,数字给咱们壮胆够了。bluepill 的 heartbeat 在 CI 的 size 检查里报过 flash 3812、RAM 2460,mps2 版的 3928 与 2588 跟它量级贴着。不过我照实说,我的构建带 Release,CI 的口径不带,这两组数字只能当量级的参照,逐字节的比对咱们不做。

第二个目标把编与跑接在了一起:编完直接进 QEMU,五秒的仿真,输出直通了终端。咱们看真实的串口:

```text
ZerOS demo: heartbeat (mps2)
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
Hello! ZerOS
A heartbeat example!
qemu-system-arm: terminating on signal 15 from pid 173443 (timeout)
-- (timeout - normal exit for a resident demo)
[100%] Built target run-mps2-heartbeat
```

QEMU 的串口里,心跳跟 bluepill 板上的一模一样。开机的一行横幅,然后是两个任务轮流的台词,五秒里说了十对。节奏咱们数过了:两个任务的优先级同级(prio 1),各自睡的是 500ms,说完了就轮到下一个,一句的间隔是 500ms,五秒凑了十对。收场是 timeout 发的 signal 15,脚本认下的 124 码没有出现意外分支,cmake 报了正常完成,整个目标的退出码落在了 0 上。

开机仪式咱们也值得对一眼。`demo_setup.hpp` 的 setup 与 bluepill 的同名同形,注释里写明的差异只有 UART 编号与主频的差别:

```cpp
inline void setup(const char* name) {
    board::clock_init();  // 固定 25MHz,空操作——仪式同形
    board::uart0_init();
    board::register_uart_channel(); // this board talks over UART0
    log::print("\r\nZerOS demo: {}\r\n", name);
    arch::cortex_m3::install_default_testaments(); // obligations, explicitly
    arch::cortex_m3::init_time(25'000'000 / 1000);  // 1kHz tick @25MHz
}
```

bluepill 那边的 clock_init 要爬时钟树,咱们得把 HSE 进 PLL 出的每一步亲手配出来,72MHz 是一步步配出来的。这边的 QEMU 上电就是 25MHz,`clock_init` 留了个空操作,开机仪式的形状不变,内容缩成了一行。时基的算式跟着主频走,咱们算的是 25'000'000 除以 1000,一毫秒一拍地走,跟 72MHz 那边算的是同一个除法,不同的只是数字。心跳的 500ms 就是从这里来的。

## 逐字 diff:两份 main.cpp 差在哪

压轴的对质轮到了应用层。bluepill 版与 mps2 版的 heartbeat `main.cpp` 咱们各取了一份,咱们拿 diff 摆在一起:

```diff
--- src/board/stm32f103_bluepill/example/heartbeat/main.cpp
+++ src/board/mps2_an385/example/heartbeat/main.cpp
@@ -1,7 +1,8 @@
 /**
  * @file main.cpp
  * @author CharlieChen114514 (725610365@qq.com)
- * @brief All right, your bootstrap example
+ * @brief   All right, your bootstrap example — mps2_an385/QEMU port
+ *          (与 bluepill/heartbeat 同源零改:demo_setup 同名同形)
  * @version 0.1
  * @date 2026-09-06
  *
@@ -32,7 +33,7 @@
 }

 int main() {
-    ZerOS::demo::setup("heartbeat");
+    ZerOS::demo::setup("heartbeat (mps2)");

     auto shape = ZerOS::task::named("HeartBeat").prio(1).words(64);
```

diff 的全部结果,就是文件头的 Doxygen 注释,外加 `setup()` 的名字字符串,还有一个文件末尾补上的换行。剩下的正文一个字都没有差:`HeartBeatPackage` 两个包、heartbeat 的任务体、`task::named("HeartBeat").prio(1).words(64)` 的任务工厂、两次 `launch`、`start_scheduler`,全都还是原来的样子。注释里的自评写着同源零改,咱们拿 diff 验过了,证明它的自评没有说大话。换了一块板,换了一种串口,换了内存地图,应用的代码一行没动,这句话从前是咱们对薄 port 的期待,现在是咱们手里的一份 diff。

## 验收

咱们把本篇的可查证事实归拢一下,一共归出了三件,咱们一件一件来。

头一件是复现的链路。您 checkout 到 `8efe93a`(或照咱们的老办法拿 `git archive` 导树),上面的三条命令您照着敲,您的 Memory region 该报 CODE 3928、RAM 2588(编译器的版本会影响小数,我的数字出自 16.2.0,这点您记着)。`run-mps2-heartbeat` 的收场该是 signal 15 的 timeout,日志该带上的暗号是一句 “(timeout - normal exit for a resident demo)”,目标整体的退出码是 0,串口里该看到的是横幅加十对台词。

第二件是 diff 的对质。两份 `main.cpp` 咱们摆在一起 diff,您该得到的就只有文件头的注释、`setup()` 的名字与末尾的换行,出现任何一行别的改动,都不该算在零改的范围内。

第三件是没验的,我照实摆给您。映射的出处是 QEMU 11.0.3 的 info mtree,我本机是 11.1.2,老版本我没装,两版映射有没有出入我没法当场对。GitHub Actions 云端的 CI 我没有跑过,验到的只是本地的部分。mps2-an385 在真实世界上有一块对应的开发板,咱们没有碰它,CMSDK UART 的寄存器行为只在 QEMU 里验过,真板上的表现不在咱们今天的验收里。时序上我只数了对数与节奏(五秒十对),QEMU 的虚拟时间走得多准,咱们没有量。

## 下一篇

第二块板站住了,四个考场里属于 QEMU 的格子填上了。轮到的下一个 commit 是 `fb9072b`,subject 写的是 “feat: stack watermark, tickless”,一个 commit 装了两个特性,咱们照例拆成两篇:栈的金丝雀升级成全栈涂刷,水位报表回答的是每个任务用了多少栈,tickless 的 idle 把心跳调成 one-shot,回答的是睡着的时候能省多少中断。心跳咱们在这块新板上数过了,下一篇咱们学着把它调慢。咱们那儿见。
