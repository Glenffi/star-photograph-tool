# Nebulens 客户端打包

本文说明 Nebulens macOS 和 Windows 客户端的可重复构建、验证与发布流程。为兼容 v0.11.0 更新协议，发行文件名暂时保留 `StarProcessor` 前缀；安装后的显示名称、DMG 卷名和 Release 标题使用 `Nebulens`。

## 发行物

| 平台 | 文件 | 架构 | 形式 |
|---|---|---|---|
| macOS | `StarProcessor-v<version>-macOS-<arch>.dmg` | 当前构建机架构 | 自包含 `.app` + Applications 快捷方式 |
| Windows | `StarProcessor-Windows-x64-v<version>.zip` | x64 | 便携目录，包含 EXE、Qt 插件和第三方 DLL |

两个平台都生成同名 `.sha256` 文件。版本号来自 `CMakeLists.txt`，不在打包脚本中重复维护。

默认在本机编译、测试、打包。仓库的两个 Actions 打包任务不监听推送、标签或 PR；只有用户明确同意使用在线额度时才手动启用并运行。构建失败也不自动重跑。

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

> 源码目标是 macOS 14，但实际兼容范围由随包依赖决定。脚本会取所有 Mach-O 最低版本中的最大值；只有本地兼容性构建才允许它高于目标，并须如实写入 `LSMinimumSystemVersion` 和 `BUILD-INFO.txt`。构建公开 arm64 包可参考 `.github/workflows/macos-package.yml` 的依赖配置：Qt 6.8.3、项目内 `arm64-osx-dynamic` vcpkg triplet，以及从源码构建的 LibRaw/libtiff；这不要求启动在线任务。

## Windows

在 Windows 的 VS 2022 x64 开发者 PowerShell 下，配置好 `VCPKG_ROOT`、`CMAKE_PREFIX_PATH`（指向 `msvc2022_64` Qt 安装目录）并安装 Python 3 后，可以直接在本机验证：

```powershell
& "$env:VCPKG_ROOT/vcpkg.exe" install libraw:x64-windows tiff:x64-windows
cmake -S . -B build/windows -G "Visual Studio 17 2022" -A x64 "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows -DBUILD_TESTING=ON -DBUILD_SAMPLE_TOOLS=ON
cmake --build build/windows --config Release --parallel
$env:PATH = "$env:CMAKE_PREFIX_PATH/bin;$env:VCPKG_ROOT/installed/x64-windows/bin;$env:PATH"
ctest --test-dir build/windows -C Release --output-on-failure
```

便携包的完整依赖复制与校验步骤见 `.github/workflows/windows-package.yml`，相同步骤可在本机执行：

1. 安装 `libraw:x64-windows` 和 `tiff:x64-windows`。
2. 配置 Release 构建并运行 CTest。
3. 用 `windeployqt` 部署 Qt、平台插件和 MSVC runtime。
4. 复制 vcpkg 动态依赖并确认 `platforms/qwindows.dll` 存在。
5. 从最终便携目录依次执行 `--runtime-check` 和 `--platform-runtime-check` 并检查退出码：前者验证 EXE 的静态 DLL 闭包，后者初始化 `QApplication`，实际加载 `qwindows.dll` 及其传递依赖。Windows GUI 子系统不依赖控制台标准输出。
6. 创建便携 ZIP 和 SHA-256 文件。在线任务只有在手动选择发行标签时才会校验标签并上传 Release 资产。

Windows 包应在 Windows 上用 MSVC 验证；macOS 的构建不能代替 Windows 验收。

## 发布前核对

- 两端打包配置均启用 `BUILD_TESTING` 和 `BUILD_SAMPLE_TOOLS`。全套 CTest 包含 `print_order_flow`、`output_delivery` 和 `timelapse_cli`；Windows 另有 `timelapse_windows_volume`。见 [冲印单定向测试](../README.md#冲印单定向测试)。
- 在各平台启动打包后的应用，确认默认进入冲印单。用 RAW 保存一张成片和一组延时照片，核对回执中的张数、实际路径和「打开目录」的去向。核对取消不显示成功回执，并检查「静止」、系统减弱动态效果和高级暗房的返回入口。离屏回归不能代替这一步。

## 发布标签

确认主分支干净、Release 构建通过后：

```bash
tag=v0.15.0
git tag -a "$tag" -m "发布：Nebulens $tag" &&
  git push origin "refs/tags/$tag"
```

标签仅记录版本，不启动构建。仅在明确授权发布后，把两端本地验证过的 ZIP、DMG 和校验文件上传到对应 Release。当前 macOS 包仍为 ad-hoc 签名，未做 Developer ID 签名与公证。

两端安装包都准备好后，再按 [`update-distribution.md`](update-distribution.md) 发布更新清单。更新服务器只保留清单指向的当前版本，不作为历史 Release 归档；历史版本仍由 GitHub Releases 承担。
