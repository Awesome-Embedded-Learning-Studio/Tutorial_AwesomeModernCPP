#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "=============================="
echo " Pre-commit Hook Setup"
echo "=============================="
echo ""

if ! git -C "$PROJECT_ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    echo -e "${RED}ERROR: not inside a Git repository.${NC}"
    exit 1
fi

if ! command -v pre-commit >/dev/null 2>&1; then
    echo -e "${RED}ERROR: pre-commit is required.${NC}"
    echo ""
    echo "Install it first, for example:"
    echo "  pipx install pre-commit"
    echo "  # or: python3 -m pip install --user pre-commit"
    exit 1
fi

cd "$PROJECT_ROOT"

current_hooks_path="$(git -C "$PROJECT_ROOT" config --get core.hooksPath || true)"
if [ -n "$current_hooks_path" ]; then
    echo -e "${YELLOW}Removing core.hooksPath=$current_hooks_path so .git/hooks/pre-commit is used.${NC}"
    git -C "$PROJECT_ROOT" config --unset core.hooksPath
fi

pre-commit install --config "$PROJECT_ROOT/.pre-commit-config.yaml"

echo ""
echo -e "${GREEN}Installed pre-commit hooks.${NC}"
echo ""

missing_tools=()
for tool in python3 clang-format; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        missing_tools+=("$tool")
    fi
done

if [ "${#missing_tools[@]}" -gt 0 ]; then
    echo -e "${YELLOW}Missing hook tools:${NC}"
    printf '  - %s\n' "${missing_tools[@]}"
    echo ""
    echo "Install them before committing files that need the corresponding checks."
else
    echo -e "${GREEN}Hook tools detected:${NC}"
    echo "  - python3: $(python3 --version 2>&1)"
    echo "  - clang-format: $(clang-format --version 2>&1)"
fi

echo ""
echo "What runs before each commit:"
echo "  - markdownlint/frontmatter/basic file checks from .pre-commit-config.yaml"
echo "  - clang-format on staged C/C++ source and header files"
echo "  - python3 scripts/coverage.py --update"
echo ""
echo "Run manually:"
echo "  pre-commit run --all-files"
echo ""
echo "Bypass only when necessary:"
echo "  git commit --no-verify -m 'message'"
