# 03-process-group —— E3 进程组语义与免疫机制

三个进程:父 P、子 A(同组,忽略位复位)、子 B(`CREATE_NEW_PROCESS_GROUP`,pgid=B 自己,保留新组自带的忽略位)。全部 handler 返回 TRUE,共享控制台,跨进程命名互斥体串行化打印(见源码 PF 宏)。六步时序(`e3_group.out`):

| 步 | 动作 | 实际响应 | 说明 |
|---|---|---|---|
| 1 | P 发 CTRL_C(0) 广播 | P、A,B **静默** | 新组免疫,同组照收 |
| 2 | P 发 CTRL_BREAK(0) 广播 | P、A、B 全响 | BREAK 不受忽略位限制 |
| 3 | 信号 B 自解免疫(NULL+FALSE) | B 报「免疫解除」 | 文档给的解除法 |
| 4 | P 发 CTRL_C(0,**定向 B 组**) | **B 收到了**(type=0) | **与 MSDN remark 矛盾,复跑一致** |
| 5 | P 发 CTRL_BREAK(1,定向 B 组) | 只有 B | 定向不溅到 P/A |
| 6 | P 再发 CTRL_C(0) 广播 | P、A、B 全响 | B 解除后与普通进程无异 |
| 尾 | exit 事件收场 | A=0,B=0 | 免疫是「不投递」,不是「杀掉」 |

三个可写进文章的判定:

1. **免疫的机制不是一个独立开关,就是忽略位**:CREATE_NEW_PROCESS_GROUP 文档口径「CTRL+C signals will be disabled for all processes within the new process group」,等效隐式 `SetConsoleCtrlHandler(NULL, TRUE)`(同一位、同被继承、同可被 NULL+FALSE 解除)。E3 步骤 3/6 与 00-env-probe 的 interop 现象用同一个机制解释——这是本篇实验最值钱的一条归因。
2. **CTRL_BREAK 是唯一能定向发组的信号,也确实只发该组**(步骤 5,P/A 一行没有)。后台服务/守护化叙事:新组子进程对控制台广播 Ctrl+C 免疫,收不收 BREAK 由父进程定向决定。
3. **文档矛盾要如实记录**:GenerateConsoleCtrlEvent Remarks 说非零 pgid 的 CTRL_C「succeeds but not be received」,本机 Win11 26200 实测(免疫解除后)能收到,两轮一致。文章引用该 remark 时标注实测反例,行为以实测为准,可移植代码别依赖「定向 CTRL_C 收不到」这条。

## 复现

```sh
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra e3_group.cpp -o e3_group.exe
chmod +x e3_group.exe && ./e3_group.exe
```
