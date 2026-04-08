#!/usr/bin/env bash
##
## @file mkdocs_dev.sh
## @brief MkDocs documentation development environment manager
##
## Usage:
##   ./scripts/mkdocs_dev.sh <command> [OPTIONS]
##
## Commands:
##   serve    Start MkDocs dev server (default)
##   build    Build static site to site/
##   install  Create/update virtual environment and install dependencies
##   clean    Clean build artifacts (site/, __pycache__, .cache)
##   reset    Delete and recreate .venv
##   help     Show this help message
##
## Options:
##   -p, --port PORT    Dev server port (default: 8000)
##   -b, --bind ADDR    Dev server bind address (default: 127.0.0.1)
##   -h, --help         Show this help message
##

set -eo pipefail

# === Constants ===
readonly VENV_DIR=".venv"
readonly DEPS_MARKER=".deps_installed"
readonly SITE_DIR="site"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# === Colors ===
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info()  { echo -e "${CYAN}[INFO]${NC} $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

# === Global variables ===
DEV_PORT=8000
DEV_ADDR="127.0.0.1"

# =============================================================================
# Python environment
# =============================================================================

create_venv() {
    local venv_path="$PROJECT_ROOT/$VENV_DIR"

    if [[ -d "$venv_path" ]]; then
        return 0
    fi

    log_info "Creating Python virtual environment: $venv_path"
    python3 -m venv "$venv_path"

    if [[ ! -d "$venv_path" ]]; then
        log_error "Failed to create virtual environment"
        exit 1
    fi

    log_ok "Virtual environment created"
}

activate_venv() {
    local activate_script="$PROJECT_ROOT/$VENV_DIR/bin/activate"

    if [[ ! -f "$activate_script" ]]; then
        log_error "Virtual environment not found. Run: $0 install"
        exit 1
    fi

    source "$activate_script"
}

install_deps() {
    local marker="$PROJECT_ROOT/$VENV_DIR/$DEPS_MARKER"
    local pyproject="$SCRIPT_DIR/pyproject.toml"

    if [[ -f "$marker" ]]; then
        local marker_hash pyproject_hash
        marker_hash=$(cat "$marker" 2>/dev/null)
        pyproject_hash=$(md5sum "$pyproject" 2>/dev/null | cut -d' ' -f1)

        if [[ "$marker_hash" == "$pyproject_hash" ]]; then
            log_info "Dependencies up-to-date, skipping install"
            return 0
        fi
    fi

    log_info "Installing documentation dependencies..."
    pip install --upgrade pip --quiet
    pip install -e "$SCRIPT_DIR" --quiet

    md5sum "$pyproject" 2>/dev/null | cut -d' ' -f1 > "$marker"
    log_ok "Dependencies installed"
}

ensure_venv() {
    if ! command -v python3 &>/dev/null; then
        log_error "Python 3 not found. Please install Python >= 3.10"
        exit 1
    fi

    create_venv
    activate_venv
    install_deps
}

# =============================================================================
# Commands
# =============================================================================

cmd_serve() {
    ensure_venv

    echo ""
    log_info "=== MkDocs Dev Server ==="
    log_info "Address: http://${DEV_ADDR}:${DEV_PORT}"
    echo ""

    cd "$PROJECT_ROOT"
    mkdocs serve --dev-addr="${DEV_ADDR}:${DEV_PORT}"
}

cmd_build() {
    ensure_venv

    local output_path="$PROJECT_ROOT/$SITE_DIR"

    echo ""
    log_info "=== MkDocs Build ==="
    log_info "Output: $output_path"
    echo ""

    cd "$PROJECT_ROOT"
    mkdocs build --clean

    log_ok "Build complete: $output_path"
}

cmd_install() {
    ensure_venv
    log_ok "Environment ready"
}

cmd_clean() {
    echo ""
    log_info "=== Cleaning build artifacts ==="

    local cleaned=0

    if [[ -d "$PROJECT_ROOT/$SITE_DIR" ]]; then
        rm -rf "$PROJECT_ROOT/$SITE_DIR"
        log_info "Cleaned: $SITE_DIR"
        cleaned=$((cleaned + 1))
    fi

    if [[ -d "$PROJECT_ROOT/.cache" ]]; then
        rm -rf "$PROJECT_ROOT/.cache"
        log_info "Cleaned: .cache"
        cleaned=$((cleaned + 1))
    fi

    local pycache_count
    pycache_count=$(find "$PROJECT_ROOT" -type d -name "__pycache__" 2>/dev/null | wc -l)
    if [[ "$pycache_count" -gt 0 ]]; then
        find "$PROJECT_ROOT" -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
        log_info "Cleaned: __pycache__ (${pycache_count} dirs)"
        cleaned=$((cleaned + 1))
    fi

    if [[ "$cleaned" -eq 0 ]]; then
        log_info "Nothing to clean"
    else
        log_ok "Cleaned ${cleaned} items"
    fi
}

cmd_reset() {
    local venv_path="$PROJECT_ROOT/$VENV_DIR"

    echo ""
    log_info "=== Resetting virtual environment ==="

    if [[ -d "$venv_path" ]]; then
        log_info "Removing: $venv_path"
        rm -rf "$venv_path"
        log_ok "Old environment removed"
    fi

    ensure_venv
    log_ok "Virtual environment rebuilt"
}

# =============================================================================
# Help & Argument parsing
# =============================================================================

show_help() {
    grep '^##' "$0" | sed 's/^## \?//g' | sed '/^$/d'
    exit 0
}

parse_args() {
    local command=""

    while [[ $# -gt 0 ]]; do
        case "$1" in
            serve|build|install|clean|reset|help)
                command="$1"
                shift
                ;;
            -p|--port)
                DEV_PORT="$2"
                shift 2
                ;;
            -b|--bind)
                DEV_ADDR="$2"
                shift 2
                ;;
            -h|--help)
                show_help
                ;;
            *)
                log_error "Unknown argument: $1"
                echo ""
                show_help
                ;;
        esac
    done

    command="${command:-serve}"

    case "$command" in
        serve)    cmd_serve ;;
        build)    cmd_build ;;
        install)  cmd_install ;;
        clean)    cmd_clean ;;
        reset)    cmd_reset ;;
        help)     show_help ;;
    esac
}

# =============================================================================
# Entry point
# =============================================================================

parse_args "$@"
