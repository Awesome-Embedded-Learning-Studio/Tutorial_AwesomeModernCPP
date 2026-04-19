#include <stdio.h>
#include <stddef.h>

typedef struct {
    int max_connections;
    int timeout_ms;
    const char* server_name;
} Config;

const Config* get_config(void)
{
    static Config instance = {0};
    static int initialized = 0;

    if (!initialized) {
        instance.max_connections = 100;
        instance.timeout_ms = 5000;
        instance.server_name = "localhost";
        initialized = 1;
        printf("Config initialized (first call)\n");
    }
    return &instance;
}

int main(void)
{
    const Config* cfg1 = get_config();
    const Config* cfg2 = get_config();

    printf("cfg1 == cfg2: %s\n", cfg1 == cfg2 ? "yes (same instance)" : "no");
    printf("max_connections: %d\n", cfg1->max_connections);
    printf("timeout_ms: %d\n", cfg1->timeout_ms);
    printf("server_name: %s\n", cfg1->server_name);

    return 0;
}
