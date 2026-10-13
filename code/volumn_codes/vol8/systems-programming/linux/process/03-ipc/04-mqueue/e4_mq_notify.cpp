// E4c mq_notify 信号通知:队列从空到非空的那一刻,注册方收到 SIGUSR1
// (siginfo 的 si_code == SI_MESGQ,sigev_value 原样带回);注册是一次性的——
// 消费掉就失效,第二次发消息不再有信号,得重新 mq_notify。与信号篇的 sigaction/SA_SIGINFO 衔接。
// 编译:g++ -std=c++20 -Wall -Wextra -O2 -I ../common e4_mq_notify.cpp -o e4_mq_notify -pthread
#include "ipc_util.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <mqueue.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
const char* kName = "/lp03_notify";
volatile sig_atomic_t g_notify = 0;
int g_si_code = 0;
void* g_si_value = nullptr;

void on_sigusr1(int, siginfo_t* si, void*) {
    g_notify = 1;
    g_si_code = si->si_code;
    g_si_value = si->si_value.sival_ptr;
}
} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);

    mq_unlink(kName);
    struct mq_attr want{};
    want.mq_maxmsg = 10;
    want.mq_msgsize = 64;
    mqd_t mq = mq_open(kName, O_CREAT | O_RDWR | O_EXCL, 0600, &want);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        return 1;
    }

    struct sigaction sa{};
    sa.sa_sigaction = on_sigusr1;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sys_call("sigaction", sigaction, SIGUSR1, &sa, nullptr);

    // 注册:队列空→非空的瞬间,给本进程发 SIGUSR1,sigev_value 带一个私货指针
    struct sigevent sev{};
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGUSR1;
    sev.sigev_value.sival_ptr = (void*)0xC0DE;
    if (mq_notify(mq, &sev) != 0) {
        perror("mq_notify");
        return 1;
    }
    std::printf("mq_notify 已注册(SIGEV_SIGNAL/SIGUSR1,sigev_value=0xC0DE),fork 子进程 500 ms "
                "后发消息……\n");

    pid_t pid = sys_call("fork", fork);
    if (pid == 0) { // 子进程:继承 mqd,两发一隔 300 ms
        usleep(500 * 1000);
        mq_send(mq, "wake-1", 6, 1);
        usleep(300 * 1000);
        mq_send(mq, "wake-2", 6, 1);
        _exit(0);
    }

    // 第一次:信号应该到达
    while (!g_notify)
        usleep(1000);
    std::printf("信号到了:si_code=%d(%s),sigev_value 带回 %p——注册时塞的私货原样返回\n", g_si_code,
                g_si_code == SI_MESGQ ? "SI_MESGQ,消息队列专属来源" : "?", g_si_value);
    struct mq_attr attr{};
    mq_getattr(mq, &attr);
    char buf[512];
    unsigned prio = 0;
    ssize_t n = mq_receive(mq, buf, sizeof buf, &prio);
    std::printf("收到本条:%.*s(mq_curmsgs 消费前=%ld)\n", (int)n, buf, attr.mq_curmsgs);

    // 第二次:注册已消费,再发不会有信号
    std::printf("等子进程发第二条(不重新注册)……\n");
    sleep(1); // 子进程的第二条在此期间落进队列
    mq_getattr(mq, &attr);
    std::printf("第二条到货了吗?mq_curmsgs=%ld;信号又来过吗:%s\n", attr.mq_curmsgs,
                g_notify > 1 ? "是" : "没有——mq_notify 的注册是一次性的,消费即失效");
    n = mq_receive(mq, buf, sizeof buf, &prio);
    std::printf("补收第二条:%.*s;要继续被通知,得再调一次 mq_notify(或在收货线程里重新武装)\n",
                (int)n, buf);

    int st = 0;
    waitpid(pid, &st, 0);
    mq_close(mq);
    mq_unlink(kName);
    return 0;
}
