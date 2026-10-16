# 办公室三维孪生 · 224.7 现场部署与运维

更新于 2026-10-16,对应现场正在运行的包 `20261016h`。打包、标定、各种现场结论见 [VideoTwin_Packaging.md](VideoTwin_Packaging.md);
本文只讲**现场这套东西装在哪、怎么起停、坏了先看哪**。

## 一、整体链路

```
摄像头 192.168.229.x:554(17 号主码流 2560×1440,其余子码流 720×576)
      │ RTSP,UE 直接拉(口令取自环境变量)
      ▼
192.168.224.7  Windows 11 · RTX 3080 Ti · 用户 ssdgj
  ├─ Docker:OntoTwin 后台 + 数据库 + 图库(127.0.0.1:15481)
  ├─ 三维场景(UE Shipping,一键包)  ←─ 读后台数据、拉摄像头、投影融合
  ├─ ue-cast:抓屏 → H.264 → 推到 224.4
  └─ ue-input:平板页面 + 把平板的点击/拖动转成键鼠(端口 8099)
      │ rtsp://192.168.224.4:8554/ue-office
      ▼
192.168.224.4  银河麒麟中控机:MediaMTX 转发 + 投屏中控(systemd,开机自启)
      │ WebRTC http://192.168.224.4:8889/ue-office/whep
      ▼
平板浏览器打开 http://192.168.224.7:8099/
```

224.4 那一侧的安装和起停见 metaverse 仓库 `screen-control/deploy/部署手册.md`,本文不重复。

## 二、能不能开机自启

**结论:登录后全自动;从按电源键到登录这一步目前要人来做。**

| 阶段 | 现状 | 说明 |
|---|---|---|
| 通电 → 开机 | 未确认 | 停电恢复后是否自动开机由 BIOS 的 "AC Power Recovery / Restore on AC Power Loss" 决定,远程看不到,要到机器前进 BIOS 设成 Power On |
| 开机 → 登录桌面 | **不自动** | 没配自动登录(`AutoAdminLogon` 为空),开机停在登录界面,下面全部不会启动 |
| 登录 → 后台 | 自动 | Docker Desktop 在用户的启动项里;三个容器的重启策略是 `unless-stopped`,Docker 起来它们就起来 |
| 登录 → 三维场景 | **自动(2026-10-16 新加)** | 计划任务 `OfficeTwinAutoStart`:登录 30 秒后运行一键包的 `启动大零号湾.exe`。它会等 Docker 就绪(最多 5 分钟)、拉起后台、再开场景,场景就绪后自动全屏 |
| 登录 → 平板投屏 | 自动 | 计划任务 `ue-cast`,一直等到场景窗口出现才开始推流 |
| 登录 → 平板操作 | 自动 | 计划任务 `ue-input` |
| 登录 → 转换服务 | 自动(已不用) | 计划任务 `VideoTwinLive`(MediaMTX,127.0.0.1:8888)。现在 UE 直接拉 RTSP,它只是旧路线的备用,开着无害 |

### 要做到"通电即可用",还差两步(需到现场)

1. **配自动登录**(需要 ssdgj 的 Windows 登录口令,所以没替你做)。推荐用微软的 Sysinternals **Autologon**:
   运行后填用户名 `ssdgj`、域 `SIMIDA_WORK`、口令,点 Enable。口令以加密形式存进系统,不是明文写注册表。
   也可以 `Win+R` → `netplwiz` 取消勾选"要使用本计算机,用户必须输入用户名和密码";Windows 11 看不到这个勾选框时,
   先到"设置 → 账户 → 登录选项"关掉"为了提高安全性,仅允许 Windows Hello 登录"。
   取消自动登录:再运行 Autologon 点 Disable。
2. **BIOS 设置停电恢复后自动开机**(见上表第一行)。

### 自启的注意事项

- **屏幕不能锁。** 投屏是抓桌面画面,锁屏(Win+L、屏保要求密码、远程软件断开时锁屏)后平板画面就停住。
  现在电源计划是"从不睡眠、从不关屏",屏保没设口令,保持这样。远程软件(ToDesk、向日葵)断开时注意别勾"断开后锁屏"。
- **场景窗口别最小化。** 最小化后抓不到画面;ue-cast 会自己尝试把它还原。
- **Windows 更新会自动重启。** 重启后若没配自动登录,就停在登录界面。建议在"设置 → Windows 更新 → 使用时间"里把演示时段设进去。
- **视频融合开机默认是关的**(一键包启动参数带 `-VideoTwinStartOff`),进场景后按 `Z` 打开。
- 这台机器同时是工作机,开机还会自动起 Steam、Epic、飞书、百度网盘、豆包等一堆程序,和场景抢 GPU/内存。演示机最好在任务管理器"启动应用"里关掉。
- 关掉自启:`schtasks /Change /TN OfficeTwinAutoStart /DISABLE`(恢复用 `/ENABLE`);
  删除:`powershell -ExecutionPolicy Bypass -File <一键包>\VideoTwinLive\register_autostart.ps1 -Remove`。
  重新登记(比如一键包换了目录)用同一个脚本不带参数跑一次。

## 三、东西放在哪

一键包(下文简称 `<一键包>`):
`D:\BaiduNetdiskDownload\New folder\大零号湾_OntoTwin_一键运行_20261012\大零号湾_OntoTwin_一键运行_20261012\`

| 位置 | 内容 |
|---|---|
| `<一键包>\启动大零号湾.exe` | 启动入口(双击或计划任务都走它),实际执行 `Start-Runtime.ps1` |
| `<一键包>\停止后台.cmd`、`后台状态.cmd` | 停后台 / 看后台容器状态 |
| `<一键包>\Runtime\` | 当前运行的场景包(20261016h) |
| `<一键包>\Runtime.bak-<时间>\` | 每次换包前的旧包,时间是换包那一刻。最新的一个就是上一版 |
| `<一键包>\Runtime\ShanghaiOfficeShell_OpenSteel\Content\VideoTwin\calibration.json` | 摄像头标定、显示范围、全屏等开关。**改完重启场景生效**。旁边的 `calibration.json.bak-*` 是改之前的备份 |
| `<一键包>\VideoTwinLive\Engine.videotwin.ini` | 渲染设置(防视频抖动)。每次启动前被复制成用户 Engine.ini,**要改渲染设置改这里** |
| `<一键包>\VideoTwinLive\register_autostart.ps1` | 登记/删除开机自启任务 |
| `<一键包>\Platform\.env` | 后台端口和容器项目名(`ontotwin-office-c12fc7d3cf77`)。**别删**,删了下次会新建一套空库 |
| `<一键包>\Logs\launch-*.log` | 每次启动的过程记录,`@@STATUS` 是进度、`@@ERROR` 是失败原因 |
| `C:\ue-cast\` | 投屏和平板操作:`ue-cast.ps1`、`ue-cast-ctl.ps1`、`ue-input.ps1`、`ue-input.html`、`ffmpeg.exe`,日志 `ue-cast.log`、`ue-input.log` |
| 用户环境变量 `OFFICE_CAM_PASSWORD` | 摄像头口令。只存在这里,不在任何文件里 |

源码在同一台机器 `D:\BaiduNetdiskDownload\New folder\UE0bay\OfficeShell`(分支 `feat/videotwin-onsite-1015`),
投屏/平板脚本的源码在 metaverse 仓库 `tools/`。

### 进程和端口

| 进程 | 端口 | 谁拉起 |
|---|---|---|
| `OfficeHost.exe` → `OfficeHost-Win64-Shipping.exe` | — | 一键包 |
| `com.docker.backend`(后台容器) | 127.0.0.1:15481 | Docker Desktop |
| `powershell ... ue-cast.ps1` + `ffmpeg.exe` | 出站到 224.4:8554 | 计划任务 `ue-cast`(进程停了 1 分钟内自动重试,最多 3 次) |
| `powershell ... ue-input.ps1` | 0.0.0.0:8099(显示为 System) | 计划任务 `ue-input` |
| `mediamtx.exe` | 127.0.0.1:8888 | 计划任务 `VideoTwinLive`(备用) |

另外机器上还有别的项目的容器(`ontotwin-ue-*` 等)和别人开的 UE 编辑器,**都不要动**。只关路径在 `<一键包>` 下的 `OfficeHost*`。

## 四、手动操作

以下在 224.7 桌面上操作;远程用 SSH(`ssdgj@192.168.224.7`,部署密钥)时见每条的"远程"写法。

### 启动

- 双击 `<一键包>\启动大零号湾.exe`,等进度窗口走完。冷启动(Docker 也没起)要 1~5 分钟,热启动约 1 分钟。场景就绪后自动全屏。
- 远程:`schtasks /Run /TN OfficeTwinAutoStart`。**不要在 SSH 里直接运行 exe**:SSH 会话没有桌面,场景起不来或看不见,会话一断进程也会被杀。
- 已经在运行时再启动,只会提示"大零号湾已经运行",不会开第二个。

### 关闭 / 重启场景

- 桌面上:场景里 `Alt+F4`。
- 远程(只关这个包的场景,不碰别的 UE):
  ```powershell
  $kit = '<一键包>'
  Get-CimInstance Win32_Process | Where-Object { $_.Name -like 'OfficeHost*' -and $_.ExecutablePath -like "$kit*" } |
      ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
  ```
  关完若残留 `TracerWebHelper.exe`(路径在 `<一键包>\Runtime` 下),一并结束,否则它锁住 Runtime 目录、换包会失败。
- 重启 = 关闭 + 启动。改了 `calibration.json`、`Engine.videotwin.ini`、摄像头口令之后都要重启场景。

### 后台

- 看状态:双击 `后台状态.cmd`(三个容器都应是 running / healthy)。
- 停后台:先关场景,再双击 `停止后台.cmd`。数据保留,下次启动自动拉起。
- 后台一般不需要单独操作:启动场景时会顺带检查并拉起。

### 平板投屏 / 操作

- 投屏状态、启停(在 224.7 上,PowerShell):
  ```powershell
  powershell -ExecutionPolicy Bypass -File C:\ue-cast\ue-cast-ctl.ps1            # 看状态
  powershell -ExecutionPolicy Bypass -File C:\ue-cast\ue-cast-ctl.ps1 -Restart   # 重启推流
  ```
- 平板操作服务重启:`schtasks /End /TN ue-input` 然后 `schtasks /Run /TN ue-input`。
- 平板播放统计每 10 秒写进 `C:\ue-cast\ue-input.log` 的 `stats` 行(帧率、丢帧、卡顿次数),排查平板卡顿先看它。

### 场景里的按键

`Z` 开关视频融合,`X` 逐台切到摄像头视角(切完等 7~9 秒过渡),`C` 复位视角,`F11` 全屏/窗口切换,
`WASD` 移动、右键观察、`V` 切视角、`Tab` 漫游面板。

### 换摄像头口令

```powershell
[Environment]::SetEnvironmentVariable('OFFICE_CAM_PASSWORD', '<新口令>', 'User')
```
然后重启场景。用计划任务(或远程 `schtasks /Run`)启动的会拿到新值;从桌面双击启动的,保险起见先注销重新登录一次
(资源管理器缓存着旧的环境变量)。口令不要写进任何文件或文档。

### 回退到上一版场景包

1. 关闭场景(见上)。
2. 把 `Runtime` 改名为 `Runtime.bad-<时间>`,把要回退的 `Runtime.bak-<时间>` 改名为 `Runtime`。
3. 启动。

现在跑的是 20261016h。`Runtime.rejected-*` 是试过又退下来的包(i、j,见 VideoTwin_Packaging.md 第八节第 13 条),可以删。
最近几个备份:`Runtime.bak-20261016-050745` 是 20261016h 之前的那一版;`Runtime.bak-20261015-094305` 是
改成 UE 直接拉 RTSP 之前的最后一版(走 HLS,延迟约 4 秒,需要 `VideoTwinLive` 转换服务在跑)。
备份目录现在有 20 个,每个都是整包,磁盘紧张时可以删掉旧的,至少留最近两个和 094305。

## 五、出问题先看哪

| 现象 | 先查 | 处理 |
|---|---|---|
| 开机后什么都没起来 | 是不是停在登录界面 | 登录即可;要免登录见第二节 |
| 启动窗口报错 | `<一键包>\Logs\` 最新 `launch-*.log` 里的 `@@ERROR` | Docker 没就绪:打开 Docker Desktop 等它变绿再启动;端口被占:按提示关掉占用程序 |
| 场景里桌椅是棋盘格方块 / 没有设备数据 | `后台状态.cmd` | 后台没起来:关场景,重新启动 |
| 某台摄像头显示的是录像不是实时 | calibration.json 里 `show_status` 改 `true`、重启,左上角会写每路是"实时"还是"录像" | 摄像头不通(从 224.7 `Test-NetConnection 192.168.229.x -Port 554`)、口令变了(见"换摄像头口令")。断流后程序每 2 秒自动重连,画面回来自动切回实时 |
| 所有摄像头都是录像 | `OFFICE_CAM_PASSWORD` 是否存在 | 没设口令时一键包直接用录像 |
| 投影视频在抖 / 上下颠 | `Engine.videotwin.ini` 还在不在 | 被删或被改回去了:按 VideoTwin_Packaging.md 第二节那三行恢复后重启场景 |
| 平板黑屏 / 画面不动 | 场景是否在运行、是否最小化、桌面是否锁屏;`ue-cast-ctl.ps1` 状态 | `ue-cast-ctl.ps1 -Restart`;224.4 那边见它的部署手册 |
| 平板画面动但点不动 | `ue-input` 任务状态、`ue-input.log` | 重启 `ue-input` |
| 平板周期性卡一下 | `ue-input.log` 的 `stats` 行 | 投屏参数和卡顿排查见 VideoTwin_Packaging.md 第五节、第八节,别随便改 `ue-cast` 任务参数 |
| 场景卡顿、帧率低 | 是否有别的程序在占 GPU(另开的 UE 编辑器、游戏、AI 客户端);`nvidia-smi` 看显存,接近 12 GB 就会卡(2026-10-16 一个另开的编辑器占了 3.7 GB) | 关掉它们;场景本身的性能设置见 VideoTwin_Packaging.md |
| 换包后场景起不来 | 是否残留 `TracerWebHelper.exe` 锁住目录 | 结束它再换;还不行就按第四节回退 |

Shipping 包不写 UE 自己的日志,场景内部的问题只能靠状态栏(`show_status`)和截图判断。

## 六、计划任务一览

| 任务名 | 触发 | 动作 |
|---|---|---|
| `OfficeTwinAutoStart` | ssdgj 登录后 30 秒 | `<一键包>\启动大零号湾.exe` |
| `ue-cast` | ssdgj 登录 | `C:\ue-cast\ue-cast.ps1 -Start -Method dda -Framerate 25 -GopSeconds 0.5 -Crf 18 -Bitrate 8M -BufSize 1M -Size 1600x900 ...`,推到 `rtsp://192.168.224.4:8554/ue-office` |
| `ue-input` | ssdgj 登录 | `C:\ue-cast\ue-input.ps1 -Port 8099 -LookScale 1.5` |
| `VideoTwinLive` | ssdgj 登录 | `<一键包>\VideoTwinLive\start_live.ps1`(备用转换服务) |

全部以 ssdgj 的交互会话运行(场景和抓屏都需要桌面),所以都依赖"有人登录"。
查看:`Get-ScheduledTask -TaskName OfficeTwinAutoStart,ue-cast,ue-input,VideoTwinLive | Get-ScheduledTaskInfo`。
