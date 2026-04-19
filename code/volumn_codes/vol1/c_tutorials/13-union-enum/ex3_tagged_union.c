#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef enum {
    kTagInt,
    kTagFloat,
    kTagString,
} ValueType;

typedef struct {
    ValueType tag;
    union {
        int   int_val;
        float float_val;
        char  string_val[64];
    } data;
} TaggedValue;

TaggedValue make_int(int v)
{
    TaggedValue tv = {.tag = kTagInt, .data = {.int_val = v}};
    return tv;
}

TaggedValue make_float(float v)
{
    TaggedValue tv = {.tag = kTagFloat, .data = {.float_val = v}};
    return tv;
}

TaggedValue make_string(const char* v)
{
    TaggedValue tv = {.tag = kTagString};
    strncpy(tv.data.string_val, v, sizeof(tv.data.string_val) - 1);
    tv.data.string_val[sizeof(tv.data.string_val) - 1] = '\0';
    return tv;
}

void print_tagged_value(const TaggedValue* tv)
{
    switch (tv->tag) {
    case kTagInt:
        printf("Int(%d)\n", tv->data.int_val);
        break;
    case kTagFloat:
        printf("Float(%.4f)\n", tv->data.float_val);
        break;
    case kTagString:
        printf("String(\"%s\")\n", tv->data.string_val);
        break;
    }
}

int get_as_int(const TaggedValue* tv, int* out)
{
    if (tv->tag != kTagInt) return -1;
    *out = tv->data.int_val;
    return 0;
}

int get_as_float(const TaggedValue* tv, float* out)
{
    if (tv->tag != kTagFloat) return -1;
    *out = tv->data.float_val;
    return 0;
}

int get_as_string(const TaggedValue* tv, const char** out)
{
    if (tv->tag != kTagString) return -1;
    *out = tv->data.string_val;
    return 0;
}

int main(void)
{
    TaggedValue values[] = {
        make_int(42),
        make_float(3.14f),
        make_string("hello tagged union"),
    };

    for (int i = 0; i < 3; i++) {
        print_tagged_value(&values[i]);
    }

    // Safe accessor demo
    int ival;
    if (get_as_int(&values[0], &ival) == 0) {
        printf("Safely got int: %d\n", ival);
    }

    if (get_as_int(&values[1], &ival) != 0) {
        printf("Correctly rejected: float is not int\n");
    }

    return 0;
}
