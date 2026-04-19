#include "stack.h"

#include <stdlib.h>

struct Stack {
    int* data;
    int capacity;
    int top;
};

Stack* stack_create(int capacity)
{
    Stack* s = malloc(sizeof(Stack));
    if (!s) return NULL;

    s->data = malloc((size_t)capacity * sizeof(int));
    if (!s->data) {
        free(s);
        return NULL;
    }
    s->capacity = capacity;
    s->top = 0;
    return s;
}

void stack_destroy(Stack* s)
{
    if (s) {
        free(s->data);
        free(s);
    }
}

int stack_push(Stack* s, int value)
{
    if (s->top >= s->capacity) return -1;
    s->data[s->top++] = value;
    return 0;
}

int stack_pop(Stack* s, int* out)
{
    if (s->top <= 0) return -1;
    *out = s->data[--s->top];
    return 0;
}

int stack_size(const Stack* s)
{
    return s->top;
}
