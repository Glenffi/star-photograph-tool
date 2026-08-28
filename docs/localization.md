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
其他标签不应使用未纳入目录的裸字符串。添加或修改文本后运行：

```bash
$(brew --prefix qt@6)/bin/lupdate src \
  -ts translations/nebulens_en.ts \
  -source-language zh_CN -target-language en_US -no-obsolete

$(brew --prefix qt@6)/bin/lrelease translations/nebulens_en.ts \
  -qm resources/i18n/nebulens_en.qm
```

翻译必须保留 `%1`、`%2`、`%n` 等占位符及必要的 HTML 标签。CTest 的
`localization_catalog` 会拒绝未完成、空白、残留中文或占位符不一致的英文翻译；
`localization_dialogs` 会实际切换中英文并检查 Qt 文件对话框标准按钮。

## 视觉检查

开发截图可临时覆盖语言而不修改用户设置：

```bash
./build/StarProcessor.app/Contents/MacOS/StarProcessor \
  --language=en_US --workflow=adjust --window-size=920x540 \
  --screenshot=/tmp/nebulens-en.png
```

对应中文参数为 `--language=zh_CN`。两种语言都应检查 920×540 最小窗口与
1440×900 标准窗口，重点观察顶部品牌区、工作流名称、检查器标题、参数标签和数值列。
