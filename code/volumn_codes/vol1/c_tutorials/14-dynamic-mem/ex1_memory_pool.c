#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

typedef struct MemoryPool MemoryPool;

struct MemoryPool {
    size_t block_size;
    size_t total_count;
    size_t used_count;
    void*  free_list;
    void*  raw_memory;
};

MemoryPool* pool_create(size_t block_size, size_t block_count)
{
    size_t actual_size = block_size < sizeof(void*) ? sizeof(void*) : block_size;

    MemoryPool* pool = malloc(sizeof(MemoryPool));
    if (!pool) return NULL;

    pool->raw_memory = malloc(actual_size * block_count);
    if (!pool->raw_memory) {
        free(pool);
        return NULL;
    }

    pool->block_size = actual_size;
    pool->total_count = block_count;
    pool->used_count = 0;

    // Build free list
    char* block = (char*)pool->raw_memory;
    pool->free_list = block;
    for (size_t i = 0; i < block_count - 1; i++) {
        void** current = (void**)(block + i * actual_size);
        *current = block + (i + 1) * actual_size;
    }
    void** last = (void**)(block + (block_count - 1) * actual_size);
    *last = NULL;

    return pool;
}

void* pool_alloc(MemoryPool* pool)
{
    if (!pool || !pool->free_list) return NULL;

    void* block = pool->free_list;
    pool->free_list = *(void**)block;
    pool->used_count++;
    return block;
}

void pool_free(MemoryPool* pool, void* block)
{
    if (!pool || !block) return;

    *(void**)block = pool->free_list;
    pool->free_list = block;
    pool->used_count--;
}

void pool_destroy(MemoryPool* pool)
{
    if (pool) {
        free(pool->raw_memory);
        free(pool);
    }
}

int main(void)
{
    MemoryPool* pool = pool_create(32, 5);
    if (!pool) {
        fprintf(stderr, "Failed to create pool\n");
        return 1;
    }

    printf("Pool created: %zu blocks of %zu bytes\n",
           pool->total_count, pool->block_size);

    void* blocks[5];
    for (int i = 0; i < 5; i++) {
        blocks[i] = pool_alloc(pool);
        printf("Allocated block %d: %p (used=%zu)\n", i, blocks[i], pool->used_count);
    }

    void* fail = pool_alloc(pool);
    printf("6th allocation: %s\n", fail ? "unexpectedly succeeded" : "correctly returned NULL");

    pool_free(pool, blocks[2]);
    printf("Freed block 2, used=%zu\n", pool->used_count);

    void* reused = pool_alloc(pool);
    printf("Reused block: %p (same as blocks[2]=%p? %s)\n",
           reused, blocks[2], reused == blocks[2] ? "yes" : "no");

    pool_destroy(pool);
    return 0;
}
