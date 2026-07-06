# StarProcessor UI 重写计划

## 阶段 1: 创建新组件（并行）
- [x] 读取所有现有文件
- [ ] 重写 ProjectPanel.h/.cpp — 自定义卡片式布局
- [ ] 新增 Toolbar.h/.cpp — 顶部工具栏
- [ ] 重写 PreviewPanel.h/.cpp — QLabel + QScrollArea 方案
- [ ] 新增 ParamsPanel.h/.cpp — 右侧参数面板

## 阶段 2: 集成
- [ ] 更新 main.cpp — 集成所有新组件
- [ ] 更新 CMakeLists.txt — 添加新源文件

## 设计决策
- ProjectPanel: QScrollArea + QWidget 容器 + 垂直布局，每个文件是自定义卡片
- Toolbar: QWidget + QHBoxLayout，自定义样式按钮
- PreviewPanel: QScrollArea + QLabel，支持缩放（Ctrl+滚轮/按钮）和拖拽平移
- ParamsPanel: QScrollArea + 垂直布局，可折叠 QGroupBox 组
- 主窗口：Toolbar + 三栏布局 + 步骤条
