# Nebulens 客户端打包

本文说明 Nebulens macOS 和 Windows 客户端的可重复构建、验证与发布流程。为兼容 v0.11.0 更新协议，发行文件名暂时保留 `StarProcessor` 前缀；安装后的显示名称、DMG 卷名和 Release 标题使用 `Nebulens`。

## 发行物

| 平台 | 文件 | 架构 | 形式 |
|---|---|---|---|
| macOS | `StarProcessor-v<version>-macOS-<arch>.dmg` | 当前构建机架构 | 自包含 `.app` + Applications 快捷方式 |
| Windows | `StarProcessor-Windows-x64-v<version>.zip` | x64 | 便携目录，包含 EXE、Qt 插件和第三方 DLL |

两个平台都生成同名 `.sha256` 文件。版本号来自 `CMakeLists.txt`，不在打包脚本中重复维护。

## macOS

依赖：CMake、Qt 6、LibRaw、libtiff 和供回归测试使用的 Python 3。使用 Homebrew 安装后可生成本机测试包：

```bash
./scripts/package-macos.sh
```

脚本依次执行：

1. 以 Release、macOS 14 deployment target 配置并编译。
2. 运行本次打包配置启用的 CTest，包括冲印单流程和无覆盖交付。
3. 用 `macdeployqt` 复制 Qt、LibRaw 和 libtiff，仅保留 Cocoa 平台、macOS 样式与 JPEG 三个实际使用的 Qt 插件；另复制 `qtbase_zh_CN.qm`，让非原生 Qt 文件对话框跟随应用语言。
4. 递归扫描所有 Mach-O 的 `@rpath` 依赖，从 vcpkg/Homebrew 运行库目录补齐非 Qt 传递依赖；随后逐项解析 `@rpath`、`@loader_path` 和 `@executable_path`，禁止缺失文件及残留 Homebrew 或用户目录依赖。
5. 扫描 bundle 内全部 Mach-O，读取依赖真实的最低系统版本并回写 `Info.plist`。
6. 在路径改写全部结束后进行 ad-hoc 深度签名并验证 bundle seal。
7. 无条件执行 `--runtime-check`，让 dyld 加载打包后的完整动态库树；有图形会话时再启动程序并生成截图，确认 Qt 平台插件可加载。
8. 创建压缩 DMG 和 SHA-256 文件。

设置 `SKIP_LAUNCH_TEST=1` 只会跳过需要图形会话的截图检查，不会跳过 dyld 运行时自检；设置 `SKIP_DMG_CREATE=1` 可只生成完成部署和签名的 staging 目录。当本机依赖高于目标系统版本时脚本默认中止；`ALLOW_HIGHER_DEPLOYMENT_TARGET=1` 只允许用于本地兼容性验证，不得用于公开发布。当前包没有 Apple Developer ID 签名和 notarization；这不影响本机测试，但不等同于面向公众的正式签名发行。

> 源码目标是 macOS 14，但客户端最终兼容范围由打包机上的预编译依赖共同决定。脚本会取 bundle 内所有 Mach-O 的最高最低版本；默认要求它不高于目标版本，否则立即失败。显式允许本地兼容性构建时，真实要求会写入 `LSMinimumSystemVersion` 和 `BUILD-INFO.txt`。正式 arm64 包由 `.github/workflows/macos-package.yml` 在 macOS 15 arm64 runner 上以 macOS 14.0 为目标构建，Qt 固定为 6.8.3，LibRaw/libtiff 使用项目内 `arm64-osx-dynamic` vcpkg triplet 从源码构建；只要任一 bundle 依赖高于 14.0，工作流就会失败而不是发布错误标记的包。

## Windows

`.github/workflows/windows-package.yml` 使用 Windows Server 2022、MSVC 2022 x64、Qt 6.8.3 和 vcpkg 动态库。推送 `main` 时先构建并保留 Actions artifact；推送 `v*` 标签时再次验证并发布 GitHub Release。工作流会：

1. 安装 `libraw:x64-windows` 和 `tiff:x64-windows`。
2. 配置 Release 构建并运行 CTest。
3. 用 `windeployqt` 部署 Qt、平台插件和 MSVC runtime。
4. 复制 vcpkg 动态依赖并确认 `platforms/qwindows.dll` 存在。
5. 从最终便携目录依次执行 `--runtime-check` 和 `--platform-runtime-check` 并检查退出码：前者验证 EXE 的静态 DLL 闭包，后者初始化 `QApplication`，实际加载 `qwindows.dll` 及其传递依赖。Windows GUI 子系统不依赖控制台标准输出。
6. 创建便携 ZIP、SHA-256 文件和 GitHub Actions artifact。
7. 当触发来源是 `v*` 标签时，校验标签与 CMake 版本一致并发布 GitHub Release 资产。

Windows 包必须由 Windows runner 验证，macOS 上不使用 MinGW 交叉编译替代 MSVC 发行结果。

## 发布前核对

- macOS 和 Windows 的打包配置均启用 `BUILD_TESTING`，关闭 `BUILD_SAMPLE_TOOLS`，因此不会运行依赖样片工具的 `timelapse_cli`。发版前另用 `-DBUILD_SAMPLE_TOOLS=ON` 构建，按 [README 的冲印单定向测试](../README.md#冲印单定向测试) 运行 `print_order_flow`、`output_delivery` 和 `timelapse_cli`。
- 在各平台启动打包后的应用，确认默认进入冲印单。用 RAW 保存一张成片和一组延时照片，核对回执中的张数、实际路径和「打开目录」的去向。核对取消不显示成功回执，并检查「静止」、系统减弱动态效果和高级暗房的返回入口。离屏回归不能代替这一步。

## 发布标签

确认主分支干净、Release 构建通过后：

```bash
tag=v0.14.0
git tag -a "$tag" -m "发布：Nebulens $tag" &&
  git push origin "refs/tags/$tag"
```

标签推送后查看 GitHub Actions 的 `Package Windows` 与 `Package macOS` 任务。成功后 Release 页面会同时出现 Windows ZIP、macOS DMG 和校验文件。当前 macOS 包仍为 ad-hoc 签名；获得 Apple Developer 证书后再接入 Developer ID 签名与公证。

两端安装包都准备好后，再按 [`update-distribution.md`](update-distribution.md) 发布更新清单。更新服务器只保留清单指向的当前版本，不作为历史 Release 归档；历史版本仍由 GitHub Releases 承担。
