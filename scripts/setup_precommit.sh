#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
HOOKS_DIR="$PROJECT_ROOT/.githooks"
PRE_COMMIT_HOOK="$HOOKS_DIR/pre-commit"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "=============================="
echo " Git Hook Setup"
echo "=============================="
echo ""

if ! git -C "$PROJECT_ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    echo -e "${RED}ERROR: not inside a Git repository.${NC}"
    exit 1
fi

if [ ! -f "$PRE_COMMIT_HOOK" ]; then
    echo -e "${RED}ERROR: missing $PRE_COMMIT_HOOK${NC}"
    exit 1
fi

chmod +x "$PRE_COMMIT_HOOK"
git -C "$PROJECT_ROOT" config core.hooksPath .githooks

echo -e "${GREEN}Installed native Git hooks via core.hooksPath=.githooks${NC}"
echo ""

missing_tools=()
for tool in python3 clang-format; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        missing_tools+=("$tool")
    fi
done

if [ "${#missing_tools[@]}" -gt 0 ]; then
    echo -e "${YELLOW}Missing optional/required hook tools:${NC}"
    printf '  - %s\n' "${missing_tools[@]}"
    echo ""
    echo "Install them before committing files that need the corresponding checks."
else
    echo -e "${GREEN}Required hook tools detected:${NC}"
    echo "  - python3: $(python3 --version 2>&1)"
    echo "  - clang-format: $(clang-format --version 2>&1)"
fi

echo ""
echo "What runs before each commit:"
echo "  - clang-format on staged C/C++ source and header files"
echo "  - python3 scripts/coverage.py --update"
echo "  - git add for files updated by the hook, so one git commit is enough"
echo ""
echo "Run manually:"
echo "  .githooks/pre-commit"
echo ""
echo "Bypass only when necessary:"
echo "  git commit --no-verify -m 'message'"
