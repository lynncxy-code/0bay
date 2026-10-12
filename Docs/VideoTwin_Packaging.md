# 视频孪生:现状、打包与部署说明

更新于 2026-10-12。分支 `feat/videotwin-calibrated`(只在开发机本地,未推送)。
`Docs/VideoTwinMVP.md` 讲的是最初的功能原型,这份讲现在的状态,以及怎么打包、怎么在目标机上跑起来。

> 标「已验证」的是实际跑过并看到结果的;标「未验证」的是按原理应当成立、但没有条件确认的。

## 一、现在是什么状态

主关卡 `Office_Demo` 运行后,真实摄像头画面按实测位置投到场景里:一部分区域显示视频,其余保持虚拟模型。

| 相机 | 状态 | 负责的区域 | 标定 |
|---|---|---|---|
| 17 | 启用 | 工位区(主体) | 实测 |
| 24 | 启用 | 工位区靠入口一侧 | 实测 |
| 18 | 启用 | 东侧开放区 | 实测 |
| 19 | 启用 | 圆桌休息区 | **未实测**,用的是模型里摆放的位置,估计有 0.5~1 米误差 |
| 21、23 | 关闭 | 茶水间一带 | 实测。关闭原因:那一带模型里的吧台、高脚椅、绿植柜和现场位置不一致,视频落在错的形状上会被拉花 |
| 20、22、25~30 | 关闭 | — | 未实测,各自的原因写在 `calibration.json` 的 `disabled_reason` 里 |

运行时按键:`Z` 开关融合,`X` 逐台切到摄像头视角,`C` 回到原视角。
左上角状态栏每台相机后面会标 `实时` / `录像` / `录像(实时连不上)`。

要开关某台相机、调它负责的区域,只改 `Content/VideoTwin/calibration.json`,不用重新编译,也不用重新打包(见第三节)。

## 二、打包

### 结论

**已验证:能打包,打出来的程序能正常运行并显示视频。**
2026-10-12 在开发机上做了一次完整试打包(Win64,Development),再用打出来的 `OfficeHost.exe` 后台运行验收:

- 4 台相机全部 `measured=1, recording=1, material=1`
- 截图里视频正常投在场景上,亮度正常
- 试打包的输出在开发机 `D:\0bay\VideoTwinPkgTrial\`(2.5 GB),确认无用后可以直接删

试打包用的命令(供对照,正式打包按你们自己的流程走即可):

```
RunUAT.bat BuildCookRun -project=<...>\ShanghaiOfficeShell_OpenSteel.uproject -noP4 -platform=Win64
    -clientconfig=Development -build -cook -stage -pak -nocompileeditor -skipbuildeditor
    -stagingdirectory=D:\0bay\VideoTwinPkgTrial -unattended -utf8output
```

### 为了能打包,工程里改了四处

正式打包必须带着这四处,缺任何一处视频都出不来。它们都已提交在分支上。

| 改动 | 位置 | 不改的后果 |
|---|---|---|
| 显式启用播放器插件 `ElectraPlayer` | `ShanghaiOfficeShell_OpenSteel.uproject` | 该插件默认不启用,工程文件里原先没有写。编辑器里能放是被别的插件顺带启用的,打包后不保证 |
| 始终烘焙 `/Game/VideoTwinMVP` | `Config/DefaultGame.ini` | 投影材质是运行时按路径加载的,没有被别的资产引用,烘焙器会把它们丢掉 |
| `Content/VideoTwin` 作为散文件带进包 | `Config/DefaultGame.ini` | 读不到标定文件,所有相机都不投影 |
| 代码不再依赖编辑器专有接口 | `Source/OfficeHost/VideoTwinProjector.cpp` | 游戏目标编译不过 |

### 打包后怎么确认

以打包输出里的 `ShanghaiOfficeShell_OpenSteel/` 为根,先看文件:

```
Content/VideoTwin/calibration.json        必须有,散文件,可以现场直接改
Content/Movies/CameraNN_Real.mp4          回退用的录像,见第五节
Content/Paks/...                          材质在这里面,看不到,靠日志确认
```

再运行一次,看 `Saved/Logs/` 里的日志,每台启用的相机应有一行:

```
VideoTwin runtime: camera 17 ready (measured=1, recording=1, material=1, calibration=.../Content/VideoTwin/calibration.json)
```

- `measured=0`:没读到标定文件,检查上面的路径。
- `material=0`,并且有一行 `Was /Game/VideoTwinMVP cooked?`:材质没被烘焙进去,检查 `DefaultGame.ini`。
- `recording=0`:包里没有这台相机的录像。实时流正常时不影响。

**画面上"没有视频"有好几种原因,只有这几行日志能分清。**

日志里会有一行 `SkipPackage: /Game/VideoTwinMVP/M_VideoProjection_17 ... does not exist`,这是正常的:17 用的是不带编号的那份材质。

## 三、现场调整:只改 calibration.json

打包后这个文件是散文件,改完重启程序即可生效。

- `enabled_cameras`:启用哪几台,例如 `["17","24","18","19"]`。
- 每台相机的 `receiver_min` / `receiver_max`:这台相机负责投视频的范围(世界坐标,厘米,地面高度是 4320)。范围以外保持虚拟模型。
- `priority`:两台相机范围重叠时,数值大的那台生效。
- `stream_url`:实时流地址。连不上时自动改放录像。

位置、朝向、镜头参数(`position_cm`、`forward`、`up`、`k1`、`k2` 等)是标定结果,不要手改。

## 四、目标机上装转换服务

UE 自带的播放器打不开摄像头的 RTSP,所以中间用 MediaMTX 把 RTSP 转成 HLS,实测延迟 4.0~4.7 秒。
转换服务和 UE 跑在同一台机器上。

把下面四个文件放进目标机的一个文件夹(比如 `D:\VideoTwinLive\`):

```
mediamtx.exe                  1.21.0 版。不在仓库里(56 MB),从开发机 D:\0bay\OfficeShell\Saved\VideoTwinMVP\live\ 拷
mediamtx.videotwin.yml        仓库 Tools\VideoTwin\
start_live.ps1                仓库 Tools\VideoTwin\
install_live.ps1              仓库 Tools\VideoTwin\
```

用**将来运行 UE 的那个 Windows 账号**登录,在这个文件夹里执行:

```
powershell -ExecutionPolicy Bypass -File install_live.ps1
```

它会问一次摄像头口令,存成该用户的环境变量 `OFFICE_CAM_PASSWORD`(不写进任何文件);
登记一个登录时自启动的计划任务 `VideoTwinLive`;立即启动,并报告每台摄像头连上了没有。

卸载:`schtasks /Delete /TN VideoTwinLive /F`

配置文件里已经写好 17、24、18、19、21、23 六台的地址(规律是 `192.168.229.(通道号 - 15)`)。
多配的两台不影响:UE 不启用就不会去取。

### 和昨天装的投屏组件不冲突

昨天装的投屏是计划任务 `ue-cast` / `ue-input`、文件夹 `C:\ue-cast`、端口 8099;这边是计划任务 `VideoTwinLive`、端口 `127.0.0.1:8888`,互不相干。有三点要留意:

- 投屏是按窗口标题抓画面的,打包后的窗口标题如果和之前不同,投屏会抓不到。
- 两边都吃显卡,见第六节。
- 投屏会把 UE 窗口通过网络发出去,而现在窗口里有真实的监控画面。

### 两条安全相关的设定,不要改回去

- 配置里 `rtsp: no`、`moq: no`。MediaMTX 默认还会在所有网卡上开 RTSP 和 MoQ 服务,等于把摄像头画面挂到局域网上。已关闭。
- HLS 只绑 `127.0.0.1`。转换服务和 UE 在同一台机器,不需要对外。

## 五、录像文件带不带,需要有人拍板

`Content/Movies/CameraNN_Real.mp4` 是 10 月 10 日录的真实画面,用作实时流连不上时的回退。

- 它们不在 git 里,但只要文件在 `Content/Movies/` 下,打包时就会被原样带进包。**已验证**:试打包里 15 个文件全部被带了进去,包括没启用的那些相机。
- 画面里有可辨认的同事,包会被拷到别的机器。
- 不带的话:实时流连不上时,该相机不显示视频。

打包前按决定处理:

- 都不带:把 `Content/Movies/Camera*_Real.mp4` 临时挪出去。
- 只带在用的:留下 17、24、18、19 四个,其余挪出去。

## 六、还没验证的事

这些在开发机上没有条件确认,到目标机后要先看:

1. **帧率**。目标机是 RTX 3080,到目前为止所有验证都是在开发机(RTX 5090)上后台渲染截图,帧率一次都没量过。每台相机带来一次全屏后处理、一张每 3 秒重渲染一次的深度图、一路视频解码。如果不够,按这个顺序减:
   - 深度图改成只在启动时渲染一次(改 `DepthRefreshSeconds`,需要重新编译)
   - 在 `enabled_cameras` 里减相机
   - 让摄像头输出更低的分辨率或帧率
2. **真实摄像头经过转换服务再到 UE 这条链路**。开发机所在网络连不到摄像头,这条链路只用模拟源测过。可能出问题的三处:
   - 主码流是 H.264 还是 H.265 没有查证过
   - 关键帧间隔:如果摄像头是 2 秒一个关键帧,延迟会从 4 秒多变成 7 秒以上
   - 四路 2560×1440、每秒 25 帧同时解码的开销
3. **Shipping 配置**。试打包用的是 Development。

出问题时看两份日志:转换服务文件夹里的 `mediamtx.log`,和 UE 的 `Saved/Logs`。

## 七、相关文件

```
Source/OfficeHost/VideoTwinProjector.h/.cpp     全部运行时代码
Content/VideoTwin/calibration.json              标定、启用列表、投影范围、流地址
Content/VideoTwinMVP/                           投影材质(每台相机一份,内容相同)
Tools/VideoTwin/projection.hlsl                 投影着色器的唯一来源
Tools/VideoTwin/update_material_code.py         改了着色器后,用它同步到所有材质
Tools/VideoTwin/dup_materials.py                给新相机生成材质
Tools/VideoTwin/mediamtx.videotwin.yml          转换服务配置
Tools/VideoTwin/start_live.ps1, install_live.ps1  转换服务的启动与安装
```
