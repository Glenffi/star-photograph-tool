# StarProcessor — 星空摄影师 RAW 处理工具

跨平台 RAW 图像处理软件，专注于星空摄影领域。

> **当前阶段**：P1 — 核心引擎。已实现 RAW 加载、星点检测、图像对齐、堆栈降噪与导出功能。

## 核心功能

- 多张 RAW 图片叠加堆栈降噪
- 延时图片序列降噪（输出图片序列）
- 自动优化（去雾、曲线调整）
- 自动缩星（纯开源方案）
- AI 自动建议（云端方案）
- 星轨图片处理

## 系统要求

| 平台 | 最低版本 | 内存 | 磁盘空间 |
|------|---------|------|---------|
| macOS | 12+ (Monterey) | 8 GB | 2 GB |
| Windows | 10/11 | 8 GB | 2 GB |

> 处理高分辨率 RAW 文件（如 60MP+）建议 16 GB 以上内存。

## 技术栈

| 组件 | 技术 | 最低版本 | License |
|------|------|---------|---------|
| UI | Qt 6 | 6.2+ | LGPLv3 |
| RAW 解码 | LibRaw | 0.21+ | LGPLv2.1/CDDL |
| 图像处理（规划中） | OpenCV | — | Apache-2.0 |
| 图像导出 | libtiff | — | BSD-2-Clause |
| 构建 | CMake | 3.20+ | — |
| AI 云端 | FastAPI + Docker | — | MIT/BSD |

## 项目结构

```
StarProcessor/
├── src/
│   ├── main.cpp                 # 主入口与 MainWindow
│   ├── core/
│   │   ├── RawImageLoader.h/cpp       # RAW 文件加载与解码
│   │   ├── ThumbnailGenerator.h/cpp   # 异步缩略图生成
│   │   ├── StarDetector.h/cpp         # 星点检测与 2D 高斯拟合
│   │   ├── ImageAligner.h/cpp         # 基于星点的图像对齐
│   │   ├── StackingEngine.h/cpp       # 堆栈降噪（均值/中值）
│   │   └── ImageExporter.h/cpp        # 16-bit TIFF 导出
│   └── ui/
│       ├── ProjectPanel.h/cpp         # 左侧面板：文件列表
│       ├── PreviewPanel.h/cpp         # 中央面板：图像预览
│       ├── ParamsPanel.h/cpp          # 右侧面板：处理参数
│       └── Toolbar.h/cpp              # 顶部工具栏
├── cmake/
│   └── Info.plist.in            # macOS Bundle 配置
├── build.sh                     # 一键构建脚本（macOS）
├── CMakeLists.txt               # CMake 构建配置
└── README.md                    # 本文件
```

## 构建

### 快速开始（macOS）

使用提供的一键构建脚本：

```bash
# 增量编译（推荐日常开发）
./build.sh

# 完整清理后重建
./build.sh --clean

# 仅编译，不启动应用
./build.sh --build-only
```

### 手动构建（macOS）

```bash
# 1. 安装依赖（需 Homebrew）
brew install cmake qt@6 libraw libtiff

# 2. 配置环境变量
export PATH=/opt/homebrew/bin:$PATH
export CMAKE_PREFIX_PATH=$(brew --prefix qt@6)

# 3. 构建
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)

# 4. 运行
./StarProcessor.app/Contents/MacOS/StarProcessor
```

### Windows

```powershell
# 使用 vcpkg 安装 Qt6 和 LibRaw
vcpkg install qtbase qtdeclarative libraw tiff

mkdir build; cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake -G "Visual Studio 17 2022"
cmake --build . --config Release
.\Release\StarProcessor.exe
```

> **注意**：当前 P1 阶段已实现核心处理链路（对齐 → 堆栈 → 导出），自动优化（去雾、曲线）与缩星功能将在后续版本迭代。

## 已知限制

- **P1 阶段**：核心对齐、堆栈、导出已可用；自动优化（去雾、曲线调整）与缩星功能尚未实现
- **Bayer 解码**：当前使用简单最近邻插值，质量低于专业 debayer 算法
- **色彩管理**：输出色彩空间转换尚未集成，当前为 sRGB 预览
- **AI 功能**：云端 AI 建议服务需单独部署后端（见 `ai-service/` 目录，后续发布）

## 贡献指南

1. Fork 本仓库
2. 创建功能分支：`git checkout -b feat/your-feature`
3. 提交修改：`git commit -am 'feat(scope): description'`
4. 推送分支：`git push origin feat/your-feature`
5. 创建 Pull Request

### Commit Message 规范

```
feat(scope): 新功能
docs(scope): 文档更新
fix(scope): 修复问题
chore(scope): 工程卫生/构建脚本
refactor(scope): 重构（无功能变更）
```

## License

[MIT License](LICENSE)
