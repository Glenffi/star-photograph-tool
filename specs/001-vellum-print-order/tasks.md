# 实施清单

- [x] T001 在 `specs/001-vellum-print-order/` 建立规格与验收契约。
- [x] T002 [P] [US1] 实现 `src/ui/PrintOrderPanel` 和资源，完成三步、真实依赖提示与返回。
- [x] T003 [US1] 由 `src/app/MainWindowOrder.cpp` 与 `MainWindow.cpp` 将导入、六场景、保存及回执与现有 worker 接通。
- [x] T004 [P] [US2] 修复 R01 并发交付和 R02 项目清空，校验 `ImageExporter` 与主窗口各入口。
- [x] T005 [P] [US4] 完成 R03/R09/R10 与 `ProcessingWorker` 的结构化阶段。
- [x] T006 [P] [US4] 完成 R04/R05 的 `StarDetector` 与校准共享规则和回归。
- [x] T007 [P] [US4] 完成 R07/R08 的 CLI 参数、排序报告和文档。
- [x] T008 [US3] 在 `src/ui` 落实待机光影、减弱动态效果、检查位置、开关与参考一致性，修复 R06。
- [x] T009 [US2] 验证真实单据、同秒写盘、清空、取消，以及文件和序列回执。
- [x] T010 编译英文资源，执行全套 CTest、六种场景及小窗口双语验收，更新 README 和工作流说明。

T002、T005、T006、T007 可并行。T003 集成面板契约和 worker 事件；T009、T010 在上述实现可用后执行。
