# 05-nested-breakaway:E5 Job 的嵌套与出走

文件:`e5_nested.cpp` / `e5_nested.out`(模式:`breakout <jobname>`(在 Job 里生孙并验孙籍)、`trybreak <jobname>`(带 CREATE_BREAKAWAY_FROM_JOB 生孙)、`alloc`/`quick`;无参=驱动)。

## 五节结论

**[0] 背景注记**:驱动开场 `IsProcessInJob(self, NULL)=1`——WSL interop 拉起的 Windows 进程本身就在一个 Job 里。本批 Job 实验不受影响(Win8+ 多 Job 成员合法),但解读"在不在 Job 里"时要有这层意识。

**[1] 世袭**:孩子在 Job 里、不带任何旗生孙 → `IsProcessInJob(孙)=1`。Job 的成员资格顺着进程树继承——Chrome 当年被 launcher 的 Job 套住、自己想开 Job 管孩子就撞在这里(Exit code 10)。

**[2] 允许位出走**:Job 挂 `JOB_OBJECT_LIMIT_BREAKAWAY_OK`,创建方带 `CREATE_BREAKAWAY_FROM_JOB` → 创建成功、`IsProcessInJob(孙)=0`——双全则走(Exit code 22)。

**[3] 强闯**:Job 不给允许位,创建硬带旗 → **CreateProcessW 直接失败,`err=5`(ERROR_ACCESS_DENIED)**(Exit code 20)。允许位 vs 强闯,一翻两瞪眼。

**[4] 静默出走**:Job 挂 `JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK`,创建方**不带旗** → 孙照样 `=0` 走掉(Exit code 11)。launcher 想"我管的孩子、孩子的孩子不管"就用它。

**[5] 嵌套:谁能限住谁**
- (a) 想当然姿势 `AssignProcessToJobObject(parent_job, child_job句柄)` → **`ret=0, err=6`**:hProcess 只认进程句柄,嵌套不是特殊参数(实测证伪)
- (b) 正路(MSDN nested-jobs 页):同一个进程**先 Assign 给根 Job,再 Assign 给子 Job**(顺序即层级):两次都 ret=1,`IsProcessInJob(父)=1, (子)=1`
- 判决:子 Job 限额 256MB、父 48MB,孩子 8MB 步进 → **40MB 处 err=1455 失败**——commit 类限额取全链最紧值,**子 Job 只能再收紧,永远放宽不了父的帽子**

## 复现

```sh
cd 05-nested-breakaway
/mnt/c/msys64/ucrt64/bin/g++.exe -std=c++20 -Wall -Wextra -municode e5_nested.cpp -o e5_nested.exe
chmod +x e5_nested.exe && ./e5_nested.exe > e5_nested.out 2>&1
```

注意:命名 Job(`Local\vol8e5-*`)带 pid,可反复跑;分配失败点 40MB 随进程脚印浮动几 MB。
