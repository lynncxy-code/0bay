# 视频孪生:打包与部署到目标机

适用于"在这台机器上开发,打包后拿到另一台机器上运行"的情况。
`Docs/VideoTwinMVP.md` 讲的是功能本身,这份只讲打包和部署。

> 标了「已验证」的是实际跑过的;标了「未验证」的是按原理应当成立、但没有在打包后的程序上确认过。
> 截至 2026-10-12,**还没有人把带视频孪生的版本完整打包并运行过**。

## 一、打包前:工程里已经做好的

| 项 | 做法 | 状态 |
|---|---|---|
| 代码不依赖编辑器 | `GetActorLabel` / `SetActorLabel` 只在编辑器里存在,已用 `#if WITH_EDITOR` 包住。打包后的程序不再去找关卡里手摆的相机,直接按实测标定生成观察相机 | **已验证**:游戏目标 `OfficeHost` 编译通过(`Build.bat OfficeHost Win64 Development`) |
| 材质等资产会被烘焙 | `Config/DefaultGame.ini` 加了 `+DirectoriesToAlwaysCook=(Path="/Game/VideoTwinMVP")`。这些资产是运行时按路径加载的,没有被别的资产引用,不加这行烘焙器会把它们丢掉 | 未验证 |
| 标定文件会被带进包 | `calibration.json` 挪到了 `Content/VideoTwin/`,并加了 `+DirectoriesToAlwaysStageAsNonUFS=(Path="VideoTwin")`,作为散文件原样拷进包 | 未验证 |

## 二、打包后:先检查这三样在不在

以打包输出目录 `<包>/ShanghaiOfficeShell_OpenSteel/` 为根:

```
Content/VideoTwin/calibration.json        必须有,散文件
Content/Movies/Camera17_Real.mp4 等        见下面第四节,可以没有
(材质在 pak 里,看不到,靠日志确认)
```

然后运行一次,看日志 `Saved/Logs/*.log` 里这几行:

```
VideoTwin runtime: camera 17 ready (measured=1, recording=1, material=1, calibration=.../Content/VideoTwin/calibration.json)
```

- `measured=0`:没读到标定文件。检查上面那个路径。
- `material=0`,并且有一行 `Was /Game/VideoTwinMVP cooked?`:材质没被烘焙进去。检查 `DefaultGame.ini` 那一行,或者在打包设置的"始终烘焙的目录"里手工加 `/Game/VideoTwinMVP`。

**没有这几行日志就下不了结论。** 画面上"没有视频"有好几种原因,只有日志能分清。

## 三、目标机上装转换服务

UE 的播放器打不开摄像头的 RTSP(准确说:Electra 不行;WmfMedia 可以但延迟 5.2 秒)。
现在用的方案是 MediaMTX 把 RTSP 转成 HLS,实测延迟 4.0~4.7 秒。

把下面四个文件放进目标机上的一个文件夹(比如 `D:\VideoTwinLive\`):

```
mediamtx.exe                  1.21.0 版。不在仓库里(56 MB),从开发机 Saved\VideoTwinMVP\live\ 拷
mediamtx.videotwin.yml        仓库 Tools\VideoTwin\
start_live.ps1                仓库 Tools\VideoTwin\
install_live.ps1              仓库 Tools\VideoTwin\
```

用**将来运行 UE 的那个 Windows 账号**登录,在这个文件夹里执行:

```
powershell -ExecutionPolicy Bypass -File install_live.ps1
```

它会:问一次摄像头口令并存成该用户的环境变量 `OFFICE_CAM_PASSWORD`(不写进任何文件);
登记一个登录时自启动的计划任务 `VideoTwinLive`;立即启动并报告每台摄像头连上了没有。

卸载:`schtasks /Delete /TN VideoTwinLive /F`

### 两条安全相关的设定,不要改回去

- 配置里 `rtsp: no`、`moq: no`。MediaMTX 1.21 默认还会在**所有网卡**上开 RTSP 服务(8554、8000、8001)
  和 MoQ(8892、8893),等于把摄像头画面挂到局域网上。已关闭,核对过进程只监听 `127.0.0.1:8888`。
- HLS 只绑 `127.0.0.1`。转换服务和 UE 在同一台机器,不需要对外。画面里有可辨认的同事。

## 四、录像文件要不要带

`Content/Movies/CameraNN_Real.mp4` 是 10 月 10 日录的真实画面,用作**实时流连不上时的回退**。

- 它们不在 git 里(被本机的 `.git/info/exclude` 挡着),但只要文件在 `Content/Movies/` 下,打包时**很可能会被原样带进包**。
- 里面有可辨认的同事。包会被拷到别的机器,**带不带是需要有人拍板的事**。
- 不带的话:实时流连不上时,该相机显示一张静止的参考图,状态栏显示无画面。

打包前如果决定不带,把这几个文件临时挪出 `Content/Movies/`。

## 五、目标机是 3080:帧率没有测过

到目前为止所有验证都在开发机(RTX 5090)上,用的是后台无头渲染出截图,**帧率一次都没量过**。
每台相机会带来:一次全屏后处理、一张每 3 秒重渲染一次的深度图、一路视频解码。
真实主码流是 2560×1440、每秒 25 帧,而到目前为止测的都是 1280×720、每秒 10 帧的录像。

所以到了目标机,第一件事是开着三台相机看帧率。如果不够,按这个顺序减:

1. 深度图改成只在启动时渲染一次(办公室家具基本不动)。改 `DepthRefreshSeconds`。
2. 在 `calibration.json` 的 `enabled_cameras` 里减相机。
3. 让摄像头输出更低的分辨率或帧率。

## 六、接上真摄像头后可能出问题的三处

这三处只有连上真摄像头才能知道,开发机所在的网络连不到摄像头:

1. **编码格式**。主码流是 H.264 还是 H.265 没有查证过。
2. **关键帧间隔**。切片必须从关键帧开始;如果摄像头是 2 秒一个关键帧,延迟会从 4 秒多变成 7 秒以上。
3. **三路高分辨率同时解码**的开销。

判断方法:状态栏每台相机后面会标 `实时` / `录像` / `录像(实时连不上)`。
出问题时看两份日志:转换服务文件夹里的 `mediamtx.log`,和 UE 的 `Saved/Logs`。
