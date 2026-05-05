# 拍照分辨率双路径方案

## 拍照分辨率规格

产品规格定义了 9 档拍照分辨率：

| 名称 | 分辨率 | 像素数 |
|------|--------|--------|
| 2M  | 1920 x 1080 | 2,073,600 |
| 4M  | 2560 x 1440 | 3,686,400 |
| 5M  | 2560 x 1944 | 4,976,640 |
| 6M  | 2688 x 2048 | 5,502,976 |
| 8M  | 3840 x 2160 | 8,294,400 |
| 16M | 5120 x 2880 | 14,745,600 |
| 24M | 6400 x 3600 | 23,040,000 |
| 32M | 7680 x 4320 | 32,768,000 |
| 42M | 8640 x 4860 | 41,990,400 |

## 传感器可选分辨率

不同传感器型号支持的分辨率不同：

| 分辨率 | GC4653 | 6603 | 8613 |
|--------|--------|------|------|
| 2M  | Y | Y | Y |
| 4M  | Y | Y | Y |
| 5M  | - | Y | - |
| 6M  | - | Y | - |
| 8M  | Y | Y | Y |
| 16M | Y | Y | Y |
| 24M | Y | Y | Y |
| 32M | Y | Y | Y |
| 42M | Y | Y | Y |

传感器型号在编译时通过宏确定（如 `SENSOR_TYPE_GC4653`），属性管道据此过滤可选项。当前仅实现 GC4653。

## 双路径设计

硬件编码器受限于传感器原始分辨率（GC4653 为 2560x1440）。超过传感器分辨率的拍照需要走软件编码路径。

### 路径选择逻辑

```
目标分辨率 <= 传感器分辨率  →  硬件编码路径
目标分辨率 >  传感器分辨率  →  软件编码路径
```

以 GC4653 (2560x1440) 为例：
- 2M (1920x1080)、4M (2560x1440) → 硬件编码
- 8M/16M/24M/32M/42M → 软件编码

### 硬件编码路径

复用现有 ImageSnap 流程：通过 IMP SDK 硬件编码器直接输出 JPEG。

### 软件编码路径

采用分条裁剪 + SIMD 缩放 + 软件 JPEG 编码 + 拼接的方案，流程如下：

```
1. 获取传感器原始 NV12 帧
   IMP_FrameSource_GetFrame(channel, &frame)

2. 分条处理（每条 32 行）
   for each strip:
     a. 裁剪：从原始帧中裁出 src_w x 32 的 NV12 条带
     b. 缩放：SIMD 双线性插值缩放到 dst_w x strip_h
        - dst_w <= 7680: 使用 libimp.a 内的 c_resize_simd()
        - dst_w > 7680: 使用 MXU2 SIMD 实现的 opencv_resize_crop_simd()
     c. JPEG 编码：IMP_Encoder_InputJpege() 对缩放后的条带进行软件 JPEG 编码
     d. 提取扫描数据：从编码结果中解析出 SOS 之后的扫描数据（去除条带自身的 JPEG 头和 EOI）

3. 拼接为最终 JPEG 文件
   a. 写入完整的 JPEG 头（SOI + DQT + DRI + SOF0 + DHT + SOS），
      其中 SOF0 的宽高设为最终目标分辨率
   b. 逐条写入扫描数据，条带之间插入 RST restart marker (0xFF D0~D7)
   c. 最后写入 EOI (0xFF D9)
```

### 内存管理

软件路径需要以下缓冲区：

| 缓冲区 | 用途 | 分配方式 |
|--------|------|----------|
| crop_buf | 一条源数据 (src_w x 32 NV12) | malloc |
| resize_buf | 一条缩放后数据 (dst_w x strip_h NV12) | IMP_Encoder_VbmAlloc（物理连续内存） |
| jpeg_buf | 一条 JPEG 编码输出 | malloc |
| simd_tmp_buf | SIMD 缩放临时缓冲 | malloc |

resize_buf 需要物理连续内存，因为 `IMP_Encoder_InputJpege` 需要通过 `IMP_Encoder_VbmV2P` 获取物理地址。每次拍照分配，完成后释放。

## 存储设计

### stillSize 存储格式

`stillSize` 存储为分辨率枚举值（0~8），直接对应分辨率表的索引：

- 0 = 2M, 1 = 4M, 2 = 5M, 3 = 6M, 4 = 8M, 5 = 16M, 6 = 24M, 7 = 32M, 8 = 42M
- 默认值: 1 (4M)

### 属性管道交互

- **getter**: 枚举值 → 名称字符串（如 4 → "8M"）
- **setter**: 名称字符串 → 枚举值（如 "8M" → 4）
- **可选列表**: 按传感器型号过滤（GC4653 不显示 5M/6M）

## JPEG 编码质量

质量参数来源（优先级从高到低）：

1. API 调用时传入的 quality 参数
2. Settings 中存储的 stillQuality
3. 默认值 85

质量参数仅影响软件编码路径的 JPEG 量化表。硬件路径的 JPEG 质量由硬件编码器控制。

## 依赖项

- **libimp.a**: 提供 `c_resize_simd`（SIMD 双线性缩放）、`IMP_Encoder_InputJpege`（软件 JPEG 编码）、`IMP_Encoder_VbmAlloc/VbmFree/VbmV2P`（物理内存管理）、`IMP_FrameSource_GetFrame/ReleaseFrame`（帧获取）
- **mxu2.h**: Ingenic MXU2 SIMD intrinsics 头文件，来自 T32 BSP 交叉编译工具链，供 >7680 宽度的 SIMD 缩放使用
