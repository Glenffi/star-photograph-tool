# Nebulens Mac App Store 上架物料

更新时间：2026-08-30

本目录是 Nebulens 首次上架 Mac App Store 的物料工作区。当前状态是**物料初稿已
准备，但二进制尚未达到可上传状态**。不要把现有 DMG、ad-hoc `.app` 或 GitHub
Actions artifact 直接上传 App Store Connect。

## 已准备

- `app-information.json`：建议的 Bundle ID、SKU、版本、分类和待填账号字段。
- `metadata/zh-Hans.json`、`metadata/en-US.json`：中英文名称、副标题、描述、
  关键词和 URL 草稿。
- `screenshots/`：中英文各 3 张 `2880×1800` PNG，无透明通道，符合 Mac 截图
  尺寸要求。
- `privacy-and-review.md`：App Privacy 答案、审核备注、联系人字段与内容版权清单。
- `privacy-policy.*.md`：中英文隐私政策长文本。
- `web/`：可部署的隐私政策与支持页面草稿。
- `technical-readiness.md`：当前代码到 Mac App Store 沙盒版之间的技术差距。
- `release-checklist.md`：从账号到正式发布的逐项勾选表。
- `source/validate_materials.py`：元数据、截图尺寸、Alpha 通道和占位符检查。

## 必须人工填写

全目录搜索 `__`，至少替换：

- 法律主体名称、Apple Developer 显示名称；
- Apple Team ID、App Store 自动生成的 Apple ID；
- 公开支持邮箱、电话和联系地址；
- App Review 联系人；
- 隐私政策生效日期；
- 定价、地区、欧盟 DSA trader 状态和中国大陆合规决定。

## 建议身份

- 显示名称：`Nebulens 星空影像工作台` / `Nebulens: Astro RAW Studio`
- 明确 Bundle ID：`net.nexusgen.nebulens`
- SKU：`NEBULENS-MAC-001`
- 首发版本：`1.0.0 (1)`
- 平台：macOS 14+，Apple silicon
- 主分类：Photo & Video；副分类：Utilities

Bundle ID 在上传构建后不能修改。注册前务必确认开发者账号归属和名称均已最终确定。

完整账号与送审步骤见 [`../docs/APPSTORE_SUBMISSION_RUNBOOK.md`](../docs/APPSTORE_SUBMISSION_RUNBOOK.md)。

