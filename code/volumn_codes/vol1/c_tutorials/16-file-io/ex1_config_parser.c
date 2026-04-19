#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>

#define kMaxEntries 32
#define kMaxLineLen 256

typedef struct {
    char key[64];
    char value[192];
} ConfigEntry;

char* trim(char* str)
{
    // Trim leading whitespace
    while (*str && isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;

    // Trim trailing whitespace
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return str;
}

size_t parse_config(const char* path, ConfigEntry* entries, size_t max_entries)
{
    FILE* fp = fopen(path, "r");
    if (!fp) return 0;

    char line[kMaxLineLen];
    size_t count = 0;

    while (fgets(line, sizeof(line), fp) && count < max_entries) {
        char* trimmed = trim(line);

        // Skip empty lines and comments
        if (*trimmed == '\0' || *trimmed == '#') continue;

        // Find '=' separator
        char* eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        char* key = trim(trimmed);
        char* val = trim(eq + 1);

        strncpy(entries[count].key, key, sizeof(entries[count].key) - 1);
        strncpy(entries[count].value, val, sizeof(entries[count].value) - 1);
        count++;
    }

    fclose(fp);
    return count;
}

const char* find_config(const ConfigEntry* entries, size_t count, const char* key)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(entries[i].key, key) == 0) {
            return entries[i].value;
        }
    }
    return NULL;
}

int main(void)
{
    // Create a test config file
    const char* config_path = "/tmp/test_config.ini";
    FILE* fp = fopen(config_path, "w");
    if (fp) {
        fprintf(fp, "# Test configuration\n");
        fprintf(fp, "host = localhost\n");
        fprintf(fp, "port = 8080\n");
        fprintf(fp, "\n");
        fprintf(fp, "max_connections=100\n");
        fprintf(fp, "debug = true\n");
        fclose(fp);
    }

    ConfigEntry entries[kMaxEntries];
    size_t count = parse_config(config_path, entries, kMaxEntries);

    printf("Parsed %zu config entries:\n", count);
    for (size_t i = 0; i < count; i++) {
        printf("  %-20s = %s\n", entries[i].key, entries[i].value);
    }

    const char* host = find_config(entries, count, "host");
    if (host) printf("\nFound host: %s\n", host);

    return 0;
}
