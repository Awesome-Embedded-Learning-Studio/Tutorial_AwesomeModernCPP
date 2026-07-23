#include <stdio.h>

typedef struct {
    int max_connections;
    int timeout_ms;
    const char* server_name;
} Config;

const Config* get_config(void);

int main(void) {
    get_config(); // 应当输出"Initializing..."
    printf("%d %d %s\n", get_config()->max_connections, get_config()->timeout_ms,
           get_config()->server_name); // 应当输出"5 500 localhost"
    return 0;
}

const Config* get_config(void) {
    // config 用 static 初始化器：程序加载时一次性初始化，正好呼应题目说的
    // "static 局部变量只在第一次进入函数时被初始化"
    static Config config = {5, 500, "localhost"};
    // 但题目还要求第一次调用时打印 "Initializing..."——静态初始化器本身
    // 没有运行时钩子去打印，所以再用一个 static flag 控制只打印一次
    static int initialized = 0;
    if (!initialized) {
        printf("Initializing...\n");
        initialized = 1;
    }
    return &config;
}
