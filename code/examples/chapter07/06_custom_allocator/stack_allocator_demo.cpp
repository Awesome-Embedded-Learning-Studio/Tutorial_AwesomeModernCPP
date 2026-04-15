// stack_allocator_demo.cpp - 栈分配器演示
#include "stack_allocator.h"
#include <iostream>
#include <cstring>

struct FrameData {
    int frame_num;
    float values[8];

    FrameData(int fn) : frame_num(fn) {
        for (int i = 0; i < 8; ++i) {
            values[i] = fn * 0.1f + i;
        }
    }

    void display() const {
        std::cout << "  Frame " << frame_num << ": [";
        for (int i = 0; i < 8; ++i) {
            std::cout << values[i];
            if (i < 7) std::cout << ", ";
        }
        std::cout << "]\n";
    }
};

int main() {
    std::cout << "=== Stack Allocator Demo ===\n\n";

    constexpr std::size_t BUFFER_SIZE = 4096;
    static char buffer[BUFFER_SIZE];
    StackAllocator allocator(buffer, BUFFER_SIZE);

    std::cout << "Initial state:\n";
    std::cout << "  Capacity: " << allocator.capacity() << " bytes\n";
    std::cout << "  Used: " << allocator.used() << " bytes\n\n";

    // 模拟帧分配
    std::cout << "=== Frame 1 allocation ===\n";
    auto mark1 = allocator.mark();

    FrameData* frame1 = static_cast<FrameData*>(allocator.allocate(sizeof(FrameData)));
    if (frame1) {
        new (frame1) FrameData(1);
        frame1->display();
    }

    // 临时分配
    float* temp_data = static_cast<float*>(allocator.allocate(32 * sizeof(float)));
    if (temp_data) {
        std::cout << "  Allocated 32 floats for temp data\n";
    }

    std::cout << "  Used after frame 1: " << allocator.used() << " bytes\n\n";

    std::cout << "=== Frame 2 allocation ===\n";
    auto mark2 = allocator.mark();

    FrameData* frame2 = static_cast<FrameData*>(allocator.allocate(sizeof(FrameData)));
    if (frame2) {
        new (frame2) FrameData(2);
        frame2->display();
    }

    double* more_temp = static_cast<double*>(allocator.allocate(16 * sizeof(double)));
    if (more_temp) {
        std::cout << "  Allocated 16 doubles for temp data\n";
    }

    std::cout << "  Used after frame 2: " << allocator.used() << " bytes\n\n";

    std::cout << "=== Rolling back to mark2 ===\n";
    allocator.rollback(mark2);
    std::cout << "  Used after rollback: " << allocator.used() << " bytes\n\n";

    std::cout << "=== Frame 3 allocation ===\n";
    FrameData* frame3 = static_cast<FrameData*>(allocator.allocate(sizeof(FrameData)));
    if (frame3) {
        new (frame3) FrameData(3);
        frame3->display();
    }
    std::cout << "  Used after frame 3: " << allocator.used() << " bytes\n\n";

    // 清理 frame1
    frame1->~FrameData();
    allocator.rollback(mark1);

    std::cout << "=== After rolling back to mark1 ===\n";
    std::cout << "  Used: " << allocator.used() << " bytes\n\n";

    std::cout << "=== Key use cases ===\n";
    std::cout << "- Frame-based allocation (games, simulations)\n";
    std::cout << "- Temporary scratch buffers\n";
    std::cout << "- Scope-based resource cleanup\n";
    std::cout << "- Exception-safe rollback\n";

    return 0;
}
