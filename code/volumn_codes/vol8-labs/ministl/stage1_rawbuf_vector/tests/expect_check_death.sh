#!/bin/sh
# 用法: expect_check_death.sh <可执行文件>
#
# 裁判脚本:期望被测程序以 "[TAMCPP Check Crash]" 报告 + 非零退出收场。
# ctest 对信号打死的子进程一律判负、不看 PASS_REGULAR_EXPRESSION,
# 所以"期望崩溃"的用例交给这层壳来翻译成干净的退出码。
# 两项都验:报告在场、且真的死了 —— 只打印不死的假检查骗不过它。

out=$("$1" 2>&1)
status=$?

case "$out" in
*"TAMCPP Check Crash"*) ;;
*)
    echo "FAIL: no crash report in output"
    exit 1
    ;;
esac

if [ "$status" -eq 0 ]; then
    echo "FAIL: printed the report but did not die (exit=0)"
    exit 1
fi

echo "OK: died with Check Crash as specified (exit=$status)"
exit 0
