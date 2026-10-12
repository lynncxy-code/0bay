# 办公室视频孪生 MVP（UE 5.6）

本功能现在已经接入原项目运行时。打开 `Office_Demo` 后，`AGrandNeoBayExperience` 会自动挂载一个运行时视频孪生组件；它保留原有 GameMode、漫游相机、场景交互和 WebUI，只额外显示视频孪生状态并接管摄像头17的演示控制。独立 MVP 地图仍保留，用于隔离调试。

## 原场景运行时（主入口）

双击项目根目录 `Launch_Office_VideoTwin_Runtime.bat`，或在编辑器打开 `/Game/OfficeShell/Maps/Office_Demo` 后点击 Play。进入场景后，左上角会显示 `VIDEO TWIN` 状态；`MOCK VIDEO | PLAYING` 表示当前仍使用本地模拟流。

| 按键 | 功能 |
|---|---|
| Z | 开启/关闭摄像头17的视频融合 |
| F7 | 原工程已有的漫游入口，保持不变 |
| X | 切到关卡中已放置的摄像头17视角 |
| C | 返回进入摄像头视角前的原视角 |

运行时组件只创建一个摄像头17投影器，不替换原地图、不替换原 GameMode，也不会关闭原有 OntoTwin 网络和 WebUI。当前 MVP 只给17路接入 mock，14路通道表已记录在 `Tools/VideoTwin/channels.json`，其余13路等待后续接真实流。

## 启动

`Launch_VideoTwin_MVP.bat` 仍可启动隔离演示地图。它用本机 `D:\UE_5.6` 启动 UE 独立游戏预览，需保留编辑器安装；本次没有交付独立打包 EXE。

在编辑器中也可打开 `/Game/VideoTwinMVP/Maps/Office_VideoTwin_MVP`，点击 Play。

## 操作

| 按键 | 功能 |
|---|---|
| 1 | 打开/关闭三维视频融合 |
| 2 | 回到摄像头17的投影视角 |
| 3 | 切换到偏移观察视角 |
| 4 | 左侧融合、右侧模型对照 |
| 5 | 投影覆盖与遮挡调试：绿为可见，红为被遮挡 |
| Space | 暂停/继续 mock 视频 |
| O | 深度遮挡开关，用于对照 |
| WASD、Q/E | 前后左右、下降/上升 |
| 按住鼠标右键 | 转动观察视角 |
| Shift | 加速移动 |
| PgUp/PgDn | 调整投影水平视场角，每次0.5度 |
| 方向键 | 调整投影偏航/俯仰，每次0.25度 |
| R | 重置本次投影标定偏移 |

运行时快捷键调参不自动保存。需长期保存时，在编辑器选中 `VideoTwin_Projector_17_MOCK`，修改 Details 的 Calibration 参数后保存 MVP 关卡。

## 已实现

- 相机17引用关卡中用户放置的 `OntoTwin_Camera_17`，保留其位置、朝向、90度水平FOV及16:9比例。
- 图三原始截图保存在 `Art/VideoTwinMVP/Camera17_Reference.jpg`。
- `Content/Movies/Camera17_Mock.mp4` 为12秒、1920×1080、15fps H.264循环视频。办公室画面来自静态截图，只有 mock 计时和角标变化，不代表真实人员运动。
- UE MediaPlayer / MediaTexture 解码本地视频，失败时回退到参考图片。
- 后处理材质使用模型表面世界坐标做相机投影；不会替换原家具材质。
- 摄像头视角的1920×1080浮点线性深度用于遮挡判断。使用3×3保守采样减轻薄边和时域重建造成的误拒绝，边缘有约1纹素的容差。
- 体积范围、投影距离、透明度、主点、径向畸变系数、位置/旋转/FOV偏移均可编辑。
- 源画面缩略图、融合对照、覆盖调试、飞行观察、暂停功能。
- `Tools/VideoTwin/channels.json` 记录17–30共14个现有相机的真实保存变换。**只有17路配置了mock媒体，其余13路未配置；没有声称已验证14路并发播放。**
- 独立MVP地图移除了原项目联网管理器、动态目标桥接和园区外景子关卡，不依赖业务后端。

## 几何和标定限制

保存的源地图在相机17区域含墙柱、柜台、柜子、花槽，但缺少图三中的工位桌面。为避免画面全部落到地面，MVP增加了 `VideoTwinMVP/Receivers_Approximate` 下的桌面/隔板/桌腿代理几何；按现有花槽排列估算，可分别移动或删除，未宣称为实测布局。原模型未改变。

当前使用用户放置的相机初始视角，没有完成镜头内参、畸变和工位几何的精确标定。径向畸变系数默认0。较大观察角度下会出现桌面错位、拉伸、投影空洞；单目图像中的人和椅子没有真实三维深度。这一版本验证接收、解码、纹理、三维投影、遮挡和交互链路。

下一步应先用实测工位模型替换代理，并以固定角点完成相机标定，再接真实视频；最后处理人物分割、多摄像头重叠及14路解码/带宽/显存调度。

## 接入真实视频

在投影器上替换 `Media Source`、取消 `Mock Source`，然后调用 `OpenVideo` 或重新运行。源协议需要有对应UE播放器支持；RTSP不能仅凭修改URL假定Electra能播放，需另接支持RTSP的插件/解码层。本MVP没有访问真实摄像头，也未实现网络断线重连。

## 文件与构建

- `Source/OfficeHost/VideoTwinProjector.h/.cpp`：投影器、演示控制器、HUD、GameMode。
- `Source/OfficeHost/GrandNeoBayExperience.cpp`：在原 `Office_Demo` 运行时自动生成视频孪生组件。
- `Tools/VideoTwin/projection.hlsl`：投影、范围、畸变、遮挡、混合公式。
- `Tools/VideoTwin/build_assets.py`：生成材质/媒体资产和独立地图；重跑会重建MVP地图并覆盖其中的手工编辑。
- `Tools/VideoTwin/build_module.ps1`：仅构建OfficeHost。应先关闭使用此DLL的UE窗口。
- `Tools/VideoTwin/make_mock.py`：使用imageio-ffmpeg重新生成mock。生成依赖仅位于Saved目录，运行MVP不需要Python或FFmpeg。
- `Launch_VideoTwin_MVP.bat`：预览入口。
- `Launch_Office_VideoTwin_Runtime.bat`：原 `Office_Demo` 运行时入口。

完整构建曾在宿主Binaries目录生成外部OntoTwin插件DLL副本，与外部插件原DLL重复加载。构建脚本会将本次宿主目录中的这些副本保存到 `Saved/VideoTwinMVP/DuplicateBuildModules_*`，并移除宿主模块清单中的重复条目；外部插件源码和原DLL未改动。

## 验证证据

`-VideoTwinTest` 自动生成 `Saved/VideoTwinMVP/01_source.png`、`02_model.png`、`03_orbit.png`、`04_comparison.png` 和 `runtime_acceptance.json`。日志位于 `Saved/VideoTwinRuntime.log`。截图必须结合上述几何/标定限制阅读，不能当作精确空间标定验收。
