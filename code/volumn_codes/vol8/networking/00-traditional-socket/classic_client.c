/* 传统 echo client:connect → write 一行 → read 回显。
 * 配套 documents/vol8-domains/networking/00-traditional-socket-basics.md。
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 13013

int main(int argc, char** argv) {
    const char* msg = (argc > 1) ? argv[1] : "hello";

    int fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    write(fd, msg, strlen(msg));

    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("echo <- '%s'\n", buf);
    } else {
        printf("no reply (read=%zd)\n", n);
    }

    close(fd);
    return 0;
}
