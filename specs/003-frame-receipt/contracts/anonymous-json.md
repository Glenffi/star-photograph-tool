# 匿名诊断 JSON v1

这是可以由用户主动保存的用片报告，不是冲印单，也不能据此找到或重开 RAW。

| 字段 | 语义 |
|---|---|
| `schemaVersion`、`sequence`、`deliverables` | 格式版本、是否为图片序列、正式交付的文件数。 |
| `submittedCount`、`usedCount`、`skippedCount`、`manualCount` | 实际提交、实际采用、已确认的自动或处理跳过，以及手动排除。未提交不算跳过。 |
| `rows[].frameNumber`、`outcome`、`reference` | 本次回执中从 1 开始的本地序号、固定的状态码，以及是否为本次参考。序号不是相机文件编号、路径的哈希或跨任务持久身份。 |
| `outputNumber` | 仅对延时中实际写出的行给出，从 1 开始，与输出序列顺序一致。 |
| `detectedStars`、`qualityScore`、`reasonCodes` | 可选诊断；缺省表示没有相应测量或可公开的原因码，不表示测出了零。 |

匿名序列化按白名单逐个构造字段，而非先导出完整报告再用正则遮盖路径。未知文本原因、源路径、文件名以及图片一概不写。仅保存在用户选择的本机文件，不会自动提交 GitHub Issue 或上传服务器。
