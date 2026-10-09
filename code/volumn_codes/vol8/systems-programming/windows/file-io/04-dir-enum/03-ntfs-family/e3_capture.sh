#!/bin/sh
# e3_capture.sh —— e3 的 cmd/fsutil 侧证据捕获(WSL 里跑,Windows 工具的 GBK 输出过 iconv)
# 产出:
#   e3_mklink_capture.txt —— mklink 四条:文件符号链接拒 / 目录符号链接拒 / junction 成 / 硬链接成
#   e3_dir_family.txt     —— dir /a 的类型列(<JUNCTION> 与跳转目标)+ fsutil hardlink list
#                            + fsutil sparse queryflag(免管理员,实测可用)
# 口径:先 cd 进 %TEMP% 下的 fixture 目录(WSL 路径形态),cmd.exe 就不会再吐
#      "UNC cwd 不支持"横幅;残留的中文串按 GBK 转 UTF-8。
set -e
OUT=$(cd "$(dirname "$0")" && pwd) # 输出目录先定格:后面要 cd 去 C: 侧,dirname $0 会跟着漂
CMD=/mnt/c/Windows/System32/cmd.exe
FSUTIL=/mnt/c/Windows/System32/fsutil.exe
CAP='C:\Users\CharlieChen114514\AppData\Local\Temp\sysprog-direnum\e3cap'
W=/mnt/c/Users/CharlieChen114514/AppData/Local/Temp/sysprog-direnum/e3cap

# 干净的 fixture
rm -rf "$W"
mkdir -p "$W/real_dir"
printf 'hardlink-demo' > "$W/real.txt"

cd "$W" # cmd 的 cwd 落在 C:,横幅消失

{
  echo "## mklink 四条(cmd 原话,控制台代码页 936 的输出经 iconv GBK->UTF-8)"
  echo
  echo 'C:\> mklink sl_file.txt real.txt        (文件符号链接)'
  $CMD /c mklink sl_file.txt real.txt 2>&1 | iconv -f GBK -t UTF-8
  echo
  echo 'C:\> mklink /D sl_dir real_dir          (目录符号链接)'
  $CMD /c mklink /D sl_dir real_dir 2>&1 | iconv -f GBK -t UTF-8
  echo
  echo 'C:\> mklink /J jn_dir real_dir          (junction,免特权)'
  $CMD /c mklink /J jn_dir real_dir 2>&1 | iconv -f GBK -t UTF-8
  echo
  echo 'C:\> mklink /H hard.txt real.txt        (硬链接,免特权)'
  $CMD /c mklink /H hard.txt real.txt 2>&1 | iconv -f GBK -t UTF-8
  echo
  echo "解读:/D 的拒绝就是 CreateSymbolicLinkW 1314 的 cmd 版——没开发者模式、"
  echo "     没管理员,符号链接造不了;/J 与 /H 不要特权,这是 junction/硬链接"
  echo "     能当'平民链接'用的原因。"
} > "$OUT/e3_mklink_capture.txt"

{
  echo "## dir /a:类型列直接标 <JUNCTION> 并附跳转目标(输出按 GBK 转 UTF-8)"
  echo
  $CMD /c dir /a 2>&1 | iconv -f GBK -t UTF-8
  echo
  echo "## fsutil hardlink list:一条 MFT 记录的多个名字(免管理员)"
  echo
  $FSUTIL hardlink list real.txt 2>&1 | iconv -f GBK -t UTF-8
  echo
  echo "## fsutil sparse queryflag(免管理员;setflag 同样免,但 C++ 侧走 FSCTL_SET_SPARSE)"
  echo
  $FSUTIL sparse queryflag real.txt 2>&1 | iconv -f GBK -t UTF-8
} > "$OUT/e3_dir_family.txt"

# 清理(hard.txt 与 real.txt 是同一文件的两个名字,各删一次;junction 只删链接)
cd /tmp
rm -f "$W/hard.txt" "$W/real.txt" "$W/sl_file.txt" 2>/dev/null || true
rmdir "$W/jn_dir" "$W/real_dir" 2>/dev/null || true
rm -rf "$W"
echo "captured: e3_mklink_capture.txt e3_dir_family.txt"
