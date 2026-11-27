# 本机复验

从仓库根目录以 `-DBUILD_TESTING=ON -DBUILD_SAMPLE_TOOLS=ON` 配置 Release 构建。运行：

```bash
ctest --test-dir build -C Release --output-on-failure \
  -R '^(frame_receipt|worker_lifecycle|print_order_flow|print_order_panel|localization_catalog)$'
```

- 单张精修：在多张原片中手动排除一张，只精修另一张。确认交付 1 张，明细分别标为采用、手动排除和未提交。查看其他行不会切换主窗口选片。
- 堆栈：参考帧和使用/跳过状态以本次任务记录为准；在两个目录导入同名文件时，应仍能区分对应行。重新导出同一结果后，源帧记录不变。
- 延时：以实际时间排序为准，交付编号从 1 递增。取消、失败、清空项目和仅载入旧 TIFF 都不能生成本次成功回执。
- 匿名导出：在明细里显式保存 JSON，检查文件中只有本次运行内的编号、状态及允许的数值和原因，而非源路径或原始错误消息。未点击导出时，成品目录中没有额外报告。

有自己的山脊示例和对应基准 TIFF 时，可用 `tests/WindowFlowTests.py <窗口测试程序> --example <冲印单> <基准 TIFF>` 重跑正式 GUI 交付。这项私有素材验证独立于 CTest，不需要云端构建。

2026-11-27 本机 macOS Release：19 组 CTest 通过。另用 9 张真实 RAW 重放，采用 7 张，另两张均记录为对齐误差未通过；交付 1 张 5900×3931 RGB16 TIFF，与原基准逐像素一致。通过真实保存对话框导出的 JSON 保留帧序号、参考标记及门槛原因，但没有拍摄文件名或本机路径。真实照片、窗口截图及私有冲印单不进入 Git。本轮未做 Windows 实机测试。
