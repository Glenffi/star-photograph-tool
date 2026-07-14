# LibRaw 内置高质量 Demosaic 算法研究报告

**日期**: 2026-07-14  
**研究目标**: 解决 `decodeToRgb()` 中简单最近邻插值导致的画质问题，探索使用 LibRaw 内置 demosaic 算法的方案。

---

## 1. 现状分析

当前 `RawImageLoader::loadRaw()` 手动从 `processor.imgdata.rawdata.raw_image` 提取 Bayer CFA 数据，并自行做 black level 校正和归一化。`decodeToRgb()` 使用极其简单的邻域平均插值：R/B 位置取对角线同色像素平均，G 位置取上下左右 G 像素平均。这本质上是 bilinear 插值，会产生严重的 zipper artifact、伪色和边缘模糊，是画质瓶颈。

---

## 2. LibRaw 内置 Demosaic 选项

LibRaw 的 `dcraw_process()` 封装了完整的 dcraw 后处理管线，其中 demosaic 算法通过 `imgdata.params.user_qual` 控制：

| `user_qual` | 算法 | 说明 |
|-------------|------|------|
| 0 | Linear (Bilinear) | 最快，质量最差 |
| 1 | VNG (Variable Number of Gradients) | 梯度自适应，减少边缘伪色 |
| 2 | PPG (Patterned Pixel Grouping) | 比 VNG 快，细节保留好 |
| 3 | **AHD** (Adaptive Homogeneity-Directed) | **默认推荐**，综合质量高，平衡速度与画质 |
| 4 | DCB | 质量极高，较慢，可迭代优化 |
| 11 | DHT | 高质量，细节保留好 |
| 12 | AAHD (Modified AHD) | AHD 改进版， Anton Petrusevich 优化 |

其他关键参数：
- `use_camera_wb = 1`：使用相机记录的拍摄白平衡（`-w`）。
- `use_camera_matrix = 1`：使用嵌入的相机色彩矩阵（`+M`），默认行为。
- `output_bps = 16`：输出 16-bit 数据（`-4`）。
- `no_auto_bright = 1`：关闭自动亮度拉伸（`-W`），避免破坏线性数据。
- `output_color = 0`：输出原始相机线性空间（`-o 0`），便于后续处理。
- `gamm[0] = 1.0; gamm[1] = 1.0`：关闭 gamma 曲线，保持线性。

---

## 3. 方案对比

### Option A: 使用 `dcraw_process()` 获取高质量 RGB，再拷贝到 `ImageData`
- **优点**: 直接获得 LibRaw 处理后的 16-bit RGB，包含 demosaic + WB + 色彩矩阵 + 线性化，代码改动最小。
- **缺点**: `dcraw_process()` 同时做了 white balance 和色彩空间转换，如果应用层希望自己做 WB 或需要原始 Bayer 数据做其他处理，则不够灵活。

### Option B: 保留当前 Bayer 提取，引入外部 demosaic 库（如 OpenCV `cv::demosaicing`）
- **优点**: 保留现有 Bayer 数据流，可独立控制 demosaic 步骤。
- **缺点**: OpenCV 的 demosaic 算法选择有限（主要是 bilinear 和 VNG），且不如 LibRaw 内置的 AHD/DCB 成熟；引入新依赖增加维护成本。

### Option C: 使用 `dcraw_process()` 做完整管线（demosaic + WB + 色彩矩阵 + gamma）
- **优点**: 最完整，直接获得 sRGB 或其他输出色彩空间的 8-bit/16-bit 图像，适合直接显示。
- **缺点**: 如果应用层需要线性数据做 HDR、曝光合成或专业调色，gamma 和色彩空间转换会丢失信息。

---

## 4. 推荐方案：Option A（带参数说明）

**推荐理由**：
1. 当前代码已经使用 LibRaw 做文件解析和 Bayer 提取，完全利用 `dcraw_process()` 是自然的扩展。
2. AHD (user_qual=3) 在速度和画质之间达到最佳平衡，是 dcraw/LibRaw 的默认推荐。
3. 通过设置 `output_bps=16`、`no_auto_bright=1`、`gamm[0]=gamm[1]=1.0`，可以输出保持线性的 16-bit RGB，满足专业后期需求。
4. 避免自行维护 demosaic 算法，减少代码复杂度和 bug 风险。

**关键 API 调用**：

```cpp
LibRaw processor;
processor.open_file(filePath.c_str());
processor.unpack();

// 配置后处理参数
processor.imgdata.params.user_qual = 3;          // AHD demosaic
processor.imgdata.params.use_camera_wb = 1;      // 使用相机白平衡
processor.imgdata.params.use_camera_matrix = 1;  // 使用相机色彩矩阵
processor.imgdata.params.output_bps = 16;        // 16-bit 输出
processor.imgdata.params.no_auto_bright = 1;     // 关闭自动亮度
processor.imgdata.params.output_color = 0;       // 输出相机线性空间
processor.imgdata.params.gamm[0] = 1.0f;         // 关闭 gamma
processor.imgdata.params.gamm[1] = 1.0f;

// 执行后处理
int ret = processor.dcraw_process();
if (ret != LIBRAW_SUCCESS) { /* error */ }

// 获取结果到内存
libraw_processed_image_t* img = processor.dcraw_make_mem_image(&ret);
if (img && img->type == LIBRAW_IMAGE_BITMAP) {
    // img->data 为 RGB 数据，img->bits=16, img->colors=3
    // 拷贝到 out.data
    out.width = img->width;
    out.height = img->height;
    out.channels = 3;
    out.data.resize(img->data_size / 2); // 16-bit = 2 bytes
    std::memcpy(out.data.data(), img->data, img->data_size);
    LibRaw::dcraw_clear_mem(img);
}
```

---

## 5. 注意事项与限制

1. **内存管理**：`dcraw_make_mem_image()` 分配的内存必须用 `LibRaw::dcraw_clear_mem()` 释放，不会在 `recycle()` 中自动释放。
2. **尺寸变化**：`dcraw_process()` 后 `imgdata.sizes.width/height` 可能与原始 Bayer 尺寸不同（如 Fuji 旋转、非正方形像素）。应使用 `get_mem_image_format()` 或 `dcraw_make_mem_image()` 返回的实际尺寸。
3. **X-Trans 传感器**：Fuji X-Trans 使用 6×6 色彩滤波阵列，不是标准 Bayer。LibRaw 会自动处理，但 `user_qual` 设置对 X-Trans 的插值路径不同（使用 `xtrans_interpolate()`）。
4. **线程安全**：LibRaw 对象应仅在一个线程中使用。多线程场景需每个线程独立创建 `LibRaw` 实例。
5. **性能**：AHD 插值比当前 bilinear 慢，但现代 CPU 处理一张 24MP RAW 通常在 100-300ms 内，可接受。
6. **色彩空间**：若需要直接输出 sRGB 供显示，可设置 `output_color = 1`（sRGB）并保留默认 gamma 曲线。

---

## 6. 结论

建议采用 **Option A**，通过 `dcraw_process()` + `dcraw_make_mem_image()` 替换当前手动 Bayer 提取和 `decodeToRgb()` 的简单插值。设置 `user_qual=3`（AHD）可在不显著增加计算成本的情况下，大幅提升 demosaic 质量，同时保留 16-bit 线性数据供后续处理。此方案代码改动集中，无需引入新依赖，是性价比最高的改进路径。
