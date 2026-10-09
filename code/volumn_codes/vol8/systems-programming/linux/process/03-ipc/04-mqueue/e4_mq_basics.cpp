// E4a POSIX 消息队列基本盘:mq_open 建命名对象(名字空间在 /dev/mqueue,
// 与 shm 的 /dev/shm 同构)、mq_getattr 看四属性、消息边界保留(三条不同长度原样到达)、
// 优先级语义(高 prio 先出,同 prio 按发送序 FIFO)、mq_unlink 后目录项消失。
// 消息格式:头 3 字节 "M1"/"M2"/"M3",其余全用标签字符填充,收发对账一目了然。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 e4_mq_basics.cpp -o e4_mq_basics
#include "ipc_util.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mqueue.h>

namespace {
const char* kName = "/lp03_e4";

void show_dir(const char* note) {
    std::printf("%s", note);
    std::fflush(stdout);
    if (system("ls -l /dev/mqueue/ | grep -v '^total'") != 0) {
    }
}

// 发一条消息:头 3 字节 id,其余全填 tag,长度 len
void send_msg(mqd_t mq, unsigned prio, size_t len, char tag, const char* id) {
    char buf[512];
    std::memset(buf, tag, len);
    std::memcpy(buf, id, 3);
    sys_call(
        "mq_send", [](mqd_t m, const char* b, size_t l, unsigned p) { return mq_send(m, b, l, p); },
        mq, buf, len, prio);
    std::printf("  发送 prio=%u 长度=%zu 字节(id=%s,填充字符 '%c')\n", prio, len, id, tag);
}
} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);

    // 建队列:10 条上限、单条最长 256
    // 字节(非特权上限:/proc/sys/fs/mqueue/{msg_max=10,msgsize_max=8192})
    mq_unlink(kName);
    struct mq_attr want{};
    want.mq_maxmsg = 10;
    want.mq_msgsize = 256;
    mqd_t mq = mq_open(kName, O_CREAT | O_RDWR | O_EXCL, 0600, &want);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        return 1;
    }
    std::printf("mq_open(\"%s\") → mqd=%d;命名对象落在哪?看目录:\n", kName, (int)mq);
    show_dir("  [ls -l /dev/mqueue/]\n");

    struct mq_attr attr{};
    mq_getattr(mq, &attr);
    std::printf("mq_getattr:mq_maxmsg=%ld mq_msgsize=%ld mq_curmsgs=%ld mq_flags=%ld\n",
                attr.mq_maxmsg, attr.mq_msgsize, attr.mq_curmsgs, attr.mq_flags);

    // ---- 消息边界 + 优先级:三条不同长度、不同 prio ----
    std::printf("\n按 prio 1 → 3 → 2 的顺序发三条不同长度的消息:\n");
    send_msg(mq, 1, 10, 'x', "M1");
    send_msg(mq, 3, 50, 'y', "M2");
    send_msg(mq, 2, 200, 'z', "M3");
    mq_getattr(mq, &attr);
    std::printf("  mq_curmsgs=%ld(排队 3 条)\n", attr.mq_curmsgs);

    std::printf("\n连收三次(mq_receive 带 prio 出参,返回值就是本条字节数):\n");
    for (int i = 0; i < 3; ++i) {
        char buf[512];
        unsigned prio = 0;
        ssize_t n = mq_receive(mq, buf, sizeof buf, &prio);
        if (n < 0) {
            perror("mq_receive");
            return 1;
        }
        char id[4] = {buf[0], buf[1], buf[2], 0};
        char tag = buf[3];
        bool pure = true;
        for (ssize_t j = 3; j < n; ++j)
            if (buf[j] != tag) {
                pure = false;
                break;
            }
        std::printf("  第%d次收到 prio=%u 长度=%zd 字节(id=%s)——长度与发送时一字不差,边界还在%s\n",
                    i + 1, prio, n, id, pure ? ",内容校验通过" : ",内容异常!");
    }

    // ---- 同优先级 FIFO ----
    std::printf("\n同 prio=5 连发两条,验证同优先级内按发送序:\n");
    {
        mq_send(mq, "first-in", 8, 5);
        mq_send(mq, "second-in", 9, 5);
        char buf[512]; // 注意:mq_receive 的缓冲区必须 ≥ 队列的 mq_msgsize,否则 EINVAL
        unsigned prio = 0;
        ssize_t n1 = mq_receive(mq, buf, sizeof buf, &prio);
        std::printf("  先收到(%zd 字节):「%.*s」\n", n1, (int)n1, buf);
        ssize_t n2 = mq_receive(mq, buf, sizeof buf, &prio);
        std::printf("  后收到(%zd 字节):「%.*s」——同优先级内 FIFO;「跳队」只发生在跨优先级之间\n",
                    n2, (int)n2, buf);
    }

    mq_close(mq);
    mq_unlink(kName);
    std::printf("\nmq_unlink 后再看目录:\n");
    show_dir("  [ls -l /dev/mqueue/]\n");
    std::printf(
        "结论:mq 的名字空间是 /dev/mqueue 这个挂载点(mqueue 文件系统),与 shm 的 /dev/shm 同构;\n"
        "mq_send/mq_receive 一次一条、边界保留, prio 高的先出——这两点是它与管道字节流的根本差异\n");
    return 0;
}
