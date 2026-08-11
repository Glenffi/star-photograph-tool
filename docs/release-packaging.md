# StarProcessor 客户端打包

本文说明 macOS 和 Windows 客户端的可重复构建、验证与发布流程。

## 发行物

| 平台 | 文件 | 架构 | 形式 |
|---|---|---|---|
| macOS | `StarProcessor-v<version>-macOS-<arch>.dmg` | 当前构建机架构 | 自包含 `.app` + Applications 快捷方式 |
| Windows | `StarProcessor-Windows-x64-v<version>.zip` | x64 | 便携目录，包含 EXE、Qt 插件和第三方 DLL |

两个平台都生成同名 `.sha256` 文件。版本号来自 `CMakeLists.txt`，不在打包脚本中重复维护。

## macOS

依赖：CMake、Qt 6、LibRaw 和 libtiff。使用 Homebrew 安装后运行：

```bash
./scripts/package-macos.sh
```

脚本依次执行：

1. 以 Release、macOS 14 deployment target 配置并编译。
2. 运行全部 CTest。
3. 用 `macdeployqt` 复制 Qt、LibRaw、libtiff 和传递依赖，仅保留 Cocoa 平台、macOS 样式与 JPEG 三个实际使用的 Qt 插件。
4. 审计所有 Mach-O 文件，禁止残留 Homebrew 或用户目录依赖。
5. 扫描 bundle 内全部 Mach-O，读取依赖真实的最低系统版本并回写 `Info.plist`。
6. 在路径改写全部结束后进行 ad-hoc 深度签名并验证 bundle seal。
7. 启动打包后的程序并生成截图，确认 Qt 平台插件可加载。
8. 创建压缩 DMG 和 SHA-256 文件。

设置 `SKIP_LAUNCH_TEST=1` 可在没有图形会话的 CI 中跳过启动测试；设置 `SKIP_DMG_CREATE=1` 可只生成完成部署和签名的 staging 目录。当前包没有 Apple Developer ID 签名和 notarization；这不影响本机测试，但不等同于面向公众的正式签名发行。

> 源码目标是 macOS 14，但客户端最终兼容范围由打包机上的预编译依赖共同决定。脚本会取 bundle 内所有 Mach-O 的最高最低版本，写入 `LSMinimumSystemVersion` 和 `BUILD-INFO.txt`。例如当前开发机的 Homebrew Qt 插件、LibRaw 和 libtiff 是面向 macOS 26 构建的，因此本机产出的 DMG 真实最低版本是 macOS 26。要发布兼容 macOS 14 的正式包，需要在 macOS 14 SDK/runner 上使用同样面向 14 构建的依赖。

## Windows

`.github/workflows/windows-package.yml` 使用 Windows Server 2022、MSVC 2022 x64、Qt 6.8.3 和 vcpkg 动态库。推送 `main` 时先构建并保留 Actions artifact；推送 `v*` 标签时再次验证并发布 GitHub Release。工作流会：

1. 安装 `libraw:x64-windows` 和 `tiff:x64-windows`。
2. 配置 Release 构建并运行 CTest。
3. 用 `windeployqt` 部署 Qt、平台插件和 MSVC runtime。
4. 复制 vcpkg 动态依赖并确认 `platforms/qwindows.dll` 存在。
5. 创建便携 ZIP、SHA-256 文件和 GitHub Actions artifact。
6. 当触发来源是 `v*` 标签时，校验标签与 CMake 版本一致并发布 GitHub Release 资产。

Windows 包必须由 Windows runner 验证，macOS 上不使用 MinGW 交叉编译替代 MSVC 发行结果。

## 发布标签

确认主分支干净、Release 构建通过后：

```bash
git tag -a v0.7.0 -m "StarProcessor v0.7.0"
git push origin v0.7.0
```

标签推送后查看 GitHub Actions 的 `Package Windows` 任务。成功后 Release 页面会出现 Windows ZIP 和校验文件。macOS DMG 在本机生成并完成启动验证后交付；获得 Apple Developer 证书后再接入签名、公证和 Release 自动上传。
