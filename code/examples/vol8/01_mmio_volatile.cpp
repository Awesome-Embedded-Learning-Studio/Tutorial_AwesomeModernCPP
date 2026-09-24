// volatile 对 MMIO 轮询的意义:host 可编译的最小对照
// 配套 documents/vol8-domains/embedded/f103/01-led/01-mmio-essence.md
// 点"运行"正常结束;点开汇编(x86-64 -O2)对比两个等待循环:
// plain 版被编译器证明"条件恒假",循环整个消失,一次内存都不读;
// volatile 版老老实实保留一次 load —— 编译器对外设状态的"知情权"
// 就这么一点点,但这正是咱们要的:它不许自作主张。

#include <cstdio>
#include <cstdint>

// 假装是外设状态寄存器:硬件会在咱们看不见的时刻改它
std::uint32_t status_plain = 0;
volatile std::uint32_t status_mmio = 0;

int main() {
    // 场景:等状态位置 1 再往下走(轮询)。
    // 主线程先置位,两个循环都能正常退出,程序能跑完;
    // 差别藏在汇编里 —— 看 Compiler Explorer 的输出对比。

    status_plain = 1;
    while (status_plain == 0) {
        // -O2:编译器看着上面那行赋值,断定条件恒假,
        // 整个循环被删掉,puts 前连一次 load 都没有。
    }
    std::puts("plain: passed without a single load");

    status_mmio = 1;
    while (status_mmio == 0) {
        // -O2:volatile 在场,编译器必须每次真的读内存。
        // 真寄存器的状态由硬件改写,读,是咱们唯一的消息来源。
    }
    std::puts("volatile: at least one honest load happened");
}
