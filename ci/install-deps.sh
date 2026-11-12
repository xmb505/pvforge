#!/usr/bin/env bash
# PVForge 本地/CI 共用的依赖安装脚本。
#
#   ci/install-deps.sh              # 只装 apt 依赖（CI 用这条）
#   ci/install-deps.sh --qt 6.7.3   # 再装一份 Qt 6.7.3 到 build/qt（本地用）
#
# 为什么本地需要 aqt：Ubuntu 24.04 仓库里的 Qt 是 6.4.2（qt6-base-dev），
# 而 CMakeLists.txt 要求 Qt 6.5+（`find_package(Qt6 6.5 REQUIRED ...)`），
# 6.4.2 会直接 configure 失败。所以本地要么用发行版更新（Debian sid / 26.04）
# 自带的 Qt，要么用 aqtinstall 装 6.6.x / 6.7.x：
#   ci/install-deps.sh --qt 6.7.3
#   cmake --preset debug -DCMAKE_PREFIX_PATH="$PWD/build/qt/6.7.3/gcc_64"
#
# CI 里 Qt 由 jurplel/install-qt-action 安装，本脚本只负责系统依赖，
# 两边用同一份 apt 列表，避免 workflow 和本地漂移。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QT_VERSION=""

while [ $# -gt 0 ]; do
    case "$1" in
        --qt)
            QT_VERSION="${2:?--qt 需要版本号，例如 6.7.3}"
            shift 2
            ;;
        -h|--help)
            sed -n '2,20p' "${BASH_SOURCE[0]}"
            exit 0
            ;;
        *)
            echo "未知参数：$1（支持 --qt <version>）" >&2
            exit 2
            ;;
    esac
done

# 构建依赖：
# - ninja-build / ccache / pkg-config / git：构建与缓存
# - libav*-dev：CMakeLists.txt 里 pkg_check_modules 硬性要求的 FFmpeg 开发库
# - libgl1-mesa-dev / libxkbcommon-dev / libxcb-*：Qt Widgets/MM 链接与运行所需
# - libpulse-dev / libasound2-dev：Qt Multimedia 后端
# - xvfb：没有 offscreen 平台插件可用的自检时跑 GUI
# - python3-venv：--qt 用 aqtinstall 时装在独立虚拟环境里
APT_PACKAGES=(
    build-essential
    cmake
    ninja-build
    ccache
    pkg-config
    git
    libavformat-dev
    libavcodec-dev
    libavutil-dev
    libswscale-dev
    libswresample-dev
    libgl1-mesa-dev
    libxkbcommon-dev
    libxkbcommon-x11-0
    libpulse-dev
    libasound2-dev
    libxcb-cursor0
    libxcb-icccm4
    libxcb-image0
    libxcb-keysyms1
    libxcb-randr0
    libxcb-render-util0
    libxcb-shape0
    libxcb-xkb1
    xvfb
    python3-venv
)

echo "==> 安装 apt 依赖（需要 sudo）"
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    SUDO="sudo"
fi
$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends "${APT_PACKAGES[@]}"

echo "==> 依赖版本"
pkg-config --modversion libavformat libavcodec 2>/dev/null || true
cmake --version | head -1
ninja --version

if [ -z "$QT_VERSION" ]; then
    cat <<'EOF'

apt 依赖已就绪。Qt 版本提示：
  - Ubuntu 24.04 的 qt6-base-dev 是 6.4.2，低于项目要求的 6.5，不能用于构建。
  - 本地请用 `ci/install-deps.sh --qt 6.7.3` 装一份 Qt 6.6.x/6.7.x。
  - CI 里 Qt 由 jurplel/install-qt-action 安装。
EOF
    exit 0
fi

echo "==> 用 aqtinstall 安装 Qt ${QT_VERSION} 到 build/qt"
venv="$ROOT/build/qt-venv"
python3 -m venv "$venv"
# shellcheck disable=SC1091
. "$venv/bin/activate"
pip install --disable-pip-version-check -q --upgrade pip
pip install --disable-pip-version-check -q aqtinstall
# 架构名要问 aqt：Qt 6.7 起桌面 Linux 从 gcc_64 改名成 linux_gcc_64。
QT_ARCH="$(aqt list-qt linux desktop --arch "$QT_VERSION" | awk '{print $1}')"
if [ -z "$QT_ARCH" ]; then
    echo "找不到 Qt ${QT_VERSION} 的 Linux 桌面架构，检查版本号（aqt list-qt linux desktop）" >&2
    exit 1
fi
# qtmultimedia 是 qtbase 之外唯一需要的模块（Widgets/Concurrent/Network/Sql/Test 都在 qtbase 里）。
aqt install-qt linux desktop "$QT_VERSION" "$QT_ARCH" \
    -m qtmultimedia \
    --outputdir "$ROOT/build/qt"

echo
echo "Qt 安装完成：$ROOT/build/qt/$QT_VERSION/$QT_ARCH"
echo "配置调试构建："
echo "  cmake --preset debug -DCMAKE_PREFIX_PATH=\"$ROOT/build/qt/$QT_VERSION/$QT_ARCH\""
