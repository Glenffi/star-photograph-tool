# StarProcessor — 星空摄影师 RAW 处理工具

跨平台 RAW 图像处理软件，专注于星空摄影领域。

> **当前阶段**：P2+。核心闭环、天地分离、局部 PSF 缩星、轻量 RAW 预览和核心自动化测试已经接入；Windows 仍需实机验证。

## 当前已实现

- 多张 RAW 图片叠加堆栈降噪
- 星点检测、Affine / Homography 自动选择、全画幅 3×3 网格质量门禁
- Average / Median / Kappa-Sigma / Winsorized 堆栈
- 天空对齐、地景固定的天地分离堆栈
- 亮度引导去雾、低频背景色偏校正与 RGB 联动 Arcsinh 拉伸
- 低阈值星点检测、亮星 PSF 收缩、暗弱小星渐隐和局部 RGB 背景重建的自动缩星
- 线性 sRGB 16-bit TIFF（嵌入 ICC）和 sRGB 8-bit PNG 导出
- 内嵌 RAW 缩略图优先、half-size 快速回退的浏览预览
- 磁盘缓存分块堆栈、基于实时可用内存的处理前资源门禁，以及受控分辨率结果预览
- 基于全部对齐变换的共同有效区域自动裁切，避免少帧覆盖边界进入成片

## 规划中

- 校准帧（Dark / Flat / Bias）
- 延时图片序列降噪、星轨合成
- 云端 AI 参数建议
- GPU 加速
- Windows 构建与 CI 持续验证

## 系统要求

| 平台 | 最低版本 | 内存 | 磁盘空间 |
|------|---------|------|---------|
| macOS | 12+ (Monterey) | 8 GB | 5 GB + 序列缓存 |
| Windows | 10/11 | 8 GB | 5 GB + 序列缓存 |

> 处理高分辨率 RAW 文件（如 60MP+）建议 16 GB 以上内存。

## 技术栈

| 组件 | 技术 | 最低版本 | License |
|------|------|---------|---------|
| UI | Qt 6 | 6.2+ | LGPLv3 |
| RAW 解码 | LibRaw | 0.21+ | LGPLv2.1/CDDL |
| 图像处理（纯开源） | 自研算法（星点检测 + 局部背景估计 + PSF 收缩） | — | MIT |
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
│   │   ├── ProcessingMemoryEstimator.h/cpp # 跨平台物理内存与处理峰值估算
│   │   ├── PreviewToneMapper.h/cpp    # 16-bit 结果的有界 8-bit 显示映射
│   │   ├── RawImageLoader.h/cpp       # RAW 文件加载与解码
│   │   ├── ThumbnailGenerator.h/cpp   # 异步缩略图生成
│   │   ├── StarDetector.h/cpp         # 星点检测与 2D 高斯拟合
│   │   ├── ImageAligner.h/cpp         # 基于星点的图像对齐
│   │   ├── StackingEngine.h/cpp       # 堆栈降噪（均值/中值/Kappa-Sigma/Winsorized）
│   │   ├── StarReducer.h/cpp          # 缩星处理（局部背景保护 + PSF 径向收缩）
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
├── tools/
│   ├── RawSampleRegression.cpp  # 真实 RAW 解码、星点与序列对齐回归工具
│   └── RawPipelineRunner.cpp    # 复用生产 worker 的完整流程验证工具
├── build.sh                     # 一键构建/测试脚本（macOS）
├── run-sample-regression.sh     # 构建并运行本地样片回归
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
./build/StarProcessor.app/Contents/MacOS/StarProcessor
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

## 真实 RAW 样片回归

样片默认放在代码目录旁的 `star-photograph-tool-samples`。工具优先识别约定的五类目录，也会自动发现其他包含 RAW 的一级目录。完整模式会逐张执行正式 AHD 解码、星点检测和同目录序列对齐，并在 `build/sample-regression-output` 写入 `report.json` 与检查预览。对齐使用独立于拟合星点的评估星点集，报告 Affine 与 Homography 两个候选模型的 RMS、P95、外圈 P95、匹配覆盖率和 3×3 网格指标。

```bash
# 完整回归
./run-sample-regression.sh

# 只检查元数据和浏览预览
./run-sample-regression.sh --quick

# 对一个大目录先完整检查排序后的前 8 张
./run-sample-regression.sh --category star-raw --limit 8

# 15 张序列使用排序后的第 8 张（索引 7）作为参考帧
./run-sample-regression.sh --category star-raw --limit 15 --reference-index 7

# 将上一份报告作为基线；能力或结果退化时返回非零退出码
./run-sample-regression.sh --baseline /path/to/baseline-report.json --strict
```

退出码 `4` 表示样片目录为空，`5` 表示基线文件无效，`6` 表示严格检查或基线比较失败。RAW 样片通常不应提交到公开仓库，报告中只记录相对文件路径。

要验证与 GUI 完全相同的完整处理 worker，构建时启用
`-DBUILD_SAMPLE_TOOLS=ON`，然后运行：

```bash
./build/StarProcessorPipelineRunner \
  --input ../star-photograph-tool-samples/star-raw \
  --output build/pipeline-output \
  --limit 15 --reference-index 7 --method kappa-sigma --kappa 2.5 \
  --stretch --star-reduce-strength 40
```

工具会生成完整分辨率 TIFF 和 `pipeline-report.json`。报告分别记录全流程耗时、纯堆栈耗时、通道样本吞吐量、自动裁切偏移和画质选项。`--stretch` 启用背景校正与 RGB 联动拉伸；`--dehaze-strength` 和 `--star-reduce-strength` 接受 `0–100`。处理期间，对齐帧写入系统临时目录并按 32 行分块堆栈；任务正常、失败或取消后都会自动清理。

`--memory-budget-mib` 可为测试或受控运行设置更低的内存上限。该值只能收紧平台安全预算，不能绕过实时内存门禁；传 `0` 或省略参数时使用自动预算。

## 已知限制

- **正式 RAW 解码**：LibRaw AHD + 相机白平衡 + 颜色矩阵，输出线性 sRGB 原色的 16-bit RGB
- **浏览预览**：优先使用相机内嵌 JPEG，回退到 half-size 快速解码；预览不参与最终处理
- **内存与磁盘**：完整处理采用磁盘缓存和 32 行分块堆栈，RAM 峰值不再随帧数线性增长；自动预算同时受物理内存 65% 上限与实时可用内存约束，并为系统保留至少 1 GiB；启动前按天地分离、去雾、拉伸和缩星选项估算 RAM，同时检查带 10% 余量的临时磁盘空间
- **结果预览**：16-bit 处理结果会映射为最长边不超过 4096 px 的 8-bit 显示缓存；TIFF/PNG 导出始终使用完整分辨率结果
- **预设**：当前仅提供“银河广角”和“深空天体”；单帧降噪与延时序列在专用流程完成前不显示
- **天地检测**：传统 CV 自动蒙版需要人工预览确认，复杂山脊、云层和强光污染场景可能误判
- **自动优化**：背景校正使用低分辨率鲁棒网格拟合加性光污染与色偏，并与 RGB 联动 Arcsinh 拉伸组合；DCP 去雾只适合明显薄雾，银河暗尘丰富时应关闭
- **自动裁切**：天空对齐后自动裁到所有成功帧的共同有效区域，避免零填充边界造成单帧/少帧接缝；长序列或大位移会相应损失少量画幅
- **统计堆栈性能**：Kappa-Sigma 需要对每个通道样本排序和迭代裁剪；15 张 36 MP Sony RAW 实测完整流程约 30 秒，明显慢于 Average，但 RAM 峰值不随帧数线性增长
- **超广角长序列对齐**：当前会在全局 Affine 与 Homography 之间自动选择；14 mm 固定机位 15 张实测已消除旧仿射结果的明显边缘多重拖线。Homography 仍不能校正镜头局部畸变、滚动快门或复杂局部形变，跨镜头和更长序列仍需样片验证
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
