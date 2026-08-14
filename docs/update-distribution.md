# StarProcessor 客户端更新与服务器发布

本文描述 v0.9.0 起的更新协议、客户端安全边界和小容量服务器发布流程。

## 用户流程

应用启动 2.5 秒后会静默检查一次更新，同一台电脑 24 小时内不重复自动检查。用户也可从“帮助 → 检查更新”立即检查。检查和下载由 Qt 网络层执行，显式启用 Windows/macOS 系统代理与 PAC 配置，应用不单独保存代理凭据。

发现新版本后，应用展示版本号、说明和安装包大小。用户确认后，安装包以流式方式写入系统“下载”目录，同时计算 SHA-256。只有 HTTPS 地址、最终下载地址、字节数和 SHA-256 全部通过校验，临时文件才会被提交；随后由用户决定是否打开安装包。应用不会静默安装，也不会执行未校验文件。

Windows 的杀毒软件或文件索引器可能在下载刚结束时短暂保留临时文件句柄。客户端会先重试原子改名；仍失败时，复制到不覆盖已有文件的唯一名称，并对落盘结果重新校验字节数和 SHA-256。两种方式都失败时，对话框会显示目标路径和底层文件错误。

公开下载页位于 `https://di.nexusgen.net/starprocessor/`。页面从同目录的 `update.json` 读取当前版本、更新说明、平台安装包、文件大小与下载地址，因此发布客户端后不需要同步修改 HTML。页面源码位于 `deploy/web/starprocessor`，独立部署命令为：

```bash
./scripts/publish-download-page.sh
```

当前平台键：

| 平台键 | 安装包 |
|---|---|
| `windows-x64` | Windows x64 便携 ZIP |
| `macos-arm64` | Apple Silicon DMG |
| `macos-x64` | Intel Mac DMG（有对应构建时才发布） |

## 清单协议

固定入口：`https://di.nexusgen.net/starprocessor/update.json`

```json
{
  "schemaVersion": 1,
  "version": "0.9.0",
  "publishedAt": "2026-08-11T10:30:00Z",
  "releaseNotes": "新增安全的客户端更新检查。",
  "platforms": {
    "windows-x64": {
      "fileName": "StarProcessor-Windows-x64-v0.9.0.zip",
      "url": "https://di.nexusgen.net/starprocessor/downloads/StarProcessor-Windows-x64-v0.9.0.zip",
      "sha256": "64 个十六进制字符",
      "size": 12345678
    }
  }
}
```

客户端还会限制下载链接必须与清单同为 `di.nexusgen.net`，且路径位于 `/starprocessor/downloads/`。SHA-256 主要防止传输损坏和服务器误配；HTTPS 证书仍是清单发布者身份的信任根。后续若更新面向公开用户，应再引入离线私钥签名的清单，并为 macOS 包完成 Developer ID 签名和公证。

## 首次服务器配置

服务器复用现有 Nginx 与 `di.nexusgen.net` 证书，不启动额外服务。将 [`deploy/nginx/starprocessor-updates.conf`](../deploy/nginx/starprocessor-updates.conf) 的下载页、静态资源、清单和安装包 `location` 放入该域名的 HTTPS `server` 块，并在重载前执行：

```bash
nginx -t
nginx -s reload
```

内容目录是 `/www/wwwroot/starprocessor`。清单禁止缓存，版本化安装包允许短期缓存。目录不开放列表浏览，也只允许 GET/HEAD。

## 发布当前版本

先在对应平台完成 Release 构建和测试，再运行：

```bash
./scripts/publish-update.sh 0.9.0 \
  windows-x64=dist/StarProcessor-Windows-x64-v0.9.0.zip \
  macos-arm64=dist/StarProcessor-v0.9.0-macOS-arm64.dmg \
  -- "新增客户端更新检查；修复 Windows 中文路径 RAW 导入。"
```

脚本使用 SSH agent 或交互密码，不保存服务器密码。它会在本地按安装包真实内容生成清单；远端先接收 `.uploading` 临时文件，完整安装包就位后才原子替换 `update.json`，最后删除不在当前清单中的旧 ZIP/DMG。服务器因此只保留一个当前版本。

发布后至少验证：

```bash
curl -fsS https://di.nexusgen.net/starprocessor/update.json
curl -I https://di.nexusgen.net/starprocessor/downloads/StarProcessor-Windows-x64-v0.9.0.zip
```

再用上一版客户端执行“检查更新”，完整下载一次并确认应用报告 SHA-256 校验通过。
