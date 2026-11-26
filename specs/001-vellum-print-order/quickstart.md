# 复验

从仓库根目录运行 `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`、`cmake --build build --parallel`、`ctest --test-dir build --output-on-failure`。用 `./build.sh` 启动桌面应用。

1. 选择一张成片，导入一张 RAW，核对并保存；打开回执目录，用第二份素材同路径替换后新开单，检查手动导出资格。
2. 选择星轨，导入三张同方向竖拍文件；选择延时，导入三张打乱文件名顺序的 RAW，确认回执目录与排序。
3. 选择深空，补齐 Dark、Flat、Bias 或适用的 Master；把 1 ms Flat 与 10 ms Dark Flat 配对时应报告曝光错误。
4. 静候纸面光影并切换静止；切到英文和较小窗口，在中性暗房对同一位置做调整预览。
