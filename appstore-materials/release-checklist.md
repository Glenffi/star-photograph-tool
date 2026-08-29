# Nebulens 1.0 Mac App Store 发布清单

## 账号与法律主体

- [ ] 决定以个人还是组织加入 Apple Developer Program。
- [ ] 组织账号的法律名称、注册地址、D-U-N-S 和统一社会信用代码一致。
- [ ] Account Holder 已接受最新 Apple Developer Program 协议。
- [ ] 已决定免费或付费；付费版已签 Paid Apps Agreement 并完成银行、美国税表及本地税表。
- [ ] 已完成中国开发者《国务院令第 810 号》信息。
- [ ] 已声明欧盟 DSA trader 状态；如为 trader，邮箱、电话和地址验证完成。
- [ ] 已决定是否首发中国大陆；没有有效 ICP/App 备案时先排除该地区。

## App 身份

- [ ] Explicit App ID `net.nexusgen.nebulens` 已注册。
- [ ] App Store Connect macOS 记录已创建，SKU 为 `NEBULENS-MAC-001`。
- [ ] App Store Connect Bundle ID 与包内 `CFBundleIdentifier` 完全一致。
- [ ] Mac App Distribution 证书及私钥位于构建机 Keychain。
- [ ] Mac Installer Distribution 证书及私钥位于构建机 Keychain。
- [ ] Mac App Store Connect provisioning profile 已生成或由 Xcode 自动管理。
- [ ] Team ID、Apple ID 和法律主体已回填 `app-information.json`。

## 技术 P0

- [ ] App Sandbox 已启用并实机验证。
- [ ] 自更新检查、下载和“检查更新”菜单已从商店构建移除。
- [ ] 所有输入/输出使用原生授权面板。
- [ ] 输出目录 security-scoped bookmark 能跨重启恢复。
- [ ] 未授权、bookmark stale、移动硬盘离线均有明确恢复流程。
- [ ] 商店构建不调用 `/usr/bin/open` 或其他 App 包外可执行文件。
- [ ] 版本为 `1.0.0`，build 为唯一递增整数。
- [ ] 隐私 manifest 位于 `Contents/Resources/PrivacyInfo.xcprivacy`。
- [ ] 所有动态库、Qt 插件、翻译、图标和许可证均在 App 包内且签名有效。

## 功能验收

- [ ] 单张、银河、深空、天地分离、星轨、延时六场景通过。
- [ ] RAW 文件、目录拖放、用户蒙版、Master 导入通过沙盒授权。
- [ ] 16-bit TIFF 与 8-bit PNG 能导出到已授权目录。
- [ ] 处理取消、内存不足、磁盘不足和损坏 RAW 不崩溃。
- [ ] 简体中文和 English 切换、重启和文件面板语言通过。
- [ ] 最新 macOS 与最低支持 macOS 14 Apple silicon 实机通过。
- [ ] 断网启动与完整本地处理通过。

## 商店物料

- [ ] 中英文名称与副标题确认，均不超过 30 字符。
- [ ] 中英文关键词不超过 100 bytes，未使用竞品或公司名称。
- [ ] 描述、宣传文本、支持 URL、Marketing URL、隐私 URL 可公网访问。
- [ ] 支持页包含真实邮箱、电话、地址；所有 `__PLACEHOLDER__` 已替换。
- [ ] 隐私政策生效日期与法律主体已填写。
- [ ] 中英文截图每组 1–10 张，统一 `2880×1800`、无 Alpha。
- [ ] 截图中的照片已取得 App Store 宣传授权。
- [ ] App Review 联系人和国际格式电话已填写。
- [ ] 审核 RAW 样片或下载链接可用，无账号登录要求。
- [ ] Age Rating 问卷按真实功能填写。
- [ ] App Privacy 选择 Data Not Collected 前已对最终商店二进制复核。
- [ ] Content Rights、Encryption、DSA、中国大陆可用性均已回答。

## 构建、上传与发布

- [ ] Release archive 由干净提交构建，Git SHA 已记录。
- [ ] `codesign --verify --deep --strict` 通过。
- [ ] `codesign -d --entitlements :-` 与 profile 权限一致。
- [ ] `spctl`、沙盒和最终 `.pkg` 本机验证通过。
- [ ] 使用 Xcode Organizer 或 Transporter 上传成功。
- [ ] 构建在 App Store Connect 处理完成，无 Missing Compliance。
- [ ] 内部 TestFlight 安装和完整功能回归通过。
- [ ] 构建已绑定到 macOS 1.0.0 版本。
- [ ] 选择 Manual Release，提交前最后检查物料和地区。
- [ ] Add for Review → Draft Submission → Submit for Review 已完成。
- [ ] 审核消息联系人保持可达；批准后手动发布并检查产品页。

