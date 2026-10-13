// E4b:带静态状态的函数为什么不能进 handler——strtok / localtime 的机制模拟
// 编译:g++ -std=c++20 -Wall -Wextra -O2 static_state.cpp -o static_state && ./static_state
//
// 这里不做"信号打断 strtok"的真崩溃实验(man 7 signal-safety 黑名单引述:
// strtok、localtime 均不在安全表内,strtok_r 才在)。用单线程下确定的、合法的
// 调用序列模拟"handler 打断主流程"——因为两者的病根相同:函数把进行中的状态
// 存在静态存储里,第二次进入会把第一次的进度覆盖掉。行为是确定的,不是未定义行为。
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

// 模拟"主流程":逐个取 token,strtok 的进度指针存在函数内部静态存储
void main_flow_strtok(const char* tag) {
    char buf[32];
    snprintf(buf, sizeof buf, "alpha beta gamma delta");
    char* tok = strtok(buf, " "); // 第一次调用:静态指针记住 "beta gamma delta"
    std::printf("[%s] main strtok #1 -> %s\n", tag, tok);

    if (tok) {
        // 模拟"信号此刻到达,handler 也调了 strtok"(单线程直接调用,效果等价)
        char hbuf[8];
        snprintf(hbuf, sizeof hbuf, "X Y");
        char* htok = strtok(hbuf, " "); // 静态指针被改写为指向 "Y"
        std::printf("[%s] (handler-style) strtok -> %s   <- 静态进度被覆盖\n", tag, htok);
    }

    // 主流程恢复,继续取——以为拿到 "beta",实际拿到的是 handler 那条串的 "Y"
    tok = strtok(nullptr, " ");
    std::printf("[%s] main strtok #2 (expect beta) -> %s%s\n", tag, tok ? tok : "(null)",
                (tok && strcmp(tok, "beta") == 0) ? "" : "   <- CORRUPTED");
}

// 模拟"handler 打断 localtime":两次调用返回的是同一个静态 struct tm
void main_flow_localtime() {
    std::time_t t1 = 1000000000;   // 2001-09-09 01:46:40 UTC
    std::time_t t2 = 1400000000;   // 2014-05-13 16:53:20 UTC
    struct tm* a = localtime(&t1); // 返回指针指向函数内部的静态 struct tm
    std::printf("[localtime] first  localtime -> %04d-%02d-%02d %02d:%02d:%02d\n",
                a->tm_year + 1900, a->tm_mon + 1, a->tm_mday, a->tm_hour, a->tm_min, a->tm_sec);

    struct tm* b = localtime(&t2); // 第二次调用:同一块静态存储被改写

    std::printf("[localtime] second localtime -> %04d-%02d-%02d %02d:%02d:%02d\n",
                b->tm_year + 1900, b->tm_mon + 1, b->tm_mday, b->tm_hour, b->tm_min, b->tm_sec);
    std::printf("[localtime] re-read pointer a   -> %04d-%02d-%02d %02d:%02d:%02d"
                "   <- a silently became t2\n",
                a->tm_year + 1900, a->tm_mon + 1, a->tm_mday, a->tm_hour, a->tm_min, a->tm_sec);
}

} // namespace

int main() {
    std::printf("== strtok: the in-progress pointer lives in static storage ==\n");
    main_flow_strtok("strtok");

    std::printf("\n== localtime: the result struct itself is static ==\n");
    setlocale(LC_ALL, "C");
    setenv("TZ", "UTC", 1);
    tzset();
    main_flow_localtime();
    return 0;
}
