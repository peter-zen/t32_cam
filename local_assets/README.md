# Local Assets

本目录用于存放**不纳入 Git 管理**的大体积 simulation 音视频资源。

建议目录结构：

```text
local_assets/
└── rtsp/
    ├── audio/
    │   └── full_frame_camera_g711a.alaw
    └── video/
        ├── full_frame_camera.h264
        └── full_frame_camera_no_b_30s.h264
```

说明：

- `full_frame_camera.h264`：原始 H.264 源，可包含 B 帧
- `full_frame_camera_no_b_30s.h264`：由脚本生成的 no-B 30 秒测试片段
- `full_frame_camera_g711a.alaw`：可选的外部音频资源；如果不提供，当前 HAL 仍可能退回内部模拟音频

相关脚本：

```bash
./script/use_rtsp_no_b_source.sh
./script/use_rtsp_no_b_source.sh --restore-default
```

注意：

- 本目录内容默认被 `.gitignore` 忽略
- 只保留本说明文件纳入版本控制
