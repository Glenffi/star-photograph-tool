#!/bin/bash
set -e

echo "🌟 StarProcessor — 一键编译启动脚本"
echo "======================================"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}"
BUILD_DIR="${PROJECT_DIR}/build"

# 检查平台
PLATFORM=$(uname -s)
ARCH=$(uname -m)

echo ""
echo -e "${BLUE}平台信息：${NC} ${PLATFORM} (${ARCH})"

# 配置 Homebrew PATH（如果需要）
if [ -x "/opt/homebrew/bin/brew" ] && [ -z "$(which cmake 2>/dev/null)" ]; then
    echo -e "${YELLOW}检测到 Homebrew 安装在 /opt/homebrew/bin，添加到 PATH${NC}"
    export PATH="/opt/homebrew/bin:$PATH"
fi

if [ -x "/usr/local/bin/brew" ] && [ -z "$(which cmake 2>/dev/null)" ]; then
    echo -e "${YELLOW}检测到 Homebrew 安装在 /usr/local/bin，添加到 PATH${NC}"
    export PATH="/usr/local/bin:$PATH"
fi

# 检查依赖
echo ""
echo "🔍 检查依赖..."

MISSING_DEPS=()

# 检查 CMake
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}❌ CMake 未安装${NC}"
    MISSING_DEPS+=("cmake")
else
    CMAKE_VERSION=$(cmake --version | head -1 | awk '{print $3}')
    echo -e "${GREEN}✅ CMake ${CMAKE_VERSION}${NC}"
fi

# 检查 Qt6
if [ -d "/opt/homebrew/opt/qt@6" ]; then
    export CMAKE_PREFIX_PATH="/opt/homebrew/opt/qt@6"
    echo -e "${GREEN}✅ Qt6 (Homebrew: /opt/homebrew/opt/qt@6)${NC}"
elif [ -d "/usr/local/opt/qt@6" ]; then
    export CMAKE_PREFIX_PATH="/usr/local/opt/qt@6"
    echo -e "${GREEN}✅ Qt6 (Homebrew: /usr/local/opt/qt@6)${NC}"
else
    echo -e "${RED}❌ Qt6 未找到（期望路径：/opt/homebrew/opt/qt@6 或 /usr/local/opt/qt@6）${NC}"
    MISSING_DEPS+=("qt@6")
fi

# 检查 LibRaw
if [ -d "/opt/homebrew/opt/libraw" ]; then
    echo -e "${GREEN}✅ LibRaw (Homebrew: /opt/homebrew/opt/libraw)${NC}"
elif [ -d "/usr/local/opt/libraw" ]; then
    echo -e "${GREEN}✅ LibRaw (Homebrew: /usr/local/opt/libraw)${NC}"
else
    echo -e "${RED}❌ LibRaw 未找到（期望路径：/opt/homebrew/opt/libraw）${NC}"
    MISSING_DEPS+=("libraw")
fi

# 如果有缺失依赖，提示安装
if [ ${#MISSING_DEPS[@]} -ne 0 ]; then
    echo ""
    echo -e "${RED}=======================================${NC}"
    echo -e "${RED}缺少依赖：${MISSING_DEPS[*]}${NC}"
    echo -e "${RED}=======================================${NC}"
    echo ""
    echo "请安装缺失的依赖："
    echo ""
    echo "  # 1. 安装 Homebrew（如果未安装）"
    echo "  /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
    echo ""
    echo "  # 2. 安装依赖"
    echo "  brew install cmake qt@6 libraw"
    echo ""
    echo "  # 3. 确保 PATH 包含 brew（如果是 Apple Silicon）"
    echo "  echo 'export PATH=/opt/homebrew/bin:\$PATH' >> ~/.zshrc"
    echo "  source ~/.zshrc"
    echo ""
    exit 1
fi

# 解析命令行参数
CLEAN_BUILD=false
BUILD_ONLY=false
for arg in "$@"; do
    case "$arg" in
        --clean)
            CLEAN_BUILD=true
            ;;
        --build-only)
            BUILD_ONLY=true
            ;;
        --help)
            echo "用法: $0 [选项]"
            echo ""
            echo "选项:"
            echo "  --clean       清理 build 目录后重新配置（完整重建）"
            echo "  --build-only  仅编译，不启动应用"
            echo "  --help        显示此帮助信息"
            exit 0
            ;;
    esac
done

# 配置 Git 代理（用于推送到 GitHub）
if [ -x "$(which scutil 2>/dev/null)" ]; then
    # macOS - 检测系统代理
    HTTP_PROXY=$(scutil --proxy 2>/dev/null | grep HTTPProxy | awk '{print $3}' | head -1)
    HTTP_PORT=$(scutil --proxy 2>/dev/null | grep HTTPPort | awk '{print $3}' | head -1)
    if [ -n "$HTTP_PROXY" ] && [ -n "$HTTP_PORT" ]; then
        echo -e "${BLUE}🌐 检测到系统代理：${HTTP_PROXY}:${HTTP_PORT}${NC}"
        git config http.proxy "http://${HTTP_PROXY}:${HTTP_PORT}"
        git config https.proxy "http://${HTTP_PROXY}:${HTTP_PORT}"
        echo -e "${GREEN}✅ Git 代理已配置${NC}"
    fi
fi

# 创建并进入 build 目录
echo ""
echo "🔨 配置 CMake..."

if [ "$CLEAN_BUILD" = true ]; then
    echo -e "${YELLOW}⚠️  清理 build 目录（完整重建）${NC}"
    rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

if [ ! -f "CMakeCache.txt" ]; then
    echo -e "${BLUE}首次配置 CMake...${NC}"
fi

cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH}" \
    2>&1

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ CMake 配置失败${NC}"
    exit 1
fi

echo -e "${GREEN}✅ CMake 配置成功${NC}"

# 编译
echo ""
echo "🔨 开始编译..."
CPU_COUNT=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
echo -e "${BLUE}使用 ${CPU_COUNT} 个线程并行编译${NC}"

make -j${CPU_COUNT} 2>&1

if [ $? -ne 0 ]; then
    echo -e "${RED}❌ 编译失败${NC}"
    exit 1
fi

echo -e "${GREEN}✅ 编译成功${NC}"

if [ "$BUILD_ONLY" = true ]; then
    echo ""
    echo -e "${BLUE}📦 仅编译模式，跳过启动应用${NC}"
    exit 0
fi

# 查找可执行文件
echo ""
echo "🚀 启动应用..."

if [ "$PLATFORM" == "Darwin" ]; then
    # macOS: StarProcessor.app/Contents/MacOS/StarProcessor
    APP_PATH="${BUILD_DIR}/StarProcessor.app/Contents/MacOS/StarProcessor"
else
    # Windows/Linux
    APP_PATH="${BUILD_DIR}/StarProcessor"
fi

if [ ! -f "$APP_PATH" ]; then
    echo -e "${RED}❌ 找不到可执行文件: ${APP_PATH}${NC}"
    exit 1
fi

echo -e "${GREEN}✅ 找到可执行文件: ${APP_PATH}${NC}"
echo ""

# 启动应用
"$APP_PATH" &
APP_PID=$!

sleep 1

if kill -0 $APP_PID 2>/dev/null; then
    echo -e "${GREEN}✅ StarProcessor 已启动！PID: ${APP_PID}${NC}"
    echo ""
    echo "======================================"
    echo "  应用已运行，按 Ctrl+C 关闭日志输出"
    echo "  应用将在后台继续运行"
    echo "======================================"
    wait $APP_PID 2>/dev/null || true
else
    echo -e "${RED}❌ 应用启动失败${NC}"
    exit 1
fi
