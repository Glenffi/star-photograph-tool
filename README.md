# StarProcessor — 星空摄影师 RAW 处理工具

跨平台 RAW 图像处理软件，专注于星空摄影领域。

> **当前阶段**：P2+。核心闭环、天地分离、延时图片序列降噪、线性多尺度降噪、Starless/Stars 缩星、轻量 RAW 预览和核心自动化测试已经接入；Windows 仍需实机验证。

## 当前已实现

- 多张 RAW 图片叠加堆栈降噪
- 星点检测、Affine / Homography 自动选择、全画幅 3×3 网格质量门禁
- 轻量预览帧质量评分、自动参考帧选择与严重失焦/拖星/云层帧保守剔除
- Average / Median / Kappa-Sigma / Winsorized 堆栈
- 帧间光度匹配：稳健估计共享曝光增益与 RGB 背景偏移，并回到序列中位光度
- 天空对齐、地景固定的天地分离堆栈；地景支持平均降噪、参考单帧和中值三种策略
- 延时 RAW 序列滑动窗口降噪：逐帧星空对齐、MAD 异常值抑制、动态内容保护、跨帧亮度/色偏平滑和固定地景双路处理，并按原顺序批量输出图片
- 连续地平线自动蒙版或用户蒙版；多尺度降噪、缩星和细节恢复均受天空/地景区域约束
- 线性 RGB 多尺度降噪，亮度与色度分离处理并保护强结构
- 亮度引导去雾、低频背景色偏校正与 RGB 联动 Arcsinh 拉伸
- 局部 RGB 背景重建的 Starless/Stars 分离、星层圆形 Minimum 和弱残留清理
- 线性 sRGB 16-bit TIFF（嵌入 ICC）和 sRGB 8-bit PNG 导出
- 内嵌 RAW 缩略图优先、half-size 快速回退的浏览预览
- 磁盘缓存分块堆栈、基于实时可用内存的处理前资源门禁，以及受控分辨率结果预览
- 基于全部对齐变换的共同有效区域自动裁切，避免少帧覆盖边界进入成片
- 面向实际处理状态的三栏工作台：素材管理、结果预览、按“堆栈 / 调整 / 输出”分组的参数页
- 有界内存的“处理前 / 处理后 / 分屏”比较，以及从单张素材快速返回最近处理结果

## 规划中

- 校准帧（Dark / Flat / Bias）
- 星轨合成
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
| 图像处理（纯开源） | 自研算法（星点检测 + 多尺度降噪 + Starless/Stars + 圆形 Minimum） | — | MIT |
| 图像导出 | libtiff | — | BSD-2-Clause |
| 构建 | CMake | 3.20+ | — |
| AI 云端 | FastAPI + Docker | — | MIT/BSD |

## 项目结构

```
StarProcessor/
├── src/
│   ├── main.cpp                 # 主入口、MainWindow 与后台处理编排
│   ├── core/
│   │   ├── ImageBufferUtils.h/cpp     # RGB 校验、亮度提取、通道转换与天地融合
│   │   ├── ProcessingMemoryEstimator.h/cpp # 跨平台物理内存与处理峰值估算
│   │   ├── PreviewToneMapper.h/cpp    # 16-bit 结果的有界 8-bit 显示映射
│   │   ├── RawImageLoader.h/cpp       # RAW 文件加载与解码
│   │   ├── ThumbnailGenerator.h/cpp   # 异步缩略图生成
│   │   ├── StarDetector.h/cpp         # 星点检测与 2D 高斯拟合
│   │   ├── SkyGroundMask.h/cpp        # 连续地平线检测、用户蒙版与羽化
│   │   ├── ImageAligner.h/cpp         # 基于星点的图像对齐
│   │   ├── FrameQualityEvaluator.h/cpp # 预览帧评分、参考帧选择与差帧门禁
│   │   ├── StackingEngine.h/cpp       # 堆栈降噪（均值/中值/Kappa-Sigma/Winsorized）
│   │   ├── TimelapseEngine.h/cpp       # 延时序列滑动窗口稳健时域降噪
│   │   ├── PhotometricNormalizer.h/cpp # 帧间曝光与背景色偏匹配
│   │   ├── NoiseReductionEngine.h/cpp # 线性 RGB 多尺度亮度/色度降噪
│   │   ├── StarReducer.h/cpp          # Starless/Stars 分离 + 星层圆形 Minimum
│   │   ├── ImageExporter.h/cpp        # 16-bit TIFF / PNG 8-bit 导出
│   │   ├── AutoOptimizeEngine.h/cpp   # 去雾、Arcsinh 拉伸与蒙版地景细节恢复
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

## 界面工作流

应用首先按拍摄任务提供“单张 RAW 精修、银河星景堆栈、深空天体堆栈、天地分离合成、星空延时序列降噪”五个场景入口。选择后进入三栏工作台；工具栏的“场景”按钮可随时返回，已有素材不会丢失。“开始处理”会按当前场景的素材门槛激活，导出只在需要手动导出的任务成功后开放。

场景会改变素材门槛、步骤条、参数基线和可见参数。单张精修只要求 1 张 RAW，并直接执行降噪、拉伸、缩星和导出，不进行对齐堆栈；银河、深空和天地场景运行各自的真实堆栈流程。延时场景要求至少 3 张 RAW，以 3/5 帧滑动窗口逐张输出，并隐藏与它无关的普通堆栈和自动优化参数。延时页可调时域强度和动态内容保护；后者会在云层、草木、灯光等局部变化处减少邻帧贡献。堆栈算法下方会随选择显示适用场景、优势与代价，非 Kappa-Sigma 模式会自动禁用无效的 κ 控件。品牌标志和操作图标均由 Qt 运行时自绘，不依赖系统图标或外部 SVG。

当降噪、自动优化、地景细节或缩星启用时，worker 会在这些收尾步骤之前保存一张最长边不超过 2400 px 的 8-bit 堆栈预览。处理结束后可查看处理前、处理后或 50/50 分屏；切回某张 RAW 检查后，可用“查看结果”返回最近成片。该设计不会额外保留一张完整分辨率 16-bit RGB 副本。

界面结构、视觉变量和离屏截图方法见 [`docs/ui-workflow.md`](docs/ui-workflow.md)。

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

> **注意**：当前 P2+ 已实现核心处理链路（对齐 → 帧间光度匹配 → 堆栈 → 线性降噪 → 自动优化 → Starless/Stars 缩星 → 导出）。

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
  --denoise-strength 30 --stretch --star-reduce-strength 70

# 单张精修：只读取排序后的第一张 RAW，不执行对齐和堆栈
./build/StarProcessorPipelineRunner \
  --input ../star-photograph-tool-samples/star-raw \
  --output build/single-output --single --denoise-strength 25 --stretch

# 固定机位星景：天空对齐，地景保持原坐标
./build/StarProcessorPipelineRunner \
  --input ../star-photograph-tool-samples/star-raw \
  --output build/sky-ground-output --limit 15 \
  --method kappa-sigma --sky-ground --sky-ground-feather 20 \
  --ground-method average --ground-detail-strength 40 \
  --denoise-strength 35 --stretch --star-reduce-strength 90

# 星空延时：每张输入 RAW 对应一张输出，5 帧窗口、动态保护并固定地景
./build/StarProcessorPipelineRunner \
  --input ../star-photograph-tool-samples/star-raw \
  --output build/timelapse-output --limit 15 --timelapse \
  --timelapse-window 5 --timelapse-strength 80 \
  --timelapse-motion-protection 75
```

工具会生成完整分辨率 TIFF 和 `pipeline-report.json`。报告分别记录全流程耗时、纯堆栈耗时、每帧质量评分、光度模型、自动裁切偏移和画质选项。天地分离模式还会保存本次实际使用的 `sky-ground-mask.png`，并记录蒙版来源与天空占比，便于检查自动检测是否可靠；`--sky-ground-mask /path/to/mask.png` 可改用白色天空、黑色地景的用户蒙版。`--ground-method` 接受 `average`、`reference` 或 `median`，默认 `average`；`--ground-detail-strength` 接受 `0–70`，默认 40。天地模式下，多尺度降噪和缩星只作用于天空，地景由原坐标多帧合成及可选细节恢复处理。延时模式为每张输入生成一张同名序号输出；`--timelapse-window` 接受 `3` 或 `5`，`--timelapse-strength` 和 `--timelapse-motion-protection` 接受 `0–100`，后者默认为 75；`--timelapse-no-ground` 可关闭固定地景保护。延时管线会根据天空区域估计整段序列的亮度与色偏曲线，并以受限的 5 帧中值平滑抑制孤立闪烁，不会强行拉平日出、月升等连续曝光趋势。`--reference-index -1` 默认自动选择参考帧；`--no-quality-rejection` 保留严重质量离群帧，但仍执行自动参考帧选择。帧间光度匹配默认开启；`--no-photometric-normalization` 可关闭帧间匹配和延时防闪烁，用于 A/B 检查。`--denoise-strength` 接受 `0–70`；`--stretch` 启用背景校正与 RGB 联动拉伸；`--dehaze-strength` 和 `--star-reduce-strength` 接受 `0–100`。处理期间，完整帧写入任务专属系统临时目录；任务正常、失败或取消后都会自动清理临时缓存。

`--memory-budget-mib` 可为测试或受控运行设置更低的内存上限。该值只能收紧平台安全预算，不能绕过实时内存门禁；传 `0` 或省略参数时使用自动预算。

## 已知限制

- **正式 RAW 解码**：LibRaw AHD + 相机白平衡 + 颜色矩阵，输出线性 sRGB 原色的 16-bit RGB
- **浏览预览**：优先使用相机内嵌 JPEG，回退到 half-size 快速解码；预览像素不进入成片，自动天地蒙版只借助其显示曲线识别地平线结构
- **内存与磁盘**：完整处理采用磁盘缓存和 32 行分块堆栈，RAM 峰值不再随帧数线性增长；自动预算同时受物理内存 65% 上限与实时可用内存约束，并为系统保留至少 1 GiB；启动前按天地分离、降噪、去雾、拉伸和 Starless/Stars 缩星选项估算 RAM，同时检查带 10% 余量的临时磁盘空间
- **结果预览**：16-bit 处理结果会映射为最长边不超过 4096 px 的 8-bit 显示缓存；可选处理前预览最长边不超过 2400 px；TIFF/PNG 导出始终使用完整分辨率结果
- **场景与预设**：银河广角和深空天体保留可编辑预设；单张精修和延时序列使用各自的专用管线，延时场景不显示无关的普通堆栈预设
- **延时序列**：当前只输出 TIFF/PNG 图片序列，不编码视频；序列边缘会使用实际可用的较小窗口。动态保护可以减少云层、草木和灯光拖影，但快速大范围变化仍应降低时域强度。固定地景依赖自动地平线检测，复杂前景检测失败时会退回纯天空处理；取消任务时已经导出的图片会保留，尚不支持断点续跑
- **天地检测**：自动模式在相机内嵌预览上寻找连续地平线，并在比例异常或预览不可用时回退线性图；复杂树冠、建筑孔洞、云层贴地或无明确地平线时仍需人工预览并改用用户蒙版
- **地景清晰度**：默认地景 Average 已提供多帧降噪，后续空间降噪和缩星不会再处理地景；默认 40 强度分别恢复小尺度纹理和中尺度清晰度，中尺度权重从地平线向近处前景递减。风吹草叶、人物移动、单帧失焦、景深不足或明显机位移动仍应改用 Median、参考单帧或重新拍摄
- **自动优化**：背景校正使用低分辨率鲁棒网格拟合加性光污染与色偏，并与 RGB 联动 Arcsinh 拉伸组合；DCP 去雾只适合明显薄雾，银河暗尘丰富时应关闭
- **缩星边界**：当前 Starless 是传统 CV 的局部背景近似，不是 AI 去星；强度 40/70/90 分别适合温和、强烈和多数暗弱小星清除。密集星云结节、饱和大星、衍射芒和不同焦段仍应在 100% 比例复核
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
