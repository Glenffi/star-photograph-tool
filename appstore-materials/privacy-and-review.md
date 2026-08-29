# 隐私、合规与 App Review 资料

## App Privacy 建议答案

仅在完成 `technical-readiness.md` 的 P0 项并使用专用 Mac App Store 构建后：

- Data Collection：`No, we do not collect data from this app`。
- Tracking：`No`。
- 账号：无账号、无登录。
- 广告与分析：无广告、无统计 SDK、无崩溃上报服务。
- 用户照片：由用户主动选择，只在本机读取、处理和导出，不上传服务器。
- 网络：商店构建移除自更新客户端；版本更新完全由 Mac App Store 提供。

如果以后接入云端 AI、遥测、崩溃上报、邮件表单或第三方分析，必须在发布对应
版本前重新填写 App Privacy，并同步修改隐私政策和 `PrivacyInfo.xcprivacy`。

## 隐私政策 URL

- 中文：`https://di.nexusgen.net/starprocessor/privacy/`
- English：`https://di.nexusgen.net/starprocessor/privacy/en/`
- 待发布页面源稿：`web/privacy/index.html`
- 发布前必须替换：`__LEGAL_NAME__`、`__PUBLIC_SUPPORT_EMAIL__`、
  `__LEGAL_OR_PUBLIC_CONTACT_ADDRESS__`。

## Support URL

- `https://di.nexusgen.net/starprocessor/support/`
- 待发布页面源稿：`web/support/index.html`
- Apple 要求支持页提供真实可达的联系信息；发布前填写邮箱、电话和地址。

## App Review Notes（英文，可直接粘贴）

Nebulens requires no account, login, subscription, external hardware, or network connection.

All RAW decoding, calibration, alignment, stacking, denoising, color adjustment, star treatment, and export are performed locally on the Mac. The submitted Mac App Store build does not include the direct-download update checker; updates are distributed only through the Mac App Store.

Suggested review workflow:
1. Launch Nebulens. The default interface is Simplified Chinese; choose More > Settings > Interface Language > English and restart if an English interface is preferred.
2. In MODE, choose Single Frame and import one supported RAW file, or choose Nightscape and import two or more RAW files.
3. Use the left workflow rail to inspect Assets, Align, Stack, Adjust, and Output.
4. For Nightscape, click Start Processing. The app selects a reference frame, aligns stars, stacks included frames, and shows the processed result.
5. Open Adjust to try denoising, exposure, color, sharpening, star defringe, and star reduction. Quick Preview updates after a short debounce.
6. Open Output and export a 16-bit TIFF or 8-bit PNG to a user-selected folder.
7. Deep Sky additionally accepts Dark, Flat, Bias, and Dark Flat RAW groups or Nebulens `.spmaster` files. These calibration files are optional for reviewing the Single Frame and Nightscape workflows.

A small review RAW set should be attached to App Review or provided through the Support URL. The reviewer may also use any LibRaw-supported Bayer RAW file. No demo credentials are required.

Sandbox behavior:
- Input and output locations are granted through standard macOS open/save panels.
- Persisted folders use app-scoped security-scoped bookmarks.
- The app does not request Full Disk Access, administrator privileges, accessibility access, or background launch permission.

## App Review 联系信息（必须人工填写）

- First name：`__REVIEW_FIRST_NAME__`
- Last name：`__REVIEW_LAST_NAME__`
- Email：`__REVIEW_EMAIL__`
- Phone（国际格式，例如 +86...）：`__REVIEW_PHONE__`
- Sign-in required：`No`
- Attachment：建议上传 2–3 张已确认版权的 RAW 样片，或附可公开下载的样片链接。

## 年龄分级建议

Nebulens 不包含用户生成内容发布、社交、聊天、赌博、暴力、成人内容、受控物品或
不受限网页浏览。按实际问卷逐项选择 `None/No`，预计得到最低年龄分级。不要为了
追求某个结果虚填；最终分级以 App Store Connect 当时的问卷计算为准。

## 内容版权

- App 本身不提供第三方媒体内容，只处理用户选择的文件。
- 商店截图中的银河照片必须由开发者拥有版权，或取得明确的商用宣传授权。
- 当前截图使用项目本地 `star-raw-2` 样片；上传前由素材所有者书面确认可用于商店宣传。
- Qt、LibRaw、libtiff 及传递依赖的许可证需要保留在 App 包资源和第三方声明中。

## 加密出口合规

当前直装版使用 HTTPS 自更新并可能封装 TLS 运行库；Mac App Store 版必须移除这条
更新链路后重新审计最终 App 包。然后在 App Store Connect 的 App Encryption
Documentation 问卷中按最终二进制如实回答。只有确认商店构建不包含非豁免加密时，
才可设置 `ITSAppUsesNonExemptEncryption = false`，本材料不替代出口合规法律判断。
