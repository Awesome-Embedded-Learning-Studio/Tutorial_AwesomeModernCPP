#ifndef STACK_H
#define STACK_H

typedef struct Stack Stack;

Stack* stack_create(int capacity);
void stack_destroy(Stack* s);
int stack_push(Stack* s, int value);
int stack_pop(Stack* s, int* out);
int stack_size(const Stack* s);

#endif /* STACK_H */
