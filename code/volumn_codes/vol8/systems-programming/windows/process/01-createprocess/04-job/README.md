# 04-job:E4 Job 对象(KILL_ON_JOB_CLOSE / 账本 / 内存限额)

文件:`e4_job.cpp` / `e4_job.out`(模式:`loop <ms>` 心跳、`quick <ms> <code>`、`alloc <total_mb> <step_mb>`、`mid kill|nokill <pidfile> <hbfile>`;无参=驱动)。统一姿势:孩子挂起出生→AssignProcessToJobObject→ResumeThread,杜绝"抢跑进不了 Job"竞态。

## 五节结论

**[1] KILL_ON_JOB_CLOSE:显式关 Job 句柄**
孩子心跳 6 拍后 `CloseHandle(job)`(当时唯一句柄):`wait=0x0 after 0ms`,退出码 0(Job 处决的默认码),心跳冻在 6 行——**句柄一关,全 Job 陪葬**,延迟在 GetTickCount64 分辨率内。

**[2] 父进程退出=句柄自动关**
三代结构:驱动→mid→孙(mid 建 Job、拉孙、写 pidfile 后退场;驱动趁 mid 活着先把孙句柄攥在手里——pid 回收后再 OpenProcess 只会 err=87,这是本实验踩过的坑)。
- (a) mid 挂 KILL_ON_JOB_CLOSE:mid 一退,孙 `wait=0x0 in 0ms`——**陪葬**
- (b) 对照没旗:1.5s 后孙还在(wait TIMEOUT),心跳继续涨——孤儿照活,Linux 侧默认就是这样,治理要 subreaper;Windows 的答案就是这面旗

**[3] 反例:Job 句柄被孩子继承→陪葬失灵**
把 Job 句柄复制成可继承、孩子连它一起继承:驱动 CloseHandle 后 1.2s 孩子仍活(它自己手里还有一只 Job 句柄)。开 KILL_ON_JOB_CLOSE 时**别让 Job 句柄漏给孩子**。

**[4] QueryInformationJobObject 账本**
空 Job 全 0;两个孩子都活着时 `JobObjectBasicProcessIdList` 报 2 个 id(驱动自身不在列);都退场后 ActiveProcesses=0、TotalProcesses=2、TotalPageFaults=2783、TotalTerminated=0(自然退出不算 terminated),清单归 0。

**[5] JOB_OBJECT_LIMIT_PROCESS_MEMORY=40MB**
限额设进 Job、读回确认;孩子 4MB 步进 VirtualAlloc:36MB 后 `err=1455`(ERROR_COMMITMENT_LIMIT,"页面文件太小");接着 `new unsigned char[64MB]` 抛 `std::bad_alloc`,孩子以自选 42 退场。对照组无 Job:256MB 全过、64MB new 成功——失败确因限额,不因机器。

## 复现

```sh
cd 04-job
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -municode e4_job.cpp -o e4_job.exe
chmod +x e4_job.exe && ./e4_job.exe > e4_job.out 2>&1
```

注意:心跳行数/页错误数随负载浮动;失败点 36MB 随进程脚印浮动几 MB,判据是"≤限额"不是精确值;全程自清理,临时文件在 `%TEMP%\vol8_e4\`。
