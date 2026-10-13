#!/bin/sh
# e1_wide_out 的汇编口径:每 mode 独立进程,stdout 落临时文件后 od 成十六进制留证,
# stderr 的返回值报告原样并入。e1_wide_out.out 就是本脚本的输出重定向。
set -e
./e1_wide_out.exe setup
{
echo "## e1_wide_out.out —— 由 run_wide_out.sh 汇编:每 mode 独立进程跑,stdout 落文件后"
echo "## od 十六进制留证(字节是证据),stderr 的返回值报告原样附在后面。"
echo
echo "===== 环境事实(mode=base,程序自己报) ====="
./e1_wide_out.exe base
echo
for m in a b c d e f orient orient2; do
  echo "===== mode=$m ====="
  ./e1_wide_out.exe $m > /tmp/wd_frag 2>/tmp/wd_err
  echo "-- stderr 报告:"
  cat /tmp/wd_err
  echo "-- stdout 字节数: $(wc -c < /tmp/wd_frag)"
  echo "-- stdout 内容(尽力按 UTF-8 显示):"
  cat /tmp/wd_frag
  echo "-- stdout 字节十六进制(od -An -tx1):"
  od -An -tx1 /tmp/wd_frag
  echo
done
}
