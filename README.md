# StarProcessor — 星空摄影师 RAW 处理工具

跨平台 RAW 图像处理软件，专注于星空摄影领域。

> **当前开发版本**：v0.10.0。新增地景粗笔迹引导的边缘贴合蒙版，以及 Dark Flat、可复用 Master 文件与多种标准深空校准组合。

## 当前已实现

- 多张 RAW 图片叠加堆栈降噪
- 星点检测、Affine / Homography 自动选择、全画幅 3×3 网格质量门禁；稀疏网格的尾差验收允许一个孤立误匹配，但保留原始 P95，且继续受局部 RMS 与全局 RMS/P95 约束
- 轻量预览帧质量评分、自动参考帧选择与严重失焦/拖星/云层帧保守剔除
- Average / Median / Kappa-Sigma / Winsorized 堆栈
- 深空标准校准：支持 Bias 或同曝光 Dark Flat 校准 Flat，并可批量生成 Master Bias / Dark / Dark Flat / Flat；在 Bayer CFA 域校准 Light 后再 AHD 去马赛克
- 可复用 `.spmaster`：保存浮点 CFA 像素、相机/ISO/曝光、有效传感器区域、Bayer 排列和校准状态，分层 SHA-256 同时保护元数据与像素；每类 RAW 原片组与对应 Master 二选一
- 深空素材聚合预检：处理前一次检查数量、重复路径、RAW 头可读性、相机、尺寸、ISO、Light/Dark 曝光和 Bias 最短曝光，并按问题类型汇总受影响文件
- 帧间光度匹配：稳健估计共享曝光增益与 RGB 背景偏移，并回到序列中位光度
- 天空对齐、地景固定的天地分离堆栈；每帧天空有效区随对齐变换，避免移动地景在山脊上方形成重影；地景支持平均降噪、参考单帧和中值三种策略
- 延时 RAW 序列滑动窗口降噪：逐帧星空对齐、MAD 异常值抑制、动态内容保护、跨帧亮度/色偏平滑和固定地景双路处理，并按原顺序批量输出图片
- 固定机位星轨合成：原坐标 RGB16 Lighten、按时间半衰期渐隐的正向/反向 Comet、帧间背景匹配，以及天空星轨与原位平均地景融合
- 连续地平线自动蒙版或用户蒙版；自动结果漏掉山体/建筑时，可用“修补地景”粗略涂抹，算法沿预览中的颜色与亮度边缘重新贴合精确边界；多尺度降噪、缩星和细节恢复均受天空/地景区域约束
- 线性 RGB 多尺度降噪，亮度与色度分离处理并保护强结构
- BCF/天文改机色彩还原：在线性域自动采样中性天空，或从结果预览吸取手动灰点；校正强度可在 0–100% 连续调整
- 亮度引导去雾、连续的低频背景色偏/亮度校正与 RGB 联动 Arcsinh 拉伸；背景模型使用共享亮度肩部和有符号色度偏移，不再由三个通道各自的硬阈值形成彩色等值环；高光保留 85% 以上亮度层次，并通过 90% 起始的混合保色肩部避免高饱和星云或亮星单通道硬截断
- ACR 风格基础调整：色温偏移、色调、曝光、对比度、高光、阴影、白色、黑色、自然饱和度和饱和度；明暗调整联动 RGB 亮度，颜色超界时围绕目标亮度收敛色度，避免逐通道硬裁切形成新色带
- 亮度通道阈值锐化：保护深阴影与亮星核心，重建 RGB 时保持色相；默认关闭，建议在线性降噪之后以 10–35 的保守强度使用
- 局部 RGB 背景重建的 Starless/Stars 分离、亚像素圆形 Minimum 和弱残留清理
- 线性 sRGB 16-bit TIFF（嵌入 ICC）和 sRGB 8-bit PNG 导出
- 完整分辨率手动导出在独立任务中执行；TIFF 扫描行和 PNG 转换支持协作取消，取消的 TIFF 不保留未完成文件
- 内嵌 RAW 缩略图优先、half-size 快速回退的浏览预览
- 磁盘缓存分块堆栈、基于实时可用内存的处理前资源门禁，以及受控分辨率结果预览
- 基于全部对齐变换的共同有效区域自动裁切，避免少帧覆盖边界进入成片
- 面向六类拍摄任务重新设计的桌面工作台：编号式场景入口、紧凑素材队列、影像画布和按“堆栈 / 调整 / 输出”组织的参数页
- 有界内存的“处理前 / 处理后 / 分屏”比较；分屏会将两张完整图片等比例并排，快速预览刷新会保持比较模式、缩放和滚动位置
- HTTPS 客户端更新检查：按平台读取版本清单，流式下载到系统“下载”目录，并在打开安装包前校验文件大小和 SHA-256

## 规划中

- 星轨断点修补与长序列分段合成
- 云端 AI 参数建议
- GPU 加速
- Windows 不同相机、系统语言和安装路径的持续实机验证

## 系统要求

| 平台 | 最低版本 | 内存 | 磁盘空间 |
|------|---------|------|---------|
| macOS | 14+ (Sonoma，源码目标) | 8 GB | 5 GB + 序列缓存 |
| Windows | 10/11 | 8 GB | 5 GB + 序列缓存 |

> 处理高分辨率 RAW 文件（如 60MP+）建议 16 GB 以上内存。

## 技术栈

| 组件 | 技术 | 最低版本 | License |
|------|------|---------|---------|
| UI | Qt 6 | 6.2+ | LGPLv3 |
| RAW 解码 | LibRaw | 0.21+ | LGPLv2.1/CDDL |
| 图像处理（纯开源） | 自研算法（星点检测 + 多尺度降噪 + Starless/Stars + 亚像素圆形 Minimum） | — | MIT |
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
│   │   ├── MinimumFilter.h/cpp        # 去雾暗通道的线性时间方形最小值滤波
│   │   ├── ProcessingMemoryEstimator.h/cpp # 跨平台物理内存与处理峰值估算
│   │   ├── PreviewToneMapper.h/cpp    # 16-bit 结果的有界 8-bit 显示映射
│   │   ├── RawImageLoader.h/cpp       # RAW 文件加载与解码
│   │   ├── ThumbnailGenerator.h/cpp   # 异步缩略图生成
│   │   ├── StarDetector.h/cpp         # 星点检测与 2D 高斯拟合
│   │   ├── SkyGroundMask.h/cpp        # 连续地平线检测、粗笔迹引导精修、用户蒙版与羽化
│   │   ├── MasterFrameIO.h/cpp        # 带元数据和校验的 .spmaster 读写
│   │   ├── ImageAligner.h/cpp         # 基于星点的图像对齐
│   │   ├── FrameQualityEvaluator.h/cpp # 预览帧评分、参考帧选择与差帧门禁
│   │   ├── StackingEngine.h/cpp       # 堆栈降噪（均值/中值/Kappa-Sigma/Winsorized）
│   │   ├── StarTrailEngine.h/cpp       # 流式 Lighten / Comet 星轨合成
│   │   ├── TimelapseEngine.h/cpp       # 延时序列滑动窗口稳健时域降噪
│   │   ├── PhotometricNormalizer.h/cpp # 帧间曝光与背景色偏匹配
│   │   ├── NoiseReductionEngine.h/cpp # 线性 RGB 多尺度亮度/色度降噪
│   │   ├── BasicAdjustmentEngine.h/cpp # ACR 风格明暗、色彩与亮度锐化
│   │   ├── StarReducer.h/cpp          # Starless/Stars 分离 + 亚像素圆形 Minimum
│   │   ├── ImageExporter.h/cpp        # 16-bit TIFF / PNG 8-bit 导出
│   │   ├── AutoOptimizeEngine.h/cpp   # 去雾、Arcsinh 拉伸与蒙版地景细节恢复
│   │   ├── UpdateManifest.h/cpp       # 更新清单校验与语义版本比较
│   │   └── PresetManager.h/cpp        # 内置预设与用户预设持久化
│   ├── ui/
│   │   ├── ProjectPanel.h/cpp         # 左侧面板：文件列表
│   │   ├── PreviewPanel.h/cpp         # 中央面板：图像预览
│   │   ├── ParamsPanel.h/cpp          # 右侧面板：处理参数
│   │   └── Toolbar.h/cpp              # 顶部工具栏
│   ├── update/
│   │   └── UpdateManager.h/cpp        # HTTPS 检查、下载、大小与 SHA-256 校验
│   └── workers/
│       ├── ProcessingWorker.h/cpp     # 正式处理管线后台执行
│       ├── ExportWorker.h/cpp         # 完整分辨率手动导出的可取消后台任务
│       └── MaskPreviewWorker.h/cpp    # 天地蒙版快速预览
├── cmake/
│   └── Info.plist.in            # macOS Bundle 配置
├── tests/
│   ├── CoreTests.cpp            # 核心算法与 TIFF ICC 回归测试
│   ├── WorkerTests.cpp          # 任务取消与失败状态测试
│   ├── StarTrailEngineTests.cpp # 星轨累积、渐隐方向与输入校验测试
│   └── PreviewPanelTests.cpp    # 完整分屏与快速预览视图状态测试
├── tools/
│   ├── RawSampleRegression.cpp  # 真实 RAW 解码、星点与序列对齐回归工具
│   └── RawPipelineRunner.cpp    # 复用生产 worker 的完整流程验证工具
├── build.sh                     # 一键构建/测试脚本（macOS）
├── run-sample-regression.sh     # 构建并运行本地样片回归
├── CMakeLists.txt               # CMake 构建配置
└── README.md                    # 本文件
```

## 界面工作流

应用首先按拍摄任务提供“单张 RAW 精修、银河星景堆栈、深空天体堆栈、天地分离合成、星轨合成、星空延时序列降噪”六个场景入口。选择后进入三栏工作台；工具栏的“场景”按钮可随时返回，已有素材不会丢失。“开始处理”会按当前场景的素材门槛激活，导出只在需要手动导出的任务成功后开放。

场景会改变素材门槛、步骤条、参数基线和可见参数。单张精修只要求 1 张 RAW，并直接执行降噪、拉伸、缩星和导出，不进行对齐堆栈；银河和天地场景运行各自的真实堆栈流程。深空场景把左侧素材视为 Light，并要求提供 Dark 与 Flat；Flat 的偏移校准可选 Bias 或同曝光 Dark Flat，各校准角色也可直接导入 StarProcessor `.spmaster`，只有组合完整且元数据兼容后才允许开始。星轨场景要求至少 3 张固定机位 RAW，不做星点对齐；彗星强度 0% 为连续 Lighten，其他值按时间半衰期生成渐隐尾迹，并可反转亮端方向。固定地景默认通过自动地平线蒙版单独平均，避免取亮合成污染前景。延时场景同样要求至少 3 张 RAW，但以 3/5 帧滑动窗口逐张输出。BCF 或天文改机素材可在“调整”页启用“BCF 改机色彩还原”；自动灰点适合多数银河画面，完成一次正式处理后也可用吸管在处理前预览中选择中性天空，并通过参数快速预览调整 0–100% 校正强度。普通相机默认关闭。堆栈算法下方会随选择显示适用场景、优势与代价，非 Kappa-Sigma 模式会自动禁用无效的 κ 控件。品牌标志和操作图标均由 Qt 运行时自绘，不依赖系统图标或外部 SVG。

当降噪、自动优化、地景细节或缩星启用时，worker 会在这些收尾步骤之前保存一张最长边不超过 2400 px 的 8-bit 堆栈预览。处理结束后可查看处理前、处理后，或将两张完整图片等比例并排比较；切回某张 RAW 检查后，可用“查看结果”返回最近成片。该设计不会额外保留一张完整分辨率 16-bit RGB 副本。

完成一次正式处理后，应用还会缓存最长边 2400 px 的线性 RGB16 堆栈和对应天地蒙版。只修改降噪、改机色彩、去雾、拉伸、地景细节或缩星参数时，界面在 400 ms 防抖后仅重跑共用收尾管线，并在后台任务间协作取消过期请求；对齐、堆栈、参考帧、校准帧或天地合成参数变化仍要求完整重算。快速预览会持续标明分辨率和“完整导出需重新处理”，且不会错误开放导出按钮。刷新结果时保留用户当前的处理前/后/分屏模式、适应或 100% 缩放及滚动位置；缩星开启时状态栏还会显示实际处理星数，便于确认参数已经生效。

界面结构、视觉变量和离屏截图方法见 [`docs/ui-workflow.md`](docs/ui-workflow.md)。

## 客户端更新

从 v0.9.0 起，应用每天最多静默检查一次更新，也可通过“帮助 → 检查更新”手动执行。发现新版本后由用户确认下载；应用不会静默安装。安装包必须来自项目的 HTTPS 下载目录，并同时通过清单声明的文件大小与 SHA-256 校验，才会保存到系统“下载”目录并允许打开。

清单协议、服务器目录、单版本保留策略和发布命令见 [`docs/update-distribution.md`](docs/update-distribution.md)。服务器凭据不进入仓库。

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

从 v0.8.1 起，RAW 输入、用户蒙版和 TIFF/PNG 输出均保留 Qt Unicode
路径，并在 Windows 分别调用 LibRaw 与 libtiff 的宽字符接口。中文用户名、
中文父目录和中文文件名不需要改名；不支持或损坏的 RAW 会明确显示“RAW
读取失败”，不再一直停留在元数据加载状态。

> **注意**：当前处理链路为：对齐 → 帧间光度匹配 → 堆栈 → 改机色彩还原 → 线性降噪 → 去雾/自动拉伸 → 基础调色 → 地景细节 → 亮度锐化 → Starless/Stars 缩星 → 导出。未启用的阶段会直接跳过。

## 客户端打包

macOS 可在安装 Homebrew 依赖后生成自包含 DMG：

```bash
./scripts/package-macos.sh
```

输出位于 `dist/StarProcessor-v<version>-macOS-<arch>.dmg`，同时生成
SHA-256 校验文件。脚本会运行 Release 构建、CTest、`macdeployqt`、包内
依赖审计、ad-hoc 签名和启动截图检查。当前没有 Apple Developer 证书，
因此本地发行包尚未公证，首次打开时可能需要在 Finder 中右键选择“打开”。
打包脚本还会检测所有随包动态库的真实系统要求并写入应用信息；若 Homebrew
依赖高于源码的 macOS 14 目标，最终 DMG 会以依赖的较高版本为准。

Windows x64 客户端由 `.github/workflows/windows-package.yml` 在
Windows Server 2022 上使用 MSVC 原生构建。推送与 CMake 版本一致的
`v*` 标签后，Windows 与 macOS 工作流会分别运行测试、封装依赖并把 ZIP/DMG
及 SHA-256 文件发布到 GitHub Releases。macOS 正式包在 macOS 14 arm64 runner
上生成，构建会拒绝任何实际最低系统版本高于 14.0 的依赖。

客户端打包流程见 [`docs/release-packaging.md`](docs/release-packaging.md)，更新服务器发布流程见 [`docs/update-distribution.md`](docs/update-distribution.md)。

## 真实 RAW 样片回归

样片默认放在代码目录旁的 `star-photograph-tool-samples`。工具优先识别约定的五类目录，也会自动发现其他包含 RAW 的一级目录。完整模式会逐张执行正式 AHD 解码、星点检测和同目录序列对齐，并在 `build/sample-regression-output` 写入 `report.json` 与检查预览。对齐使用独立于拟合星点的评估星点集，报告 Affine 与 Homography 两个候选模型的 RMS、P95、外圈 P95、匹配覆盖率和 3×3 网格指标。局部网格同时保留原始 P95 与去除一个最坏样本后的稳健 P95：前者用于诊断，后者用于避免三颗星网格被一个误匹配直接否决；局部 RMS 和全局门限仍会拒绝成组残差。

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

# 深空标准校准：input 为 Light；各 RAW 校准组至少 3 张，建议 10–20 张
./build/StarProcessorPipelineRunner \
  --input /path/to/light --dark-dir /path/to/dark \
  --flat-dir /path/to/flat --bias-dir /path/to/bias \
  --output build/deep-sky-output --method winsorized \
  --denoise-strength 30 --stretch --save-generated-masters

# 长曝光 Flat 可用同曝光 Dark Flat 替代 Bias；下次可直接复用生成的 Master
./build/StarProcessorPipelineRunner \
  --input /path/to/light --master-dark /path/to/master_dark.spmaster \
  --master-flat /path/to/master_flat.spmaster \
  --output build/deep-sky-reuse --method winsorized --stretch

# 单张精修：只读取排序后的第一张 RAW，不执行对齐和堆栈
./build/StarProcessorPipelineRunner \
  --input ../star-photograph-tool-samples/star-raw \
  --output build/single-output --single --denoise-strength 25 \
  --restore-modified-camera-color --modified-camera-color-strength 75 --stretch \
  --exposure 0.3 --highlights -25 --shadows 15 \
  --vibrance 18 --sharpening 25

# 手动灰点使用 0–1 归一化坐标，并自动启用改机色彩还原
./build/StarProcessorPipelineRunner \
  --input ../star-photograph-tool-samples/star-raw \
  --output build/manual-gray-output --single \
  --modified-camera-gray-point 0.82,0.25 \
  --modified-camera-color-strength 75 --stretch

# 固定机位星景：天空对齐，地景保持原坐标
./build/StarProcessorPipelineRunner \
  --input ../star-photograph-tool-samples/star-raw \
  --output build/sky-ground-output --limit 15 \
  --method kappa-sigma --sky-ground --sky-ground-feather 20 \
  --ground-method average --ground-detail-strength 40 \
  --denoise-strength 35 --stretch --star-reduce-strength 90

# 从排序后的第 7 张开始取 14 张，复现目录中间的连续拍摄段
./build/StarProcessorPipelineRunner \
  --input ../star-photograph-tool-samples/star-raw-2 \
  --output build/normal-galaxy-segment --start-index 6 --limit 14 \
  --method kappa-sigma --sky-ground --denoise-strength 35 \
  --stretch --star-reduce-strength 70

# 星空延时：每张输入 RAW 对应一张输出，5 帧窗口、动态保护并固定地景
./build/StarProcessorPipelineRunner \
  --input ../star-photograph-tool-samples/star-raw \
  --output build/timelapse-output --limit 15 --timelapse \
  --timelapse-window 5 --timelapse-strength 80 \
  --timelapse-motion-protection 75

# 星轨合成：0 为连续 Lighten；提高后按时间渐隐，可加 --star-trail-reverse
./build/StarProcessorPipelineRunner \
  --input ../star-photograph-tool-samples/star-raw-3 \
  --output build/star-trail-output --limit 15 --star-trail \
  --star-trail-comet-strength 65 --stretch
```

命令行回归工具与 GUI 使用同一套校准组合：`--dark-dir`、`--flat-dir`、`--bias-dir`、`--dark-flat-dir` 接收 RAW 目录；`--master-dark`、`--master-flat`、`--master-bias`、`--master-dark-flat` 接收 `.spmaster`，同一角色的 RAW 组与 Master 不可同时指定。`--save-generated-masters` 会把本次由 RAW 生成并通过完整校验的 Master 原子提交到输出目录。校准素材必须与 Light 来自同一机身、ISO、传感器有效区和 Bayer 布局；Dark 还必须匹配 Light 曝光，Dark Flat 必须匹配 Flat 曝光，Bias 必须为短曝光。正式解包 CFA 前会聚合报告重复素材及相机/尺寸/ISO/曝光不匹配；像素相关的 Flat 欠曝、接近饱和和 CFA 布局仍在正式校准阶段继续检查。

工具会生成完整分辨率 TIFF、固定全范围的 `result-preview.png` 和 `pipeline-report.json`。报告分别记录全流程耗时、纯堆栈耗时、每帧质量评分、结构化跳帧原因、光度模型、自动裁切偏移、输出直方图和画质选项；当整组素材的中位星点椭率达到 0.22 时，还会写入 `starShapeWarning`，提示检查单帧曝光拖线或镜头像差。`--start-index` 先跳过指定数量的已排序 RAW，再由 `--limit` 限制数量，`--reference-index` 始终相对于最终选中的片段。`--restore-modified-camera-color` 在线性 RGB 阶段自动估计并校正 BCF/天文改机的通道响应，`--modified-camera-color-strength` 接受 `0–100`，`--modified-camera-gray-point x,y` 以归一化坐标改用手动稳健邻域；报告会记录模式、强度、中性样本、RGB 增益和高光截断数量。普通相机不应默认启用。天地分离模式还会保存本次实际使用的 `sky-ground-mask.png`，并记录蒙版来源与天空占比，便于检查自动检测是否可靠；`--sky-ground-mask /path/to/mask.png` 可改用白色天空、黑色地景的用户蒙版。`--ground-method` 接受 `average`、`reference` 或 `median`，默认 `average`；`--ground-detail-strength` 接受 `0–70`，默认 40。天地模式下，多尺度降噪和缩星只作用于天空，地景由原坐标多帧合成及可选细节恢复处理。延时模式为每张输入生成一张同名序号输出；`--timelapse-window` 接受 `3` 或 `5`，`--timelapse-strength` 和 `--timelapse-motion-protection` 接受 `0–100`，后者默认为 75；`--timelapse-no-ground` 可关闭固定地景保护。延时管线会根据天空区域估计整段序列的亮度与色偏曲线，并以受限的 5 帧中值平滑抑制孤立闪烁，不会强行拉平日出、月升等连续曝光趋势。`--reference-index -1` 默认自动选择参考帧；`--no-quality-rejection` 保留严重质量离群帧，但仍执行自动参考帧选择。帧间光度匹配默认开启；`--no-photometric-normalization` 可关闭帧间匹配和延时防闪烁，用于 A/B 检查。`--denoise-strength` 接受 `0–70`；`--stretch` 先进行背景校正，再根据背景中值和高光端自动求解 RGB 联动 Arcsinh 曲线，亮度高光继续使用剩余输出空间，高饱和像素在 90% 以上通过保色与最低亮度约束的混合肩部收敛到 99.5%，避免直接夹到 65535；`--dehaze-strength` 和 `--star-reduce-strength` 接受 `0–100`。处理期间，完整帧写入任务专属系统临时目录；任务正常、失败或取消后都会自动清理临时缓存。

基础调色参数与 GUI 使用同一引擎：`--temperature`、`--tint`、`--contrast`、`--highlights`、`--shadows`、`--whites`、`--blacks`、`--vibrance` 和 `--saturation` 接受 `-100..100`，`--exposure` 接受 `-5.0..5.0 EV`，`--sharpening` 接受 `0..100`。这些参数及结果统计会写入 `pipeline-report.json`，便于同一 RAW 的 A/B 对照。延时逐帧输出当前不支持这组收尾参数。

`--memory-budget-mib` 可为测试或受控运行设置更低的内存上限。该值只能收紧平台安全预算，不能绕过实时内存门禁；传 `0` 或省略参数时使用自动预算。

## 已知限制

- **正式 RAW 解码**：LibRaw AHD + 相机白平衡 + 颜色矩阵，输出线性 sRGB 原色的 16-bit RGB
- **BCF 改机色彩还原**：自动模式依赖画面中存在足够的近中性天空样本；极光、黎明、强气辉或大面积发射星云可在完成一次正式处理后改用手动灰点。吸管只记录画面位置，实际取样始终在线性 RGB16 邻域完成；切换素材、参考帧或场景会清除旧灰点。普通相机照片应保持关闭
- **深空校准**：当前支持重复 2×2 Bayer RAW，尚不支持 X-Trans、单色传感器和自动温度匹配。Dark 应与 Light 同曝光、同 ISO 且尽量同温度；Dark Flat 应与 Flat 同曝光；Flat 应保持原光路、焦点与灰尘位置。`.spmaster` 是 StarProcessor 的传感器域复用格式，不应改名冒充普通 RAW
- **浏览预览**：优先使用相机内嵌 JPEG，回退到 half-size 快速解码；预览像素不进入成片，自动天地蒙版只借助其显示曲线识别地平线结构
- **内存与磁盘**：完整处理采用磁盘缓存和 32 行分块堆栈，RAM 峰值不再随帧数线性增长；自动预算同时受物理内存 65% 上限与实时可用内存约束，并为系统保留至少 1 GiB；启动前按天地分离、降噪、去雾、拉伸和 Starless/Stars 缩星选项估算 RAM，同时检查带 10% 余量的临时磁盘空间
- **结果预览**：16-bit 处理结果会映射为最长边不超过 4096 px 的 8-bit 显示缓存；处理前和参数快速预览最长边不超过 2400 px。快速预览复用正式收尾算法，但缩放后星点与噪声的像素尺度仍可能和完整分辨率略有差异；TIFF/PNG 导出始终使用重新处理后的完整分辨率结果
- **场景与预设**：银河广角和深空天体保留可编辑预设；单张精修、星轨合成和延时序列使用各自的专用管线，序列工具不显示无关的普通堆栈预设
- **星轨合成**：当前面向固定机位、同尺寸且按拍摄顺序排列的 RAW。Lighten 会保留飞机、卫星和车灯等亮轨；Comet 只衰减高于稳健背景的信号，并以首帧的低频背景作为合成基准。跨越月升、日出或剧烈光污染变化的长序列建议先分段合成。尚未提供断点修补和用户地景蒙版，长间隔或掉帧产生的空隙会原样保留
- **延时序列**：当前只输出 TIFF/PNG 图片序列，不编码视频；序列边缘会使用实际可用的较小窗口。元数据、RAW 解码、邻帧对齐和光度预分析均提供单调进度，窗口处理与逐行导出会响应取消。同一素材即使被多个滑动窗口复用，光度统计仍只按源帧计一次。动态保护可以减少云层、草木和灯光拖影，但快速大范围变化仍应降低时域强度。固定地景依赖自动地平线检测，复杂前景检测失败时会退回纯天空处理；取消任务时已经导出的图片会保留，尚不支持断点续跑
- **天地检测**：自动模式在相机内嵌预览上寻找连续地平线，并在比例异常或预览不可用时回退线性图；漏检地景可用橙色粗笔迹作为地景种子，松手后自动沿颜色/亮度梯度贴边。复杂树冠孔洞、透明枝叶或无明确边缘时仍可能需要用户蒙版
- **地景清晰度**：默认地景 Average 已提供多帧降噪，后续空间降噪和缩星不会再处理地景；默认 40 强度分别恢复小尺度纹理和中尺度清晰度，中尺度权重从地平线向近处前景递减。风吹草叶、人物移动、单帧失焦、景深不足或明显机位移动仍应改用 Median、参考单帧或重新拍摄
- **自动优化**：背景校正使用低分辨率鲁棒网格拟合加性光污染与色偏，色度校正为连续有符号偏移，亮度校正使用平滑肩部后再进入 RGB 联动 Arcsinh 拉伸；它避免旧版逐通道硬阈值在强拉伸后显出环状色阶。DCP 暗通道使用与旧方形邻域逐像素等价的可分离线性最小值滤波，并复用 Guided Filter 工作平面以控制大图峰值内存。去雾只适合明显薄雾，银河暗尘丰富时应关闭
- **缩星边界**：当前 Starless 是传统 CV 的局部背景近似，不是 AI 去星；强度 40/70/90 分别适合温和、强烈和多数暗弱小星清除。星层采用 16 方向亚像素圆形亮度腐蚀，避免整数像素十字核产生方向偏置，并会自动校正未饱和中小星相对星核突增的彩色边缘。饱和大星和宽光晕默认保持原样，防止形成暗环；衍射芒、单帧曝光拖线、密集星云结节和不同焦段仍应在 100% 比例复核
- **自动裁切**：天空对齐后自动裁到所有成功帧的共同有效区域，避免零填充边界造成单帧/少帧接缝；长序列或大位移会相应损失少量画幅
- **统计堆栈性能**：Kappa-Sigma 需要对每个通道样本排序和迭代裁剪；15 张 36 MP Sony RAW 实测完整流程约 30 秒，明显慢于 Average，但 RAM 峰值不随帧数线性增长
- **超广角长序列对齐**：当前会在全局 Affine 与 Homography 之间自动选择；14 mm 固定机位 15 张实测已消除旧仿射结果的明显边缘多重拖线。Homography 仍不能校正镜头局部畸变、滚动快门或复杂局部形变，跨镜头和更长序列仍需样片验证
- **测试样片**：算法合成测试和本地多组 Sony/Canon RAW 回归已接入；Windows 与 macOS 14 arm64 打包 CI 均已建立。仍缺 X-Trans、无内嵌预览 RAW 与多台 Windows 实机的持续样片覆盖

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
