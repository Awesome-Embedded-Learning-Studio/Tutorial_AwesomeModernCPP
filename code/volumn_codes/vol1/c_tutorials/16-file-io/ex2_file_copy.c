#include <stdio.h>
#include <stdlib.h>

#define kBufferSize 4096

int copy_file(const char* src_path, const char* dst_path)
{
    FILE* src = fopen(src_path, "rb");
    if (!src) {
        perror("Cannot open source");
        return -1;
    }

    FILE* dst = fopen(dst_path, "wb");
    if (!dst) {
        perror("Cannot create destination");
        fclose(src);
        return -1;
    }

    // Get source file size for progress
    fseek(src, 0, SEEK_END);
    long file_size = ftell(src);
    fseek(src, 0, SEEK_SET);

    unsigned char buffer[kBufferSize];
    long total_copied = 0;

    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        size_t bytes_written = fwrite(buffer, 1, bytes_read, dst);
        if (bytes_written != bytes_read) {
            perror("Write error");
            fclose(dst);
            fclose(src);
            return -1;
        }
        total_copied += (long)bytes_read;

        if (file_size > 0) {
            int progress = (int)(total_copied * 100 / file_size);
            printf("\rCopying: %3d%% (%ld/%ld bytes)", progress, total_copied, file_size);
            fflush(stdout);
        }
    }
    printf("\n");

    fclose(dst);
    fclose(src);
    return 0;
}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        // Demo mode: copy this source file
        printf("Usage: %s <src> <dst>\n", argv[0]);
        printf("Demo: copying this source file...\n");

        int result = copy_file(__FILE__, "/tmp/ex2_file_copy_backup.c");
        if (result == 0) {
            printf("Copy succeeded!\n");
        }
        return result;
    }

    return copy_file(argv[1], argv[2]);
}
