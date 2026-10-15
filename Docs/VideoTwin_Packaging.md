# 视频孪生:现状、打包与部署说明

更新于 2026-10-15。分支 `feat/videotwin-onsite-1015`(在 224.7 的源码副本 `D:\BaiduNetdiskDownload\New folder\UE0bay\OfficeShell` 上,未推送)。
`Docs/VideoTwinMVP.md` 讲的是最初的功能原型,这份讲现在的状态,以及怎么打包、怎么在目标机上跑起来。

> 标「已验证」的是实际跑过并看到结果的;标「未验证」的是按原理应当成立、但没有条件确认的。

10 月 15 日在现场(192.168.224.7,RTX 3080 Ti)解决了卡顿、平板投屏、画面抖动等一批问题,结论分散写在下面各节;
完整的排查过程见第八节。

## 一、现在是什么状态

主关卡 `Office_Demo` 运行后,真实摄像头画面按实测位置投到场景里:一部分区域显示视频,其余保持虚拟模型。

| 相机 | 状态 | 负责的区域 | 标定 |
|---|---|---|---|
| 17 | 启用 | 工位区(主体) | 实测 |
| 24 | 启用 | 工位区靠入口一侧的过道 | 实测 |
| 18 | 启用 | 东侧开放区 | 实测 |
| 19 | 启用 | 圆桌休息区 | **未实测**,用的是模型里摆放的位置,估计有 0.5~1 米误差 |
| 21、23 | 关闭 | 茶水间一带 | 实测。关闭原因:那一带模型里的吧台、高脚椅、绿植柜和现场位置不一致,视频落在错的形状上会被拉花 |
| 20、22、25~30 | 关闭 | — | 未实测,各自的原因写在 `calibration.json` 的 `disabled_reason` 里 |

运行时按键(平板页面上有同名按钮):

| 键 | 作用 |
|---|---|
| `Z` | 开关融合。一键包启动时默认关(启动参数 `-VideoTwinStartOff`) |
| `X` | 逐台切到摄像头视角。切换有过渡动画,动画中按键会丢,按后等几秒 |
| `C` | **复位**:如果在摄像头视角就先退出,再回到开场时的位置和朝向 |

- 摄像头视角下只显示这台相机自己的视频,而且整幅显示、不做遮挡裁切(`camera_view_only_own`);别的相机的投影从这个位置看是歪的,只会拉花。
- 「开场位置」是场景走完默认路线后停住的那个画面:在第一次操作(移动、转视角、点鼠标、按 X)之前一直跟着更新,第一次操作时定下来。
  只等「画面静止 5 秒」在编辑器里可以,Shipping 包里开场画面从不完全静止,结果一直定不下来,按 C 没反应。
- 左上角状态栏(每台相机是 `实时` / `录像`、融合开关)默认**不显示**(`show_status: false`),排查时改成 `true` 再重启。
- **视频由 UE 直接拉摄像头 RTSP**(FFmpeg,`VideoTwinRtspReader`),投影比摄像头晚约 0.1 秒;原来经转换服务转 HLS 是 3.6 秒(见第八节第 8 条)。
- 实时流断了会自己重连:读取线程每 2 秒重试一次;断流 3 秒(首次连接 12 秒)没画面就先放录像,画面回来自动切回实时。

要开关某台相机、调它负责的区域、改上面这些开关,只改 `Content/VideoTwin/calibration.json`,不用重新编译,也不用重新打包(见第三节)。

## 二、打包

### 结论

**已验证:Shipping 包能正常运行并显示实时视频。** 2026-10-15 在 224.7 上打了多次 Shipping 包,替换进一键包的 `Runtime` 实际运行。

打包命令(`-iterate` 让第二次起只烘焙改动的资产):

```
RunUAT.bat BuildCookRun -project=<...>\ShanghaiOfficeShell_OpenSteel.uproject -noP4 -platform=Win64
    -clientconfig=Shipping -build -cook -iterate -stage -pak -nocompileeditor -skipbuildeditor
    -stagingdirectory=D:\VideoTwinBuild\<日期标签> -unattended -utf8output
```

替换一键包:先关掉一键包的 UE(**还要结束残留的 `TracerWebHelper.exe`,它会锁住 Runtime 目录**),
把原来的 `Runtime` 改名备份(不要删),再把 `D:\VideoTwinBuild\<标签>\Windows\` 拷成新的 `Runtime`。
回退就是把备份改回 `Runtime`。

### 为了能打包,工程里改了这些

正式打包必须带着这些改动,它们都已提交在分支上。

| 改动 | 位置 | 不改的后果 |
|---|---|---|
| 显式启用播放器插件 `ElectraPlayer` | `ShanghaiOfficeShell_OpenSteel.uproject` | 该插件默认不启用。编辑器里能放是被别的插件顺带启用的,打包后不保证 |
| 始终烘焙 `/Game/VideoTwinMVP` | `Config/DefaultGame.ini` | 投影材质是运行时按路径加载的,没有被别的资产引用,烘焙器会把它们丢掉 |
| **始终烘焙 `/Game/Art/Furniture`、`/Game/Art/camera`、`/OntoTwinSync`、`/OntoTwinCharacterPack_Core`** | `Config/DefaultGame.ini` | 这些是运行时按数据生成的家具和相机模型,同样没有被引用。**已验证**:不加时包里的桌椅全是棋盘格方块 |
| `Content/VideoTwin` 作为散文件带进包 | `Config/DefaultGame.ini` | 读不到标定文件,所有相机都不投影 |
| 代码不再依赖编辑器专有接口 | `Source/OfficeHost/VideoTwinProjector.cpp` | 游戏目标编译不过 |
| 模块依赖加 `HTTP` | `Source/OfficeHost/OfficeHost.Build.cs` | 实时流自动重连的探测要用 |
| FFmpeg 8.1(LGPL 动态库) | `Source/ThirdParty/FFmpeg`,`OfficeHost.Build.cs` | 直连摄像头要用。DLL 随包复制到 `Binaries/Win64`;缺了会退回放录像。来源和许可见该目录 README |
| 抗锯齿三项(见下) | `Config/DefaultEngine.ini` | 投影视频抖动 |

抗锯齿这三项缺一不可:

```
r.AntiAliasingMethod=2          时间抗锯齿(TAA)。不能用 FXAA(=1):投影的遮挡判断依赖它,FXAA 下会出现大块缺口
r.TemporalAASamples=1           关掉每帧的子像素镜头抖动,否则投影视频边缘每帧都在闪
r.TemporalAA.Upsampling=0       关掉时间性放大,否则窗口切全屏后抖动又回来(见第八节第 4 条)
```

一键包另有一份 `VideoTwinLive\Engine.videotwin.ini`,内容就是这三行,`Start-Runtime.ps1` 每次启动 UE 前把它复制成
`%LOCALAPPDATA%\ShanghaiOfficeShell_OpenSteel\Saved\Config\Windows\Engine.ini`。
打包版 UE 读完用户 Engine.ini 会把它删掉,所以**现场要改渲染设置,改的是 `Engine.videotwin.ini`**,直接改用户 Engine.ini 下次启动就被覆盖。

### 打包后怎么确认

**Shipping 包不写日志**,原来靠 `Saved/Logs` 里 `camera 17 ready (measured=1, recording=1, material=1 ...)` 那几行确认的办法只对 Development 包有效
(而 Development 包直接运行 `Binaries` 下的 exe 会卡在 "Initializing movie player",要用编辑器 `-game` 代替)。Shipping 包这样确认:

1. 文件:以 `Runtime/ShanghaiOfficeShell_OpenSteel/` 为根,`Content/VideoTwin/calibration.json` 必须是散文件。
2. 画面:进场景看桌椅不是棋盘格;按 `Z` 打开融合,工位区出现视频;按 `X` 进摄像头视角,视频整幅显示。
3. 是实时还是录像:临时把 `show_status` 改成 `true` 重启,左上角会写。

## 三、现场调整:只改 calibration.json

打包后这个文件是散文件,改完重启程序即可生效。

全局开关:

| 键 | 现值 | 作用 |
|---|---|---|
| `enabled_cameras` | `["17","24","18","19"]` | 启用哪几台 |
| `camera_view_only_own` | `true` | 摄像头视角下只显示这台自己的视频,整幅显示 |
| `local_light_shadows` | `false` | 点光、聚光、面光不投影子(太阳光照投)。**这是卡顿的根因**,见第八节第 1 条。灯光亮度没有改 |
| `show_status` | `false` | 左上角状态栏 |

每台相机:

- `receiver_min` / `receiver_max`:这台相机负责投视频的范围(世界坐标,厘米,地面高度是 4320)。范围以外保持虚拟模型。
  边界已沿墙根、柜面、过道边线放,保证桌椅和人不会被边界切成两半。现用:
  17 `X 700..2850, Y -2500..-1250`;24 `X 300..1400, Y -1580..-980`;18 `X 4950..5750, Y -1500..700`;19 `X 950..2000, Y -1090..-420`。
- `receiver_feather_cm`:范围四边的羽化宽度,默认 12,0 为硬边。
- `priority`:两台相机范围重叠时,数值大的那台生效。
- `stream_url`:实时流地址,现为摄像头子码流
  `rtsp://admin:${OFFICE_CAM_PASSWORD}@192.168.229.<通道号-15>:554/LiveMedia/ch1/Media2?src=onvif`。
  `${OFFICE_CAM_PASSWORD}` 运行时从同名环境变量填入,口令不写进文件。改回 `http://127.0.0.1:8888/cam<id>/index.m3u8` 就是旧的 HLS 路线(需要转换服务)。

位置、朝向、镜头参数(`position_cm`、`forward`、`up`、`k1`、`k2` 等)是标定结果,不要手改。

## 四、目标机上装转换服务(现在只为提供口令和备用)

**2026-10-15 起 UE 直接拉摄像头 RTSP,不再经过转换服务。** 但 `install_live.ps1` 仍要跑一次:它把摄像头口令存成环境变量
`OFFICE_CAM_PASSWORD`,UE 直连要用。转换服务本身留作备用(把 `stream_url` 改回 HLS 地址时才用到),停掉不影响直连。

原来的做法:UE 自带的播放器打不开摄像头的 RTSP,所以中间用 MediaMTX 把 RTSP 转成 HLS(现场实测比直连慢 3.5 秒)。
转换服务和 UE 跑在同一台机器上。

把下面四个文件放进目标机的一个文件夹(一键包里是 `VideoTwinLive\`):

```
mediamtx.exe                  1.21.0 版。不在仓库里(56 MB)
mediamtx.videotwin.yml        仓库 Tools\VideoTwin\
start_live.ps1                仓库 Tools\VideoTwin\
install_live.ps1              仓库 Tools\VideoTwin\
```

用**将来运行 UE 的那个 Windows 账号**登录,在这个文件夹里执行:

```
powershell -ExecutionPolicy Bypass -File install_live.ps1
```

它会问一次摄像头口令,存成该用户的环境变量 `OFFICE_CAM_PASSWORD`(不写进任何文件,配置里的 `CHANGE_ME` 是占位);
登记一个登录时自启动、不限运行时长的计划任务 `VideoTwinLive`;立即启动,并报告每台摄像头连上了没有。
(以前用 `schtasks` 登记,路径里有空格时引号会被弄乱,装不上;现在改用 `Register-ScheduledTask`。)

卸载:`Unregister-ScheduledTask -TaskName VideoTwinLive -Confirm:$false`

配置文件里已经写好 17、24、18、19、21、23 六台的地址(规律是 `192.168.229.(通道号 - 15)`)。多配的两台不影响:UE 不启用就不会去取。

**取的是子码流 `Media2`**(720×576,H.265,25 帧/秒)。主码流是 2560×1440,四路同时解码会把画面拖到每秒 2~3 帧。
子码流画面本身是稳的:逐帧比对,上下左右位移都是 0。

### 两条安全相关的设定,不要改回去

- 配置里 `rtsp: no`、`moq: no`。MediaMTX 默认还会在所有网卡上开 RTSP 和 MoQ 服务,等于把摄像头画面挂到局域网上。已关闭。
- HLS 只绑 `127.0.0.1`。转换服务和 UE 在同一台机器,不需要对外。

## 五、平板投屏

投屏不在这个仓库里,源码在 metaverse 仓库 `tools/ue-cast.ps1`(抓屏推流)、`ue-input.ps1`(平板操作中继)、`ue-input.html`(平板页面),
224.7 上装在 `C:\ue-cast`,计划任务 `ue-cast` / `ue-input`,平板打开 `http://192.168.224.7:8099/`。
画面路径:224.7 抓屏编码 → 224.4 的 MediaMTX(`rtsp://192.168.224.4:8554/ue-office`)→ 平板浏览器(WebRTC)。

现用参数(2026-10-15 定下,连续观察零卡顿):

```
-Method dda -Framerate 25 -GopSeconds 0.5 -Crf 18 -Bitrate 8M -BufSize 1M -Size 1600x900 -ProcessMatch "OfficeHost"
```

| 参数 | 为什么 |
|---|---|
| `-Method dda` | Desktop Duplication 抓屏。原来的 GDI 抓屏在 UE 占满显卡时出帧不匀:本机流畅、平板一卡一卡 |
| `-Framerate 25` | 和摄像头一样 25 帧。试过 50 帧,没有改善 |
| `-GopSeconds 0.5` | 关键帧间隔 = 出错后最长停顿时间。平板偶尔会解出一帧坏帧,只能等下一个关键帧(浏览器发的关键帧请求经 224.4 传不到推流端)。2 秒时每 20~40 秒停 1.5~2 秒 |
| `-Crf 18 -Bitrate 8M -BufSize 1M` | 按固定画质压缩,码率封顶。按固定码率压时,静止画面每到关键帧整体糊一下,形成有规律的「呼吸」;关键帧不封顶时一个 350 KB,每次都顿一下 |
| `-Size 1600x900` | UE 本来就按 1600×900 渲染,推 1080p 只是多花流量 |
| `-ProcessMatch` | 按进程找 UE 窗口,不依赖窗口标题 |

平板页面:

- 接收缓冲默认 150 ms(地址后加 `?jb=300` 可调)。
- 左上角「···」展开状态栏,显示 fps / 丢帧 / 卡顿次数(页面自己量:两帧真正上屏间隔超过 100 ms 记一次;刷新页面清零)。
- 页面每 10 秒把这些数发回中继,记在 `C:\ue-cast\ue-input.log` 的 `stats` 行。**查平板卡不卡看这里**,不用人盯着平板。
- 平板上的点击按投屏实际截取的区域换算(`ue-cast` 每次开始截取时写 `C:\ue-cast\capture-rect.txt`)。
  以前按整屏换算,UE 不全屏时点击整体偏 130 像素,小地图双击传送落在地图外,看起来像传送功能没了。

注意:投屏会把 UE 窗口通过网络发出去,而窗口里有真实的监控画面。

## 六、录像文件带不带,需要有人拍板

`Content/Movies/CameraNN_Real.mp4` 是 10 月 10 日录的真实画面,用作实时流连不上时的回退。

- 它们不在 git 里,但只要文件在 `Content/Movies/` 下,打包时就会被原样带进包,包括没启用的那些相机。
- 画面里有可辨认的同事,包会被拷到别的机器。
- 不带的话:实时流连不上时,该相机不显示视频。

现场替换 Runtime 时只保留了一键包原有的 5 个(17 的两个、18、19、24),其余删掉。

## 七、还没验证的事

1. **摄像头本身的延迟**(编码 + 网络,到 224.7 收到 RTSP 为止)没量:摄像头水印只到秒,时钟也没对齐。同类设备一般 0.2~0.5 秒。
2. **只在切到某台摄像头视角时才拉那一路流**。用户提过,要改代码,没做。

## 八、2026-10-15 现场排查结论

每条都是实测得出的,写下来是为了下次别再走弯路。

1. **场景卡顿的根因是 40 盏局部灯光投 VSM 阴影**,不是视频、不是深度图重拍。
   编辑器 `-game` 下用 `stat unit` 量:每帧 40 ms / 7721 次绘制 → 关掉局部灯光阴影后 23.7 ms / 1807 次绘制。
   关 Lumen、降分辨率几乎不变;关 VSM 直接显存不够卡死。对应 `local_light_shadows: false`。
   深度图改成只在启动后第 1、6、20 秒各拍一次(`DepthRefreshSeconds=0`),但这不是卡顿主因。
2. **量帧率的坑**:通过平板中转发「转视角」再抓屏,帧率被指令频率封顶在 25~35;GDI 抓屏 60 帧在负载下也跟不上。
   可靠的只有编辑器 `-game -ExecCmds="stat unit"`。
3. **平板卡、本机流畅 = 投屏链路问题**,见第五节。查的顺序:224.4 发出的流是否匀(录 `ue-office` 看帧间隔)→ 平板 `stats` 行
   (卡顿时丢包为 0、ping 平板 3 ms → 不是网络,是等关键帧)。
4. **「摄像头画面有规律地上下颠」= 窗口切成了全屏**。桌面 1920×1080、UE 按 1600×900 渲染再时间性放大,
   放大会把子像素镜头抖动重新打开。场景本身被抗锯齿抹平看不出,投影视频是按深度反算位置贴上去的,跟着抖:
   在 0 和 0.3 像素之间来回跳(本机看只是边缘微闪,压缩后在平板上很明显)。
   加 `r.TemporalAA.Upsampling=0` 后,窗口和全屏下都实测为 0.00 像素(全屏 1920×1080,摄像头视角,融合开)。
   判别办法:录一段投屏,对视频区域逐帧做相位相关求亚像素位移,标准差接近 0 正常,0.1 以上就是在抖;摄像头原始流同法测是 0。
5. **按 C 没反应**:原来 C 只负责退出摄像头视角,漫游时按下去什么都不做。现在是复位,见第一节。
6. **背面剔除用几何法线**(屏幕空间 ddx/ddy 求的面法线)。用 GBuffer 法线时带了法线贴图的起伏,地面上出现黑点。
7. **桌椅成棋盘格方块**:运行时生成的家具没被烘焙,见第二节表格。
8. **视频比现实晚 5~6 秒,大头在 HLS**。同一台机器、同一时钟下录 RTSP 直读 / UE 屏幕 / 投屏,按画面动作对齐:
   RTSP 收到 → UE 显示 3.57 秒(MediaMTX 要攒满 1 秒片段,Electra 固定落后 3 个片段,对 HLS 没有可调参数);
   UE → 投屏 0.3 秒;平板缓冲 0.15 秒。改为 UE 内 FFmpeg 直读 RTSP 后,RTSP 收到 → UE 显示 **0.10 秒**。
   代价:UE 进程里多了四路 720×576 H.265 解码(换包后 UE 进程约占 5 个核,整机 CPU 44~67%,换包前没量)。投屏编码器因此被挤,平板每 10 秒顿 1~4 次;
   把投屏 ffmpeg 设为高优先级后恢复零卡顿(`tools/ue-cast.ps1` 已固化)。

## 九、相关文件

```
Source/OfficeHost/VideoTwinProjector.h/.cpp       全部运行时代码
Content/VideoTwin/calibration.json                标定、启用列表、投影范围、流地址、现场开关
Content/VideoTwinMVP/                             投影材质(每台相机一份,内容相同)
Tools/VideoTwin/projection.hlsl                   投影着色器的唯一来源
Tools/VideoTwin/update_material_code.py           改了着色器后,用它同步到所有材质
Tools/VideoTwin/rebuild_projection_materials.py   着色器输入有增减时(如加了 CameraPos)重建材质节点;编辑器命令行跑,路径用正斜杠
Tools/VideoTwin/dup_materials.py                  给新相机生成材质
Tools/VideoTwin/mediamtx.videotwin.yml            转换服务配置
Tools/VideoTwin/start_live.ps1, install_live.ps1  转换服务的启动与安装
Config/DefaultEngine.ini                          抗锯齿三项
Config/DefaultGame.ini                            始终烘焙的目录、散文件
```
