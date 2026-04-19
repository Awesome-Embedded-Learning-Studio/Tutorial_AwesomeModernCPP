#include <stdio.h>
#include <string.h>
#include <stddef.h>

size_t str_split(const char* input, char delim,
                 const char** out_starts, size_t* out_lengths, size_t max_tokens)
{
    size_t count = 0;
    const char* p = input;

    while (*p != '\0' && count < max_tokens) {
        // Skip leading delimiters
        while (*p == delim) p++;
        if (*p == '\0') break;

        out_starts[count] = p;
        size_t len = 0;
        while (*p != '\0' && *p != delim) {
            len++;
            p++;
        }
        out_lengths[count] = len;
        count++;
    }

    return count;
}

int main(void)
{
    const char* input = "hello,,world,,,foo,bar";
    const char* starts[10];
    size_t lengths[10];

    size_t count = str_split(input, ',', starts, lengths, 10);

    printf("Input: \"%s\"\n", input);
    printf("Tokens (%zu):\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("  [%zu] \"%.*s\" (len=%zu)\n", i, (int)lengths[i], starts[i], lengths[i]);
    }

    return 0;
}
