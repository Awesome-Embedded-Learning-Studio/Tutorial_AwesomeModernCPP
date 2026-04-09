#!/usr/bin/env bash
set -euo pipefail

# ── 路径推断 ─────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
TUTORIALS_BASE="${REPO_ROOT}/codes_and_assets/stm32f1_tutorials"

# ── 颜色定义 ─────────────────────────────────────────────────────────────────
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly CYAN='\033[0;36m'
readonly BOLD='\033[1m'
readonly NC='\033[0m'

# ── 辅助函数 ─────────────────────────────────────────────────────────────────
msg_info()    { printf "${CYAN}[INFO]${NC}  %s\n" "$*"; }
msg_success() { printf "${GREEN}[OK]${NC}    %s\n" "$*"; }
msg_warn()    { printf "${YELLOW}[WARN]${NC}  %s\n" "$*" >&2; }
msg_error()   { printf "${RED}[ERROR]${NC} %s\n" "$*" >&2; }
die()         { msg_error "$@"; exit 1; }

# ── 验证目录名格式: N_description ──────────────────────────────────────────
validate_name() {
    local name="$1"
    if [[ ! "$name" =~ ^[1-9][0-9]*_[a-z][a-z0-9_]*$ ]]; then
        die "Invalid name '${name}'. Expected format: N_description (e.g., 2_uart, 10_timer_interrupt). \
Only lowercase letters, digits, underscores after the prefix."
    fi
}

# ── 目录名 → CMake 项目名 (去掉前缀编号) ────────────────────────────────────
dir_name_to_cmake_name() {
    local name="$1"
    echo "${name#*_}"
}

# ── 自动检测下一个可用编号 ──────────────────────────────────────────────────
get_next_number() {
    local max=0
    for dir in "$TUTORIALS_BASE"/*; do
        [[ -d "$dir" ]] || continue
        local base
        base="$(basename "$dir")"
        if [[ "$base" =~ ^([0-9]+)_ ]]; then
            local num="${BASH_REMATCH[1]}"
            (( num > max )) && max=$num
        fi
    done
    echo $(( max + 1 ))
}

# ── 根据编号查找教程目录名 ──────────────────────────────────────────────────
find_tutorial_by_number() {
    local target_num="$1"
    for dir in "$TUTORIALS_BASE"/*; do
        [[ -d "$dir" ]] || continue
        local base
        base="$(basename "$dir")"
        if [[ "$base" =~ ^([0-9]+)_(.+)$ ]]; then
            local num="${BASH_REMATCH[1]}"
            if [[ "$num" -eq "$target_num" ]]; then
                echo "$base"
                return 0
            fi
        fi
    done
    return 1
}

# ── 查找前一个教程目录（编号为 N-1 的） ─────────────────────────────────────
find_previous_tutorial() {
    local target_num="$1"
    local prev_num=$(( target_num - 1 ))
    if [[ "$prev_num" -lt 0 ]]; then
        return 1
    fi
    find_tutorial_by_number "$prev_num"
}

# ── 从目录名提取 CMake 项目名 ────────────────────────────────────────────────
extract_cmake_name_from_dir() {
    local dir_name="$1"
    # 从 CMakeLists.txt 中提取 project(...) 名
    local cmake_file="${TUTORIALS_BASE}/${dir_name}/CMakeLists.txt"
    if [[ -f "$cmake_file" ]]; then
        grep -oP 'project\(\K[^ )]+' "$cmake_file" 2>/dev/null | head -1
    fi
}

# ── 列出现有教程 ─────────────────────────────────────────────────────────────
list_tutorials() {
    printf "\n${BOLD}Existing tutorials:${NC}\n"
    for dir in "$TUTORIALS_BASE"/*; do
        [[ -d "$dir" ]] || continue
        local base
        base="$(basename "$dir")"
        local cmake_name="(template)"
        if [[ "$base" =~ ^[0-9]+_(.+)$ ]]; then
            cmake_name="${BASH_REMATCH[1]}"
        fi
        printf "  %-25s  %s\n" "$base" "$cmake_name"
    done
    printf "\n"
}

# ── 使用帮助 ─────────────────────────────────────────────────────────────────
show_help() {
    cat <<'EOF'
Usage: create_tutorial.sh [OPTIONS]

Create a new STM32F1 tutorial project by copying from the previous tutorial (N-1).

Options:
  -n, --name NAME     Create tutorial with specified name (non-interactive)
  -l, --list          List existing tutorial directories
      --dry-run       Show what would be done without making changes
  -h, --help          Show this help message

Naming convention:
  Directory:  N_description   (e.g., 2_uart, 10_timer_interrupt)
  CMake name: description     (auto-derived from directory name)

Examples:
  # Interactive mode - auto-detect next number
  ./create_tutorial.sh

  # Non-interactive: create 2_uart
  ./create_tutorial.sh --name 2_uart

  # List existing tutorials
  ./create_tutorial.sh --list

  # Dry run
  ./create_tutorial.sh --dry-run --name 2_uart
EOF
}

# ── 参数解析 ─────────────────────────────────────────────────────────────────
DIR_NAME=""
DRY_RUN=false
ACTION=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -n|--name)
            [[ $# -lt 2 ]] && die "--name requires an argument"
            DIR_NAME="$2"
            shift 2
            ;;
        -l|--list)
            ACTION="list"
            shift
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            die "Unknown option: $1. Use --help for usage."
            ;;
    esac
done

# ── 列出教程 ─────────────────────────────────────────────────────────────────
if [[ "$ACTION" == "list" ]]; then
    list_tutorials
    exit 0
fi

# ── 预检查 ───────────────────────────────────────────────────────────────────
[[ -d "$TUTORIALS_BASE" ]] || die "Tutorials base not found: ${TUTORIALS_BASE}"

# ── 交互模式：自动推断名称 ──────────────────────────────────────────────────
if [[ -z "$DIR_NAME" ]]; then
    list_tutorials

    next_num="$(get_next_number)"
    printf "${BOLD}Next available number:${NC} ${CYAN}${next_num}${NC}\n\n"

    read -rp "Enter tutorial description (e.g., uart, timer_interrupt): " desc
    [[ -z "$desc" ]] && die "Description cannot be empty."

    # 清理输入：转小写、空格换下划线、去掉非法字符
    desc="$(echo "$desc" | tr '[:upper:]' '[:lower:]' | tr ' ' '_' | tr -cd 'a-z0-9_')"

    DIR_NAME="${next_num}_${desc}"

    cmake_name="$(dir_name_to_cmake_name "$DIR_NAME")"
    printf "\n  Directory:  ${BOLD}${DIR_NAME}${NC}"
    printf "\n  CMake name: ${BOLD}${cmake_name}${NC}\n\n"

    read -rp "Proceed? [Y/n] " confirm
    [[ "${confirm,,}" == "n" ]] && { msg_info "Aborted."; exit 0; }
fi

# ── 验证名称 ─────────────────────────────────────────────────────────────────
validate_name "$DIR_NAME"

TARGET_DIR="${TUTORIALS_BASE}/${DIR_NAME}"
CMAKE_NAME="$(dir_name_to_cmake_name "$DIR_NAME")"

# ── 查找源教程（前一个编号） ─────────────────────────────────────────────────
NEW_NUM="${DIR_NAME%%_*}"
SOURCE_DIR_NAME="$(find_previous_tutorial "$NEW_NUM")" \
    || die "No previous tutorial found for number ${NEW_NUM}. Cannot determine source."
SOURCE_DIR="${TUTORIALS_BASE}/${SOURCE_DIR_NAME}"
SOURCE_CMAKE_NAME="$(extract_cmake_name_from_dir "$SOURCE_DIR_NAME")"
[[ -n "$SOURCE_CMAKE_NAME" ]] || SOURCE_CMAKE_NAME="$(dir_name_to_cmake_name "$SOURCE_DIR_NAME")"

msg_info "Source tutorial: ${SOURCE_DIR_NAME} (CMake project: ${SOURCE_CMAKE_NAME})"

# ── 检查目标是否已存在 ───────────────────────────────────────────────────────
if [[ -d "$TARGET_DIR" ]]; then
    die "Directory already exists: ${TARGET_DIR}\nRemove it first or choose a different name."
fi

# ── Dry-run 模式 ─────────────────────────────────────────────────────────────
if [[ "$DRY_RUN" == true ]]; then
    printf "\n${BOLD}[DRY RUN] Would perform:${NC}\n"
    printf "  Create:  %s\n" "$TARGET_DIR"
    printf "  Copy:    %s/ (excluding build/ .cache/)\n" "$SOURCE_DIR"
    printf "  Replace: '%s' -> '%s' in CMakeLists.txt and .vscode/launch.json\n" "$SOURCE_CMAKE_NAME" "$CMAKE_NAME"
    printf "\n"
    exit 0
fi

# ── 清理 trap：中断时删除不完整目录 ──────────────────────────────────────────
COPY_STARTED=false
cleanup() {
    if $COPY_STARTED && [[ -d "$TARGET_DIR" ]]; then
        msg_warn "Interrupted. Removing incomplete directory: ${TARGET_DIR}"
        rm -rf "$TARGET_DIR"
    fi
}
trap cleanup EXIT

# ── 复制模板 ─────────────────────────────────────────────────────────────────
COPY_STARTED=true
msg_info "Creating: ${TARGET_DIR}"

if command -v rsync &>/dev/null; then
    rsync -a --exclude='build/' --exclude='.cache/' "$SOURCE_DIR/" "$TARGET_DIR/"
else
    # 回退方案：逐项复制，排除 build/ 和 .cache/
    mkdir -p "$TARGET_DIR"
    for item in "$SOURCE_DIR"/*; do
        cp -r "$item" "$TARGET_DIR/"
    done
    # 复制隐藏文件/目录
    for item in "$SOURCE_DIR"/.*; do
        base="$(basename "$item")"
        [[ "$base" == "." || "$base" == ".." || "$base" == ".cache" ]] && continue
        cp -r "$item" "$TARGET_DIR/"
    done
fi

msg_success "Files copied (build/ and .cache/ excluded)."

# ── 替换项目名: CMakeLists.txt ───────────────────────────────────────────────
sed -i "s/project(${SOURCE_CMAKE_NAME} /project(${CMAKE_NAME} /" \
    "${TARGET_DIR}/CMakeLists.txt"
msg_success "CMakeLists.txt: project name -> ${CMAKE_NAME}"

# ── 替换项目名: .vscode/launch.json ─────────────────────────────────────────
sed -i "s/${SOURCE_CMAKE_NAME}/${CMAKE_NAME}/g" \
    "${TARGET_DIR}/.vscode/launch.json"
msg_success "launch.json: executable -> ${CMAKE_NAME}.elf"

# ── 验证替换结果 ─────────────────────────────────────────────────────────────
if grep -rq "$SOURCE_CMAKE_NAME" "${TARGET_DIR}/CMakeLists.txt" \
    "${TARGET_DIR}/.vscode/launch.json" 2>/dev/null; then
    msg_warn "Some occurrences of '${SOURCE_CMAKE_NAME}' remain. Manual review needed."
fi

# ── 完成 ─────────────────────────────────────────────────────────────────────
TARGET_DIR=""  # 清空，防止 EXIT trap 误删
msg_success "Done! Tutorial created at: codes_and_assets/stm32f1_tutorials/${DIR_NAME}"
printf "\n  ${CYAN}cd codes_and_assets/stm32f1_tutorials/%s && mkdir build && cd build && cmake ..${NC}\n" "${DIR_NAME}"
