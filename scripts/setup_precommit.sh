#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ENV_DIR=".venv"

echo "=============================="
echo " Pre-commit Setup "
echo "=============================="
echo ""

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 1. Check python3
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}❌ python3 not found.${NC}"
    echo "   Please install Python 3 first."
    exit 1
fi
echo -e "${GREEN}✅ Python detected: $(python3 --version)${NC}"

echo -e "🔄 Activating environment..."
source $ENV_DIR/bin/activate

# 2. Check pip
if ! command -v pip &> /dev/null && ! python3 -m pip --version &> /dev/null; then
    echo -e "${RED}❌ pip not found.${NC}"
    echo "   Please install pip first."
    exit 1
fi
echo -e "${GREEN}✅ pip available${NC}"

# 3. Install pre-commit
echo ""
echo "📦 Installing pre-commit..."
python3 -m pip install pre-commit || {
    echo -e "${YELLOW}⚠ User install failed, trying system install...${NC}"
    python3 -m pip install pre-commit
}

# Verify pre-commit installation
if command -v pre-commit &> /dev/null; then
    echo -e "${GREEN}✅ pre-commit installed: $(pre-commit --version)${NC}"
else
    # Add user base to PATH if not already there
    USER_BASE=$(python3 -m site --user-base)
    export PATH="$USER_BASE/bin:$PATH"

    if command -v pre-commit &> /dev/null; then
        echo -e "${GREEN}✅ pre-commit installed: $(pre-commit --version)${NC}"
    else
        echo -e "${RED}❌ pre-commit installation failed${NC}"
        exit 1
    fi
fi

# 4. Install pyyaml (for frontmatter validation)
echo ""
echo "📦 Installing pyyaml (for frontmatter validation)..."
pip install pyyaml

# 5. Check for node and npm (for markdownlint)
echo ""
if command -v node &> /dev/null && command -v npm &> /dev/null; then
    echo -e "${GREEN}✅ Node.js detected: $(node --version)${NC}"
    echo "📦 Installing markdownlint-cli..."
    sudo npm install -g markdownlint-cli

    if command -v markdownlint &> /dev/null; then
        echo -e "${GREEN}✅ markdownlint installed${NC}"
    else
        echo -e "${YELLOW}⚠ markdownlint installation failed, pre-commit will skip it${NC}"
    fi
else
    echo -e "${YELLOW}⚠ Node.js/npm not found, skipping markdownlint installation${NC}"
    echo "   To install markdownlint manually, visit: https://github.com/igorshubovych/markdownlint-cli"
fi

# 6. Install git hooks
echo ""
echo "🔧 Installing git hooks..."
cd "$PROJECT_ROOT"

if pre-commit install; then
    echo -e "${GREEN}✅ Git hooks installed successfully${NC}"
else
    echo -e "${YELLOW}⚠ Pre-commit install failed, trying with --allow-missing-config${NC}"
    pre-commit install --allow-missing-config || {
        echo -e "${RED}❌ Failed to install git hooks${NC}"
        echo "   You may need to install pre-commit manually:"
        echo "   pip install pre-commit"
        echo "   pre-commit install"
        exit 1
    }
fi

# 7. Summary
echo ""
echo "=============================="
echo -e "${GREEN}🎉 Setup completed!${NC}"
echo "=============================="
echo ""
echo "Installed tools:"
echo "  - pre-commit: $(pre-commit --version 2>/dev/null || echo 'not available')"
echo "  - pyyaml: $(python3 -c 'import yaml; print(yaml.__version__)' 2>/dev/null || echo 'not available')"
if command -v markdownlint &> /dev/null; then
    echo "  - markdownlint: $(markdownlint --version 2>/dev/null || echo 'available')"
fi
echo ""
echo "Usage:"
echo "  # Run all checks manually"
echo "  pre-commit run --all-files"
echo ""
echo "  # Run checks on staged files"
echo "  pre-commit run"
echo ""
echo "  # Skip hooks (not recommended)"
echo "  git commit --no-verify -m 'message'"
echo ""
echo "Configuration: .pre-commit-config.yaml"
