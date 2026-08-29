# Nebulens Mac App Store 上架全流程

更新时间：2026-08-30

本文按 Apple 当前页面整理首次上架 macOS App 的完整操作。Apple 会持续调整协议、
问卷和界面；真正提交当天，以本文链接的 Apple 官方页面和 App Store Connect 实际
提示为准。

## 0. 当前结论

建议商店身份：

| 项目 | 建议值 |
|---|---|
| 产品名 | Nebulens |
| 中文商店名 | Nebulens 星空影像工作台 |
| 英文商店名 | Nebulens: Astro RAW Studio |
| Bundle ID | `net.nexusgen.nebulens` |
| SKU | `NEBULENS-MAC-001` |
| 首发版本 | `1.0.0 (1)` |
| 平台 | macOS 14+，Apple silicon |
| 主分类 | Photo & Video |
| 副分类 | Utilities |
| 主语言 | Simplified Chinese (`zh-Hans`) |

**当前 DMG 不能上传。**它是 ad-hoc 签名的直装版，而且还没有 App Sandbox、
security-scoped bookmark 和 Mac App Store 专用更新策略。Apple 要求 Mac App Store
App 使用沙盒，并规定更新只能由 Mac App Store 分发；现有独立更新器必须从商店构建
移除。技术差距见 [`../appstore-materials/technical-readiness.md`](../appstore-materials/technical-readiness.md)。

## 1. 确定 Apple Developer 账号主体

入口：[Apple Developer Program Enrollment](https://developer.apple.com/programs/enroll/)

先决定使用个人还是组织账号，因为产品页显示的开发者名称和后续合同主体不同。

### 1.1 个人账号

- 使用本人的 Apple Account 和法定身份证明注册。
- 不需要 D-U-N-S。
- App Store 开发者名称通常显示个人法定姓名，不适合希望展示公司品牌的情况。

### 1.2 组织账号

- 必须是可承担合同义务的法律实体；品牌名、工作室别名或分支机构不能替代法律主体。
- 准备营业执照法律名称、注册地址、工作联系人和有签约权限的人员。
- 先在 Apple/D&B 页面查询 D-U-N-S；没有则免费申请。Apple 说明 D&B 通常最多约
  5 个工作日生成，随后还需等待信息同步。
- 中国大陆组织确保 D-U-N-S 中的中文公司名和统一社会信用代码准确。

官方说明：[D-U-N-S Number](https://developer.apple.com/help/account/membership/D-U-N-S/)。

### 1.3 注册操作

1. 打开 Enrollment 页面，登录开启双重认证的 Apple Account。
2. 选择 Individual 或 Organization。
3. 按页面要求填写身份、地址、电话和组织信息。
4. 完成 Apple 的身份/主体核验。
5. 阅读并接受 Apple Developer Program License Agreement。
6. 完成年费支付，等待 Membership 显示 Active。
7. 登录 [App Store Connect](https://appstoreconnect.apple.com/)，确认能看到
   `Apps`、`Business`、`Users and Access`。

## 2. 完成协议、税务和地区合规

### 2.1 免费还是付费

- 免费 App：加入 Apple Developer Program 并接受最新协议后即可分发免费 App。
- 付费 App 或 IAP：Account Holder 必须在 App Store Connect → Business →
  Agreements 签署 Paid Apps Agreement。
- 付费还要填写银行账户和税表。所有开发者都会遇到美国税表；中国等非美国主体通常
  按问卷进入 W-8BEN 或 W-8BEN-E，并可能还有本地表单。税表提交前逐字核对，部分
  字段提交后不能自行修改。

官方步骤：[Sign and update agreements](https://developer.apple.com/help/app-store-connect/manage-agreements/sign-and-update-agreements)、
[Provide tax information](https://developer.apple.com/help/app-store-connect/manage-tax-information/provide-tax-information/)。

建议首发前明确写入 `app-information.json` 的 `price`，不要在截图或描述中承诺尚未
确定的价格。

### 2.2 中国大陆开发者信息

App Store Connect → Business → Agreements → Compliance：

1. 打开 China Mainland，确认中国开发者身份信息。
2. 按页面提示完成《国务院令第 810 号》所需个人身份证信息，或组织 D-U-N-S/USCI。
3. 决定首发是否包含中国大陆地区。Apple 提醒部分 App 需要有效 ICP Filing Number，
   并要求商店元数据与工信部备案一致。
4. 如果尚未确认 ICP/App 备案要求，首发先取消 China mainland，可在合规完成后添加。

官方页面：[Mainland China compliance](https://developer.apple.com/help/app-store-connect/manage-compliance-information/view-mainland-china-compliance-information/)、
[State Council Decree No. 810](https://developer.apple.com/help/app-store-connect/manage-compliance-information/manage-information-for-state-council-decree-no-810)。

### 2.3 欧盟 DSA

即使不在欧盟分发，也要声明 trader 状态。App Store Connect → Business →
Agreements → Compliance → Digital Services Act：

1. 由账号持有人自行判断 Trader / Non-Trader；Apple 不替你做法律判断。
2. Trader 需要验证公开邮箱、电话和地址；这些信息会显示在欧盟产品页。
3. 完成账号级声明后，在 App → App Information → Digital Services Act 检查
   Nebulens 的 App 级状态。

官方页面：[EU DSA trader requirements](https://developer.apple.com/help/app-store-connect/manage-compliance-information/manage-european-union-digital-services-act-trader-requirements)。

## 3. 注册 Bundle ID（重点）

建议使用：`net.nexusgen.nebulens`

这是反向域名形式：你已有 `nexusgen.net`，所以前缀写成 `net.nexusgen`。使用全小写，
不要使用空格、中文或版本号。Bundle ID 是技术身份，不等于用户看到的 App 名称。

### 3.1 注册操作

1. 登录 [Certificates, Identifiers & Profiles](https://developer.apple.com/account/resources/identifiers/list)。
2. 左侧选择 `Identifiers`。
3. 点击左上角 `+`。
4. 选择 `App IDs`，点击 Continue。
5. 类型选择 `App`，点击 Continue。
6. Description 填 `Nebulens macOS`。
7. 选择 `Explicit App ID`，不要选择 Wildcard。
8. Bundle ID 填 `net.nexusgen.nebulens`。
9. Capabilities 只启用实际需要的能力。首版不需要 Push、CloudKit、Sign in with
   Apple、App Groups 或 In-App Purchase 服务时不要提前开启。
10. 点击 Continue，核对 Team 和 Bundle ID，再点 Register。

Apple 官方步骤：[Register an App ID](https://developer.apple.com/help/account/identifiers/register-an-app-id)。

### 3.2 注册失败怎么办

- 如果提示 Bundle ID 不可用，不要创建 App Store Connect 记录，也不要先上传构建。
- 先确认该 ID 是否已在同一 Team 注册；如果属于别的 Team，需要换成最终 ID，例如
  `net.nexusgen.nebulens.mac`，并一次性同步代码、profile 和物料。
- 一旦上传构建，App Store Connect 的 Bundle ID 不能修改；Apple ID、用户评价和
  后续版本都会绑定它。

## 4. 创建签名证书和 Provisioning Profile

### 4.1 在 Xcode 登录账号

1. 从 Mac App Store 安装当前稳定版 Xcode。
2. 首次启动，安装附加组件并接受许可证。
3. Xcode → Settings → Accounts → `+` → Apple Account。
4. 选择正确 Team，确认角色是 Account Holder/Admin 或具备构建权限。

### 4.2 创建证书

推荐由 Xcode 管理私钥：

1. Xcode → Settings → Accounts。
2. 选择 Team → Manage Certificates。
3. 点击 `+`，创建 `Mac App Distribution`。
4. 再创建 `Mac Installer Distribution`。
5. 打开 Keychain Access → My Certificates，确认每张证书下面都有私钥。

前者签 `.app`，后者签提交 Mac App Store 的 `.pkg`。证书类型说明见
[Certificates overview](https://developer.apple.com/help/account/create-certificates/certificates-overview)。

私钥只保存在安全 Keychain/加密备份中，不提交 Git，不放入公开 CI artifact。

### 4.3 Provisioning Profile

可优先使用 Xcode automatic signing。手动创建时：

1. Developer Account → Profiles → `+`。
2. Distribution 下选择 `Mac App Store Connect`。
3. 选择明确 App ID `net.nexusgen.nebulens`。
4. 选择 Mac App Distribution certificate。
5. Profile Name 填 `Nebulens Mac App Store`。
6. Generate → Download，双击安装或让 Xcode 下载。

官方步骤：[Create an App Store Connect provisioning profile](https://developer.apple.com/help/account/provisioning-profiles/create-an-app-store-provisioning-profile)。

## 5. 在 App Store Connect 创建 App 记录

在上传第一个构建前创建记录。Account Holder 必须先接受最新协议。

1. 打开 App Store Connect → Apps。
2. 点击左上角 `+` → New App。
3. Platforms 只勾 `macOS`。
4. Name 填主语言名称：`Nebulens 星空影像工作台`。
5. Primary Language 选 `Simplified Chinese`。
6. Bundle ID 选择刚注册的 `net.nexusgen.nebulens`。
7. SKU 填 `NEBULENS-MAC-001`。SKU 只供内部识别，创建后不能改。
8. User Access 选择 Full Access；多人团队有权限隔离需求时再选 Limited Access。
9. 点击 Create。

官方步骤：[Add a new app](https://developer.apple.com/help/app-store-connect/create-an-app-record/add-a-new-app)。

创建后记录：

- App Information 页面生成的 Apple ID；
- Team ID；
- 实际开发者展示名称；
- App Record 的 Bundle ID。

回填 `appstore-materials/app-information.json`。

## 6. 完成 Mac App Store 专用技术整改

按 [`technical-readiness.md`](../appstore-materials/technical-readiness.md) 完成 P0，核心要求：

1. 增加独立 `NEBULENS_APP_STORE_BUILD` 构建配置。
2. Bundle ID 使用 `net.nexusgen.nebulens`，直装版可继续保留旧 Bundle ID。
3. 启用 App Sandbox 和 `Config/Nebulens-AppStore.entitlements`。
4. macOS 商店构建恢复原生 Open/Save Panel，获得系统沙盒授权。
5. 对跨重启输出目录保存 app-scoped security-scoped bookmark。
6. 移除 UpdateManager、外部更新菜单、DMG 下载和外部更新 URL。
7. 不调用 `/usr/bin/open` 或其他包外程序。
8. 把许可证、翻译和 `PrivacyInfo.xcprivacy` 放进单一 App 包。
9. 首发版本改为 `1.0.0`，build 设为 `1`；每次上传 build 都必须递增。
10. 使用 macOS 14 兼容依赖构建 arm64 包，不使用本机 macOS 26 Homebrew 二进制冒充
    macOS 14 兼容。

Apple 说明 App Sandbox 是 Mac App Store 要求；用户通过系统 Open/Save Panel 选择的
文件可获得授权，跨启动访问则需要 security-scoped bookmark：
[App Sandbox](https://developer.apple.com/documentation/security/protecting-user-data-with-app-sandbox)、
[Accessing files from the macOS App Sandbox](https://developer.apple.com/documentation/security/accessing-files-from-the-macos-app-sandbox)。

App Review Guidelines 还明确要求 Mac App Store App 通过商店更新，不允许其他更新
机制：[App Review Guidelines](https://developer.apple.com/app-store/review/guidelines/)。

## 7. 隐私、许可证与加密

### 7.1 Privacy Manifest

仓库已提供 `resources/PrivacyInfo.xcprivacy`，当前声明：不追踪、不收集数据、无追踪
域名。macOS manifest 应位于 `.app/Contents/Resources/PrivacyInfo.xcprivacy`。

最终 Store 包生成后：

1. 在 Xcode Organizer 生成 Privacy Report，或检查所有 App/Framework manifest。
2. 确认 Qt 与所有动态库没有引入未声明的数据收集或 tracking domain。
3. App Store Connect → App Privacy → Get Started。
4. 只有最终二进制符合时，选择 `No, we do not collect data from this app`。
5. 填入已公开的隐私政策 URL。

官方页面：[Manage app privacy](https://developer.apple.com/help/app-store-connect/manage-app-information/manage-app-privacy)、
[Privacy manifest placement](https://developer.apple.com/documentation/bundleresources/adding-a-privacy-manifest-to-your-app-or-third-party-sdk)。

### 7.2 第三方许可证

- Qt 6：LGPLv3，动态链接并保留许可证/重新链接权利说明。
- LibRaw：LGPLv2.1/CDDL 双许可，按实际采用路径保留文本。
- libtiff：BSD-2-Clause。
- 检查随 Qt/LibRaw 引入的 JPEG、LCMS、zlib、OpenSSL 等传递依赖及许可证。

Mac App Store 只能提交单一 App 包，因此当前 DMG 根目录的 LICENSE/NOTICE 不够；必须
放进 `Nebulens.app/Contents/Resources`，并从 About 或帮助页可访问。

### 7.3 加密出口合规

App Store Connect → App Information → App Encryption Documentation，或在选择构建
时点击 Manage，按最终商店二进制回答。当前直装版含 HTTPS 更新链路，不能直接声明
不使用加密。移除更新器和不需要的 TLS 库后重新审计；仅在答案确认后写
`ITSAppUsesNonExemptEncryption`。

官方页面：[Overview of export compliance](https://developer.apple.com/help/app-store-connect/manage-app-information/overview-of-export-compliance)。

## 8. 准备并上传商店物料

### 8.1 中英文 Localization

主语言先填 `metadata/zh-Hans.json`：

1. App Information 填中文 Name、Subtitle。
2. macOS 1.0.0 页面填 Promotional Text、Description、Keywords、Support URL、
   Marketing URL、Copyright。
3. 在语言菜单添加 `English (U.S.)`。
4. 粘贴 `metadata/en-US.json` 对应字段。

Apple 当前限制：Name/Subtitle 最多 30 字符；Promotional Text 最多 170；Description
最多 4000；Keywords 最多 100 bytes。Support URL 必填并需要真实联系信息。
[字段说明](https://developer.apple.com/help/app-store-connect/reference/app-information/platform-version-information)。

### 8.2 Screenshot

1. 在 macOS 1.0.0 → App Previews and Screenshots 上传 `screenshots/zh-Hans` 3 张。
2. English localization 上传 `screenshots/en-US` 3 张。
3. 保持显示顺序：素材 → 堆栈 → 深空校准。
4. 当前文件为 `2880×1800` PNG、无 Alpha，符合 Apple Mac 截图规范。
5. 最终沙盒 1.0.0 包完成后复拍并覆盖，确保截图与提交版本一致。

Apple 允许每个语言 1–10 张 Mac 截图，接受 1280×800、1440×900、2560×1600、
2880×1800：[Screenshot specifications](https://developer.apple.com/help/app-store-connect/reference/app-information/screenshot-specifications/)。

### 8.3 URL

发布前把 `appstore-materials/web` 部署为：

- Marketing：`https://di.nexusgen.net/starprocessor/`
- Support：`https://di.nexusgen.net/starprocessor/support/`
- Privacy 中文：`https://di.nexusgen.net/starprocessor/privacy/`
- Privacy English：`https://di.nexusgen.net/starprocessor/privacy/en/`

从未登录浏览器和境外网络分别测试 HTTPS 200、移动端排版、邮箱/电话/地址。替换全部
`__PLACEHOLDER__` 后才可上线。

## 9. Archive、签名和上传

此步骤必须等技术 P0 完成并新增经过验证的 Store archive 脚本后执行。目标流程：

1. 使用干净 Git 提交和 macOS 14 兼容依赖生成 Release `.xcarchive`。
2. Xcode Organizer 选择 archive → Distribute App → App Store Connect → Upload。
3. 使用 Automatic Signing，或明确选择 Mac App Distribution certificate 和 profile。
4. Xcode 对所有嵌套 Framework/PlugIn 重新签名并验证 entitlements。
5. 如改走手动 `.pkg`，先用 Mac App Distribution 签 App，再用
   Mac Installer Distribution 执行 `productbuild`；不能上传 DMG。
6. 上传前本地执行 `codesign --verify --deep --strict`，并检查包内 Bundle ID、版本、
   build、profile、sandbox entitlement 和动态库闭包。

也可把正确签名的 `.pkg` 拖入 Transporter。Apple 支持 Xcode、Transporter 和上传
API：[Upload builds](https://developer.apple.com/help/app-store-connect/manage-builds/upload-builds/)。

## 10. App Store Connect 构建处理

1. 等待 Apple 处理构建并发送完成邮件。
2. TestFlight → macOS 查看构建状态。
3. 若出现 Missing Compliance，点击 Manage 回答 Encryption 问卷。
4. 检查警告：Bundle ID、entitlements、privacy manifest、icon、minimum OS、签名和
   nested code。
5. 先添加内部测试员，下载 App Store 分发的构建，而不是本地 archive。
6. 用 `release-checklist.md` 完成沙盒与六场景回归。

## 11. 填写 App Store Connect 剩余字段

### App Information

- Categories：Photo & Video / Utilities。
- Content Rights：App 不提供第三方内容；用户处理自己选择的照片。
- Age Rating：按真实功能逐项 None/No，预计最低分级。
- License Agreement：无特殊需求时使用 Apple Standard EULA。
- DSA、China Mainland、Korea/Vietnam 等地区合规按实际地区回答。

### App Privacy

- 发布隐私 URL。
- 最终 Store build 审计通过后选择 Data Not Collected。

### macOS 1.0.0

- Copyright：`2026 法律主体`，Apple 自动添加 ©。
- Build：点击 `+` 选择已处理的 1.0.0 build。
- Version Release：首版建议 `Manually release this version`，批准后再主动上线。
- App Review Information：填写姓名、邮箱、国际格式电话；无需账号。
- Notes：粘贴 `privacy-and-review.md` 的英文审核备注。
- Attachment：上传已授权的 2–3 张 RAW 样片或给出稳定样片链接。

App Review 联系人、说明、账号字段定义见
[Platform version information](https://developer.apple.com/help/app-store-connect/reference/app-information/platform-version-information)。

## 12. 提交审核

1. 确认版本页没有红色必填项。
2. Build 区域绑定正确构建。
3. 点击右上角 `Add for Review`。
4. 选择创建新的 Draft Submission。
5. 打开左侧 App Review / Draft Submissions，检查提交项目。
6. 点击 `Submit for Review`。
7. 状态依次可能为 Ready for Review → Waiting for Review → In Review。
8. 审核期间保持联系人可达；Apple 提问时在 Resolution Center 提供具体复现信息。

官方步骤：[Submit an app](https://developer.apple.com/help/app-store-connect/manage-submissions-to-app-review/submit-an-app)。

## 13. 批准与发布后

1. 状态到 Pending Developer Release 后做最后一次产品页和地区检查。
2. 点击 Release This Version。
3. 等待各地区传播，Apple 提醒完整可见可能需要最多约 24 小时。
4. 从真实 Mac App Store 下载并验证：首次启动、沙盒文件选择、处理、导出、语言、
   版本号、支持和隐私链接。
5. 保存 archive、dSYM、Git tag、build number、提交物料和审核通信。
6. 后续每次更新递增 build；版本变更写具体 What's New；继续只通过 Mac App Store
   更新商店版。

## 14. 本次需要你提供的信息

要继续实际建档，需要账号持有人确认：

1. 个人还是组织 Apple Developer 账号；
2. Apple Team ID 与法律主体显示名；
3. `net.nexusgen.nebulens` 是否注册成功；
4. 免费还是付费、首发地区；
5. 公开支持邮箱、电话、地址；
6. App Review 联系人；
7. `star-raw-2` 样片是否拥有 App Store 宣传授权；
8. 是否首发中国大陆与欧盟。

这些信息不要在公开 issue 或源码里提交身份证号、银行账号、税号、证书私钥或 Apple
密码。Team ID、Bundle ID 和公开联系信息可以进入发布配置；敏感信息只留在 Apple
后台和安全 Keychain。
