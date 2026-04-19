#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define kMaxAllocations 1024

typedef struct {
    void*      ptr;
    size_t     size;
    const char* file;
    int        line;
    int        freed;
} AllocRecord;

static AllocRecord g_records[kMaxAllocations];
static int g_record_count = 0;

void* tracked_malloc(size_t size, const char* file, int line)
{
    void* ptr = malloc(size);
    if (ptr && g_record_count < kMaxAllocations) {
        g_records[g_record_count].ptr = ptr;
        g_records[g_record_count].size = size;
        g_records[g_record_count].file = file;
        g_records[g_record_count].line = line;
        g_records[g_record_count].freed = 0;
        g_record_count++;
    }
    return ptr;
}

void tracked_free(void* ptr)
{
    if (!ptr) return;
    for (int i = 0; i < g_record_count; i++) {
        if (g_records[i].ptr == ptr && !g_records[i].freed) {
            g_records[i].freed = 1;
            break;
        }
    }
    free(ptr);
}

void mem_report(void)
{
    printf("\n=== Memory Report ===\n");
    int leaks = 0;
    for (int i = 0; i < g_record_count; i++) {
        if (!g_records[i].freed) {
            printf("LEAK: %zu bytes at %p, allocated at %s:%d\n",
                   g_records[i].size, g_records[i].ptr,
                   g_records[i].file, g_records[i].line);
            leaks++;
        }
    }
    printf("Total allocations: %d, Leaks: %d\n", g_record_count, leaks);
}

#define TMALLOC(size) tracked_malloc((size), __FILE__, __LINE__)

int main(void)
{
    atexit(mem_report);

    void* a = TMALLOC(100);
    void* b = TMALLOC(200);
    void* c = TMALLOC(50);

    printf("Allocated 3 blocks\n");

    tracked_free(a);
    tracked_free(c);
    // Intentionally NOT freeing b to demonstrate leak detection

    printf("Freed 2 blocks, 1 intentionally leaked\n");
    return 0;
}
