# 复验

从仓库根目录运行 `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SAMPLE_TOOLS=ON`、`cmake --build build --parallel`、`ctest --test-dir build -C Release --output-on-failure`。用 `./build.sh` 启动桌面应用。

1. 选择一张成片，导入一张 RAW，核对并保存；打开回执目录，用第二份素材同路径替换后新开单，检查手动导出资格。
2. 选择星轨，导入三张同方向竖拍文件；选择延时，导入三张打乱文件名顺序的 RAW，确认回执目录与排序。
3. 选择深空，补齐 Dark、Flat、Bias 或适用的 Master；把 1 ms Flat 与 10 ms Dark Flat 配对时应报告曝光错误。
4. 静候纸面光影并切换静止；切到英文和较小窗口，在中性暗房对同一位置做调整预览。

## 审查编号与可执行验收

| 编号 | CTest 分组 | 检查内容 |
|---|---|---|
| R01、R02 | `output_delivery`、`print_order_flow` | 并发发布保住已有成片；清空后，即使新 RAW 使用相同路径、大小和时间，也不能导出旧像素。正式或手动保存完成前切换素材，不得把旧结果记入当前单据。 |
| R03、R09、R10 | `worker_lifecycle` | 检查竖幅星轨、同卷与异卷空间、独占序列目录，以及共同有效区不足的失败反馈。 |
| R04、R05 | `core_algorithms` | 拟合星与留出星分别保持空间覆盖；预检和 CFA 校准共用短曝光匹配规则。 |
| R06 | `print_order_flow`、`output_delivery` | 银河启用天地分离，调地景细节时快速预览与同参数的正式结果一致；冲印单印样采用与成品 PNG 相同的显示转换。 |
| R07、R08 | `timelapse_cli` | 未支持选项返回错误。报告参考帧、源路径、输出编号使用同一帧表。 |
| 原生交互 | `print_order_panel`、`print_order_flow`、`localization_catalog` | 双语单据、静置光影、减弱动效、独立的照片像素、取消、回执和高级暗房往返。 |

`timelapse_cli` 需要配置 `-DBUILD_SAMPLE_TOOLS=ON`。Windows 配置另提供 `timelapse_windows_volume`。如有可写 SMB 共享，可另外运行 `python tests/TimelapseWindowsVolumeTests.py <runner.exe> --unc-dir <共享目录> --mapped-dir <同目录的盘符别名>`。
