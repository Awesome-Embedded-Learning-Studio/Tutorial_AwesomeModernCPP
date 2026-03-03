#!/usr/bin/env bash

set -e
$ENV_DIR=".venv"

echo "==============================="
echo " MkDocs Local Dependency Check "
echo "==============================="

# 1️⃣ Check python3
if ! command -v python3 &> /dev/null; then
    echo "❌ python3 not found."
    echo "   Please install Python 3 first."
    exit 1
fi

echo "✅ Python detected: $(python3 --version)"

# 2️⃣ Create venv if not exists
if [ ! -d "$ENV_DIR" ]; then
    echo "📦 Creating isolated Python environment..."
    python3 -m venv $ENV_DIR
else
    echo "✅ Local environment already exists."
fi

# 3️⃣ Activate environment
echo "🔄 Activating environment..."
source $ENV_DIR/bin/activate

# 4️⃣ Upgrade pip safely
echo "⬆️ Upgrading pip..."
pip install --upgrade pip

# 5️⃣ Install dependencies
echo "Installing default mkdocs stack..."
pip install "mkdocs<2.0" \
        mkdocs-material \
        mkdocs-awesome-pages-plugin \
        mkdocs-git-revision-date-localized-plugin

# 6️⃣ Verify mkdocs
if command -v mkdocs &> /dev/null; then
    echo "✅ MkDocs installed successfully."
    echo "   Version: $(mkdocs --version)"
else
    echo "❌ MkDocs installation failed."
    exit 1
fi

echo "🎉 Dependency setup completed."
