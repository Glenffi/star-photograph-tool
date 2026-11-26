# Nebulens 本地化维护

Nebulens 以简体中文作为源码语言，并提供完整的英文翻译目录。旧用户和没有保存
语言偏好的新安装默认使用简体中文；用户可在“设置 → 界面语言”选择
`简体中文` 或 `English`。切换后应用提示重启，避免运行中的项目、预览和后台
任务同时持有两套控件文本。

## 文件与运行时

- `src/i18n/AppLocalization.*`：读取 `ui/language`、安装应用及 Qt 翻译器、重启应用。
- `translations/nebulens_en.ts`：可审阅的英文翻译源，共覆盖菜单、参数、状态、错误和更新流程。
- `resources/i18n/nebulens_en.qm`：由 Qt `lrelease` 生成并随应用资源发布。
- `resources/translations.qrc`：把英文应用翻译嵌入可执行文件。
- `qtbase_zh_CN.qm`：打包时从当前 Qt 安装复制，只负责 Qt 文件对话框的标准按钮。

应用文件选择器统一使用 Qt Widgets 实现，不再混用 macOS 原生面板语言。因此应用
选择中文时，标题、过滤器、目录和“打开/取消”等标准控件都保持中文；英文模式则
全部显示英文。

## 新增界面文本

用户可见文本使用统一翻译上下文：

```cpp
QCoreApplication::translate("Nebulens", "选择 RAW 文件")
```

品牌名、RAW、RGB、TIFF、Light、Dark、Flat、Bias 和算法专有名词可以保留英文；
其他用户可见的标签、提示和后台错误都应纳入目录。添加或移动界面文本后，先从
`src` 提取到临时目录，避免把测试字符串或无关的行号变化写进正式 TS：

```bash
qt_bin="$(brew --prefix qt@6)/bin"
extract_dir="$(mktemp -d)"
"$qt_bin/lupdate" src -ts "$extract_dir/nebulens_en.ts" \
  -source-language zh_CN -target-language en_US -no-obsolete -locations absolute
```

按翻译上下文、原文和消歧注释（如有）对照正式目录，只合入缺少的消息或有意修改的译文。
原文的空格和换行是翻译键的一部分；相邻 C++ 字面量应使用提取后的完整原文。
版本号等可变内容通过占位符插入，不要把版本宏拼进翻译键。
搬动主窗口代码不会改变 `Nebulens` 上下文，不需要刷新既有消息的文件和行号。
保留兼容条目，例如旧 `Toolbar` 上下文的“选择”和历史路径 `/星炼/Output`。

翻译须保留 `%1`、`%2`、`%n` 等占位符及必要的 HTML 标签。修改完成后重新生成
应用实际加载的 QM，并运行本地化测试：

```bash
"$qt_bin/lrelease" translations/nebulens_en.ts \
  -qm resources/i18n/nebulens_en.qm
cmake --build build --target NebulensLocalizationTests NebulensLocalizationDialogTests
ctest --test-dir build --output-on-failure -R '^localization_(catalog|dialogs)$'
```

`localization_catalog` 会拒绝未完成、空白、残留中文（历史路径除外）或占位符
不一致的译文，并逐条检查 QM 与 TS 一致。回归样例覆盖冲印三步、回执和目录、纸面
光影及磁盘空间和裁切错误。`localization_dialogs` 会切换语言并检查 Qt 文件对话框
的标题和标准按钮。

## 视觉检查

开发截图可临时覆盖语言而不修改用户设置：

```bash
./build/StarProcessor.app/Contents/MacOS/StarProcessor \
  --language=en_US --workflow=adjust --window-size=920x540 \
  --screenshot=/tmp/nebulens-en.png
```

对应中文参数为 `--language=zh_CN`。两种语言都应检查 920×540 最小窗口与
1440×900 标准窗口。去掉 `--workflow=adjust` 从冲印单开始，依次检查三步菜单、
冲印回执和实际保存位置、纸面光影选项与减弱动态效果提示，以及失败时的重试文案。
进入高级工作台后，重点观察工作流名称、检查器标题、参数标签和数值列。
