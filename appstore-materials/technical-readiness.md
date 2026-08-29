# Mac App Store 技术就绪评估

结论：当前 `v0.13.0` 直装版功能完整，但不是 Mac App Store 可上传二进制。以下 P0
必须完成并用真实分发证书重新验证。

| 优先级 | 项目 | 当前状态 | 完成标准 |
|---|---|---|---|
| P0 | App Sandbox | 未启用 | 商店构建使用 `Config/Nebulens-AppStore.entitlements`，Activity Monitor 显示已沙盒化 |
| P0 | 用户文件授权 | Qt 非原生文件面板 | 商店构建使用 macOS 原生 Open/Save Panel，选中的 RAW、蒙版、Master 和目录能获得沙盒授权 |
| P0 | 跨重启目录权限 | 只保存普通路径 | 输出目录和需要跨启动复用的路径保存、恢复 app-scoped security-scoped bookmark，并处理 stale bookmark |
| P0 | 默认输出目录 | 直接写 `~/Nebulens/Output` | 首次要求用户选择输出目录，或默认写沙盒容器；未授权时不扫描用户主目录 |
| P0 | 独立更新器 | 自动访问外部更新清单并下载 DMG | Mac App Store 构建完全移除 UpdateManager、检查更新菜单和下载代码，只由商店更新 |
| P0 | 外部进程 | `QProcess` 调用 `/usr/bin/open`，语言切换会启动新进程 | 商店构建改用允许的 AppKit/LaunchServices API；不执行 App 包外程序 |
| P0 | 签名与上传 | ad-hoc 签名 DMG | 使用 Mac App Distribution 签 `.app`、Mac Installer Distribution 签 `.pkg`，并匹配明确 App ID 与 profile |
| P0 | Bundle ID | 直装版为 `com.starprocessor.StarProcessor` | 商店构建改为已注册的 `net.nexusgen.nebulens`，App Store Connect 记录与包内完全一致 |
| P0 | 版本号 | 开发版本 `0.13.0` | 首发包改为 `CFBundleShortVersionString=1.0.0`、唯一递增 `CFBundleVersion=1` |
| P1 | 许可证 | DMG 根目录有声明 | LGPL/BSD/CDDL 许可证和 notices 放入 `.app/Contents/Resources`，About/帮助可访问 |
| P1 | Privacy Manifest | 已加入空数据 manifest | 对最终商店二进制和所有动态库生成隐私报告，声明与 App Privacy 一致 |
| P1 | 加密出口 | 未完成最终审计 | 商店构建移除网络更新和不需要的 TLS 运行库后回答 App Encryption 问卷 |
| P1 | 审核数据 | 无内置演示素材 | 附 2–3 张有宣传/审核授权的 RAW，或提供稳定 HTTPS 样片链接 |
| P1 | 沙盒回归 | 未建立 | 输入、拖放、Master、临时缓存、历史结果、导出、重启授权和中英文流程全部通过 |
| P2 | Intel Mac | 当前仅 arm64 | 首发明确 Apple silicon；如需 Intel，再建立 x86_64/Universal 构建和样片回归 |

## 特别说明

Apple 要求 Mac App Store App 使用沙盒，并规定商店版只能通过 Mac App Store 更新。
因此不能仅给当前 `.app` 补一个签名；文件授权、路径持久化、更新入口和打包方式都要
形成独立的 Store 构建配置。

沙盒文件方案建议：

1. 当前会话中的 RAW 和校准素材由原生 Open Panel 授权，处理任务结束后释放访问。
2. 输出目录在首次导出时由用户选择，并保存 app-scoped security-scoped bookmark。
3. 启动时解析 bookmark，若 stale 则重建；解析失败时要求重新选择，不静默回退到
   用户主目录。
4. 最近结果只扫描已授权的输出目录；移除授权时同步清理历史列表。
5. 临时处理缓存继续使用沙盒允许的临时目录，任务完成/失败/取消都清理。

