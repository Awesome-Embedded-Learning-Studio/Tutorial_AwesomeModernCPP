#!/usr/bin/env bash
set -e
ENV_DIR=".venv"

echo "======================"
echo " MkDocs Local Preview "
echo "======================"

if [ ! -d "$ENV_DIR" ]; then
    echo "❌ Local Python environment not found."
    echo "Run:"
    echo "   bash scripts/mkdoc_local_dependency.sh"
    exit 1
fi

echo "🔄 Activating environment..."
source $ENV_DIR/bin/activate

echo "🚀 Starting local server..."
mkdocs serve
