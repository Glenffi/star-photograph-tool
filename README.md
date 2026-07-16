# StarProcessor — 星空摄影师 RAW 处理工具

跨平台 RAW 图像处理软件，专注于星空摄影领域。

> **当前阶段**：P2+。核心闭环、天地分离、形态学缩星、轻量 RAW 预览和核心自动化测试已经接入；Windows 仍需实机验证。

## 当前已实现

- 多张 RAW 图片叠加堆栈降噪
- 星点检测、仿射对齐、Average / Median / Kappa-Sigma / Winsorized 堆栈
- 天空对齐、地景固定的天地分离堆栈
- Dark Channel Prior 去雾与 Arcsinh 曲线拉伸
- 星点遮罩、形态学腐蚀和羽化混合的自动缩星
- 线性 sRGB 16-bit TIFF（嵌入 ICC）和 sRGB 8-bit PNG 导出
- 内嵌 RAW 缩略图优先、half-size 快速回退的浏览预览

## 规划中

- 校准帧（Dark / Flat / Bias）
- 延时图片序列降噪、星轨合成
- 云端 AI 参数建议
- GPU 加速、磁盘分块和内存预算
- Windows 构建与 CI 持续验证

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
| 图像处理（纯开源） | 自研算法（星点检测 + 形态学腐蚀） | — | MIT |
| 图像导出 | libtiff | — | BSD-2-Clause |
| 构建 | CMake | 3.20+ | — |
| AI 云端 | FastAPI + Docker | — | MIT/BSD |

## 项目结构

```
StarProcessor/
├── src/
│   ├── main.cpp                 # 主入口、MainWindow 与后台处理编排
│   ├── core/
│   │   ├── ImageBufferUtils.h/cpp     # RGB 校验、亮度提取与通道转换
│   │   ├── RawImageLoader.h/cpp       # RAW 文件加载与解码
│   │   ├── ThumbnailGenerator.h/cpp   # 异步缩略图生成
│   │   ├── StarDetector.h/cpp         # 星点检测与 2D 高斯拟合
│   │   ├── ImageAligner.h/cpp         # 基于星点的图像对齐
│   │   ├── StackingEngine.h/cpp       # 堆栈降噪（均值/中值/Kappa-Sigma/Winsorized）
│   │   ├── StarReducer.h/cpp          # 缩星处理（星点检测 + 形态学腐蚀）
│   │   ├── ImageExporter.h/cpp        # 16-bit TIFF / PNG 8-bit 导出
│   │   ├── AutoOptimizeEngine.h/cpp   # 自动优化：Dark Channel Prior 去雾 + Arcsinh 曲线拉伸
│   │   └── PresetManager.h/cpp        # 内置预设与用户预设持久化
│   ├── ui/
│   │   ├── ProjectPanel.h/cpp         # 左侧面板：文件列表
│   │   ├── PreviewPanel.h/cpp         # 中央面板：图像预览
│   │   ├── ParamsPanel.h/cpp          # 右侧面板：处理参数
│   │   └── Toolbar.h/cpp              # 顶部工具栏
│   └── workers/
│       ├── ProcessingWorker.h/cpp     # 正式处理管线后台执行
│       └── MaskPreviewWorker.h/cpp    # 天地蒙版快速预览
├── cmake/
│   └── Info.plist.in            # macOS Bundle 配置
├── tests/
│   ├── CoreTests.cpp            # 核心算法与 TIFF ICC 回归测试
│   └── WorkerTests.cpp          # 任务取消与失败状态测试
├── build.sh                     # 一键构建/测试脚本（macOS）
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

# 编译、测试后启动
./build.sh --test
```

### 手动构建（macOS）

```bash
# 1. 安装依赖（需 Homebrew）
brew install cmake qt@6 libraw libtiff

# 2. 配置环境变量
export PATH=/opt/homebrew/bin:$PATH
export CMAKE_PREFIX_PATH=$(brew --prefix qt@6)

# 3. 构建
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure

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

> **注意**：当前 P2 阶段已实现核心处理链路（对齐 → 堆栈 → 自动优化 → 缩星 → 导出）。

## 已知限制

- **正式 RAW 解码**：LibRaw AHD + 相机白平衡 + 颜色矩阵，输出线性 sRGB 原色的 16-bit RGB
- **浏览预览**：优先使用相机内嵌 JPEG，回退到 half-size 快速解码；预览不参与最终处理
- **内存**：处理流程仍会把可用帧保存在内存中，超高像素和大量帧尚未实现磁盘分块
- **天地检测**：传统 CV 自动蒙版需要人工预览确认，复杂山脊、云层和强光污染场景可能误判
- **测试样片**：算法合成测试已接入；跨机型 RAW 样片集和 Windows CI 尚未建立

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
