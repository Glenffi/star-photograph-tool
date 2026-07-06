# StarProcessor — 星空摄影师 RAW 处理工具

跨平台 RAW 图像处理软件，专注于星空摄影领域。

## 核心功能

- 多张 RAW 图片叠加堆栈降噪
- 延时图片序列降噪（输出图片序列）
- 自动优化（去雾、曲线调整）
- 自动缩星（纯开源方案）
- AI 自动建议（云端方案）
- 星轨图片处理

## 技术栈

| 组件 | 技术 | License |
|------|------|---------|
| UI | Qt 6 | LGPLv3 |
| RAW 解码 | LibRaw | LGPLv2.1/CDDL |
| 图像处理 | OpenCV | Apache-2.0 |
| 构建 | CMake | — |
| AI 云端 | FastAPI + Docker | MIT/BSD |

## 构建

### macOS

```bash
brew install cmake qt@6
export CMAKE_PREFIX_PATH=$(brew --prefix qt@6)
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
./StarProcessor
```

### Windows

```powershell
# 使用 vcpkg 安装 Qt6
vcpkg install qtbase qtdeclarative

mkdir build; cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake -G "Visual Studio 17 2022"
cmake --build . --config Release
.\Release\StarProcessor.exe
```

## License

[MIT License](LICENSE)
