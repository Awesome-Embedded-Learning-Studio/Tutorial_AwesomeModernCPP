// DMA 通道 RAII 示例 - 管理 DMA 资源的生命周期
// 演示如何使用 RAII 管理 DMA 通道，确保资源正确释放

#include <cstdint>
#include <cstdio>

// 模拟 DMA HAL
namespace hal {
    inline void dma_allocate(uint8_t ch) {
        printf("[HAL] DMA channel %d allocated\n", ch);
    }

    inline void dma_free(uint8_t ch) {
        printf("[HAL] DMA channel %d freed\n", ch);
    }

    inline bool dma_start(uint8_t ch, void* src, void* dst, size_t len) {
        printf("[HAL] DMA ch%d: %p -> %p, len=%zu started\n", ch, src, dst, len);
        return true;
    }

    inline void dma_abort(uint8_t ch) {
        printf("[HAL] DMA channel %d aborted\n", ch);
    }

    inline bool dma_wait(uint8_t ch, unsigned timeout_ms) {
        printf("[HAL] DMA ch%d: waiting (timeout=%u ms)...\n", ch, timeout_ms);
        return true;  // 模拟成功
    }
}

class DMAChannel {
public:
    DMAChannel(uint8_t ch) noexcept : ch_(ch), running_(false) {
        hal::dma_allocate(ch_);
    }

    ~DMAChannel() noexcept {
        if (running_) {
            // 不要在析构中长时间等待，只执行非阻塞的中止
            hal::dma_abort(ch_);
            running_ = false;
        }
        hal::dma_free(ch_);
    }

    DMAChannel(const DMAChannel&) = delete;
    DMAChannel& operator=(const DMAChannel&) = delete;
    DMAChannel(DMAChannel&&) = delete;

    bool start(void* src, void* dst, size_t len) noexcept {
        running_ = hal::dma_start(ch_, src, dst, len);
        return running_;
    }

    // 可选：调用者显式等待（可能会阻塞）
    bool wait_until_done(unsigned timeout_ms) noexcept {
        return hal::dma_wait(ch_, timeout_ms);
    }

    uint8_t channel() const noexcept { return ch_; }
    bool is_running() const noexcept { return running_; }

private:
    uint8_t ch_;
    bool running_;
};

// 使用示例
void transfer_example() {
    DMAChannel dma(0);

    uint8_t src_buf[128] = {0};
    uint8_t dst_buf[128] = {0};

    printf("Starting DMA transfer...\n");
    if (dma.start(src_buf, dst_buf, sizeof(src_buf))) {
        // 显式等待完成
        dma.wait_until_done(100);
        printf("DMA transfer complete\n");
    }

    // DMAChannel 析构函数会自动释放通道
}

// 演示异常安全（即使发生异常也会正确清理）
void safe_transfer() {
    DMAChannel dma(1);

    uint8_t src[64] = {0};
    uint8_t dst[64] = {0};

    dma.start(src, dst, sizeof(src));

    // 模拟某些条件导致提前退出
    bool error_condition = false;
    if (error_condition) {
        return;  // DMAChannel 析构函数仍然会被调用
    }

    dma.wait_until_done(50);
}

int main() {
    printf("=== DMA Channel RAII Example ===\n\n");

    transfer_example();

    printf("\n");

    safe_transfer();

    printf("\n=== Demonstration complete ===\n");
    printf("Notice how DMA channels are properly freed even on early exit.\n");

    return 0;
}
