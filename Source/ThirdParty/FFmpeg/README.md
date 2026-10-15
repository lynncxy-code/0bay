# FFmpeg（视频孪生直连摄像头用）

`Source/OfficeHost/VideoTwinRtspReader.cpp` 用它直接拉摄像头的 RTSP 并解码，替代原来"MediaMTX 转 HLS → Electra 播放"这条路
(2026-10-15 现场实测，那条路让投影视频落后摄像头 3.6 秒)。

| 项 | 值 |
|---|---|
| 版本 | FFmpeg 8.1,BtbN/FFmpeg-Builds 的 `ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip`(2026-10-14 构建) |
| 来源 | https://github.com/BtbN/FFmpeg-Builds/releases/tag/latest |
| SHA-256(zip) | `1689da96399f372cfee985b3f2b9d74685892078059bc37764388f49e3118bdb`,与该发布的 `checksums.sha256` 一致 |
| 许可 | LGPL 2.1 或更高（见 `LICENSE.txt`），**不含 GPL 组件** |

只取了用得到的部分:

```
include/   libavformat libavcodec libavutil libswscale libswresample 的头文件
lib/Win64/ 对应的 MSVC 导入库
bin/Win64/ avformat-62 avcodec-62 avutil-60 swscale-9 swresample-6 的 DLL
```

- DLL 以动态库方式使用(`OfficeHost.Build.cs` 里延迟加载，打包时随包复制到 `Binaries/Win64`)，这是 LGPL 允许的用法；
  分发时要带上 `LICENSE.txt`，且不得静态链接、不得改用 GPL 版构建(GPL 版带 x264 等，会把整个程序拖进 GPL)。
- `.dll` / `.lib` 走 Git LFS(见仓库根目录 `.gitattributes`)。
- 升级:换同一来源的 lgpl-shared 包，DLL 文件名里的主版本号变了的话，同步改 `OfficeHost.Build.cs` 和
  `VideoTwinRtspReader.cpp` 里的 DLL 列表。
