#!/usr/bin/env bash
# PreToolUse hook (matcher: Bash)
# 目的:拦截裸 python / python3 调用,强制使用 .venv/bin/python。
# 原因:系统 python (/usr/sbin/python) 缺少 PyYAML 等依赖,会让
#       scripts/validate_frontmatter.py 等脚本把全仓文件误报成 error。
# 策略:任何错误一律 fail-open (exit 0),避免有 bug 的 hook 卡住正常工作。

input=$(cat)

# 从 stdin JSON 中取 .tool_input.command;jq 出错就放行。
if ! cmd=$(printf '%s' "$input" | jq -r '.tool_input.command // empty' 2>/dev/null); then
  exit 0
fi
[ -z "$cmd" ] && exit 0

# 显式放行:命令里用了项目 venv 的 python。
if printf '%s' "$cmd" | grep -q '\.venv/bin/python'; then
  exit 0
fi

# 拦截:python/python3 作为"命令"被调用(段首,或紧跟在 shell 操作符 ;|&( 或
# 包装器 sudo/exec/env/nohup/command/xargs/nice/time 之后)。
# 不匹配 `grep python`、`echo python` 这类把 python 当参数的情况。
if printf '%s' "$cmd" | grep -qE '(^|[;|&(])[[:space:]]*((sudo|exec|env|nohup|command|xargs|nice|time)[[:space:]]+)?python[0-9.]*([[:space:]]|$)'; then
  cat >&2 <<'EOF'
⛔ [block-bare-python] 检测到裸 python/python3 调用,已拦截。
本项目必须使用 .venv/bin/python —— 系统 python (/usr/sbin/python) 缺少 PyYAML 等依赖,
会让 scripts/validate_frontmatter.py 等脚本把全仓文件误报成 error。
请改写为:.venv/bin/python <你的参数>
EOF
  exit 2
fi

exit 0
