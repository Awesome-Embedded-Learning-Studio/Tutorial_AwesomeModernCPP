#include <cstddef>
#include <cstdint>

template <std::size_t BlockSize, std::size_t BlockCount> class FixedPool {
    struct Block {
        alignas(std::max_align_t) std::uint8_t data[BlockSize];
    };

    Block pool_[BlockCount];
    std::size_t free_list_[BlockCount];
    std::size_t free_head_;
    std::size_t used_count_;

  public:
    FixedPool() : free_head_(0), used_count_(0) {
        for (std::size_t i = 0; i < BlockCount; ++i) {
            free_list_[i] = i + 1;
        }
        free_list_[BlockCount - 1] = static_cast<std::size_t>(-1);
    }

    void* allocate() {
        if (free_head_ == static_cast<std::size_t>(-1)) {
            return nullptr;
        }

        const std::size_t index = free_head_;
        free_head_ = free_list_[index];
        ++used_count_;
        return pool_[index].data;
    }

    void deallocate(void* ptr) {
        if (!ptr) {
            return;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(&pool_[0]);
        const auto current = reinterpret_cast<std::uintptr_t>(ptr);
        const std::size_t index = (current - base) / sizeof(Block);

        free_list_[index] = free_head_;
        free_head_ = index;
        --used_count_;
    }

    std::size_t used_count() const { return used_count_; }
};

FixedPool<32, 8> pool;

void* allocate_packet_buffer() {
    return pool.allocate();
}

void release_packet_buffer(void* buffer) {
    pool.deallocate(buffer);
}

std::size_t used_packet_buffers() {
    return pool.used_count();
}
