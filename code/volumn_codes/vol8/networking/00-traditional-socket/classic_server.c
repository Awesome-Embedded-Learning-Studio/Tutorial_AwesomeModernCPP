/* 传统 C 风格 echo server —— 裸 fd、手动 close、errno + perror。
 * 配套 documents/vol8-domains/networking/00-traditional-socket-basics.md。
 * 这就是从 Stevens《UNIX 网络编程》/ Beej's Guide 学的那套经典写法。
 * 单线程:专注演示"服务器五步",不掺并发(并发是 01 的事)。
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 13013
#define BACKLOG 64
#define BUFSZ 4096

int main(void) {
    signal(SIGPIPE, SIG_IGN); /* 否则 write 到已关连接会触发 SIGPIPE 杀进程 */

    /* 第一步:socket() 向内核要一个通信端点,返回 fd */
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        perror("socket");
        return 1;
    }

    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    /* 第二步:bind() 把 fd 钉到本地地址 */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); /* 监听所有网卡 */
    addr.sin_port = htons(PORT);              /* 网络字节序 */
    if (bind(lfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    /* 第三步:listen() 标记为被动监听,内核开始接受连接 */
    if (listen(lfd, BACKLOG) < 0) {
        perror("listen");
        return 1;
    }

    printf("classic echo server on 0.0.0.0:%d (pid %d)\n", PORT, getpid());

    for (;;) {
        /* 第四步:accept() 取出一个已完成握手的连接,返回新 fd */
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            continue;
        }

        /* 第五步:在连接 fd 上 read/write */
        char buf[BUFSZ];
        for (;;) {
            ssize_t n = read(cfd, buf, BUFSZ);
            if (n <= 0)
                break;          /* 0 = 对端关闭;<0 = 出错 */
            write(cfd, buf, n); /* 教学版:假设一次写完 */
        }
        close(cfd); /* 手动 close——漏了就泄漏 fd */
    }
    return 0;
}
