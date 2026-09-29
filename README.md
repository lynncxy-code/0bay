# Office VR

把上海办公室场景跑到 **Pico 4 Ultra** 上：能走动，能用射线查看桌子、椅子、电视等物体的属性。

当前一期走 **PC 串流**（PICO Connect）。渲染在 Windows 上，画面投到头显。还没有独立 APK。

唯一工程目录：

```text
F:\Office_VR
```

不要再用 `D:\UE56`。打开 Cursor 或 Unreal，都从这一层进。

---

## 环境要求

| 项 | 要求 |
|----|------|
| 引擎 | Unreal Engine **5.6.1**（本机路径示例：`D:\UE_5.6`） |
| 工程文件 | `F:\Office_VR\ShanghaiOfficeShell_OpenSteel.uproject` |
| 头显 | Pico 4 Ultra，系统 **≥ 5.14** |
| 串流 | PC 安装 **PICO Connect**（没有企业 Business Streaming） |
| 网络 | 头显和 PC 同一局域网，建议 Wi‑Fi 6/7 或有线 |

---

## 1. 启动这个文件夹

1. 用 Cursor / VS Code 打开 **`F:\Office_VR`**（不要打开它的子目录）。
2. 确认能看到 `ShanghaiOfficeShell_OpenSteel.uproject`、`Content`、`Source`、本 README。

---

## 2. 按步骤打开 UE 工程

### 第一次（或换电脑后）

1. 确认已安装 UE **5.6.1**。
2. 任选一种方式打开工程：
   - 双击 `F:\Office_VR\ShanghaiOfficeShell_OpenSteel.uproject`
   - 或双击 `F:\Office_VR\StartEditor.bat`（会调用 `D:\UE_5.6\Engine\Binaries\Win64\UnrealEditor.exe`；若引擎不在这个路径，改用双击 `.uproject`）
   - 或打开 Epic / 引擎启动器，选 5.6，再 Browse 到上述 `.uproject`
3. 若提示编译 C++ / Missing Modules，选 **Yes**，等 `OfficeHost` 编完。
4. 编辑器打开后，在 Content Browser 进入：
   `Content / OfficeShell / Maps / Office_Demo`
5. 双击打开 **`Office_Demo`**。这是主关卡，出生点按公司大门附近的 `PlayerStart`。
6. 看窗口左上角或 World Settings：GameMode 应为 **`OfficeVRGameMode`**。
   - 若 Play 进去仍是旧的上帝视角摄像机，打开 **Output Log**（Window → Developer Tools → Output Log），执行：
     ```text
     py F:/Office_VR/Scripts/SetupOfficeVR.py
     ```
   - 门口附近应有两个占位人 **Agent 0 / Agent 1**（World Outliner 可搜）。人的位置已按现场固定，不要再跑摆人脚本去挪他们。
   - 等日志出现 `OfficeVR: GameMode -> OfficeVRGameMode`，然后 **保存关卡**（Ctrl+S）。

### 无头显先验证（编辑器里）

1. 点工具栏 **Play**（不必 VR Preview）。
2. 用键盘鼠标确认能走、能点（按键见下一节「键盘」）。
3. 门口附近应有一块「操作说明」；对准后左键可弹出属性。

### Pico 串流进场景

1. 先完全退出 Unreal。
2. PC 上 **PICO Connect** 已连上头显（头显电量数字在跳即可）。
3. 头显里不要停在「电脑桌面镜像」。桌面串流只能看显示器，不能做 VR Preview。
4. 双击 `F:\Office_VR\StartEditor.bat`（会关掉卡死的 SteamVR，并用 PICO 自己的 OpenXR）。
5. 打开 `Office_Demo`，点 Play 旁下拉，选 **VR Preview**。
6. 若弹出切换 GPU：点 **取消**，不要点确定。点确定会让编辑器重启报错。
7. 戴上头显应看到办公室。若 Steam 仍弹出房间设置，关掉窗口即可，不要填定位器。

串流延迟高时，可在 PICO Connect 里先降画质保流畅。工程默认保持 Nanite / Lumen / 虚拟阴影（`officevr.LowGI 0`）。本机 3080 Ti 12GB 在 VR Preview 下会显存不足，见常见问题。

---

## 3. Pico 按键（Pico 4 Ultra）

串流时 Pico 手柄按 OpenXR 映射，对应下面这套（与 Quest/Oculus Touch 布局相同）。

**左手柄**

| 按键 | 作用 |
|------|------|
| 摇杆 | 按身体朝向行走（扭头不改方向） |
| **Y** | 按住和数字人说话，松开后出字并朗读。接通前松开要再按一次，面板会提示 |
| 摇杆按下、X、菜单、握把、扳机 | 一期不用 |

**右手柄**

| 按键 | 作用 |
|------|------|
| 摇杆左右 | 连续转身体，可无限转圈，松杆即停 |
| 摇杆下推不放 | 进入瞬移瞄准；地面出现标记后 **松开** 瞬移 |
| 食指扳机 | 射线对准可查看物体时，弹出属性面板 |
| **B**（靠上那颗，靠近摇杆） | 关闭属性面板；正在和数字人说话时结束这次会话 |
| A、握把 | 一期不用 |

**射线**

- 从右手 **Aim** 向前射出（应对准手柄指尖，不应再出现旁边错位的灰线）。
- 对准可查看物体时射线变绿；对准占位 Agent 时变橙，扳机不打开家具面板。
- 先对准，再扣扳机。

**属性面板字段**

名称、类别、区域、尺寸、材质、资产编号、所属系统、维保周期、下次维保、备注。  
维保目前是示例数据。

---

## 4. 键盘（没有头显时）

| 按键 | 作用 |
|------|------|
| WASD / 方向键 | 走 |
| Q / E | 按住连续左转 / 右转 |
| 鼠标左键 | 查看属性（用屏幕中央/右手射线瞄准） |
| 鼠标右键、B、Esc | 关闭面板；若正在和数字人说话，则结束这次会话 |
| Y | 对准人后按住说话，松开后出字并朗读 |
| T | 按住瞄准瞬移，松开执行 |

手柄插在 PC 上时，也可用手柄左摇杆走、右摇杆转/瞬移、右侧面键关面板。

---

## 5. 和门口的数字人说话

语音只在 Unreal 里。先双击仓库根目录的 `StartOfficeVR.bat`（会拉起语音边车），再打开 `Office_Demo` 做 VR Preview。浏览器里的 `OfficeWebXR` 不能对话，页面上会写明这一点。

1. 右手射线对准门口的人，线变成橙色。扳机不会打开家具面板。
2. 按住左手 **Y**（没有头显时按键盘 **Y**），不要松开，直到面前面板出现「正在听」。
3. 说完再松开。面板先显示你的话，再显示回复。人头旁边出字，并朗读。
4. 接通还没完成就松开时，这次不会录音。面板会写「刚才松开太早，这次没有录音。请再按住左手 Y」。再按住说一次即可。
5. 按 **B**（键盘 B、Esc 或鼠标右键）结束当前这个人。要换另一个人，先结束，再把射线对准下一个人。

没有中文语音包时，回复文字仍会显示，面板会写「未安装中文语音包，将用系统默认声音朗读」。语音文件取不到或不是 16-bit PCM 时，面板写「没有声音」和原因，回复文字保留。

中文语音在 Windows 设置里加：时间和语言 → 语音 → 添加语音，选中文。

---

## 6. 常见问题

**Play 进去不是 VR，还是飞天摄像机**  
关卡 GameMode 没换成 `OfficeVRGameMode`。按第 2 节跑 `SetupOfficeVR.py` 并保存关卡。

**头显里弹出「正在自动重试 / 模型加载」**  
那是 OntoTwin 在连 `localhost:5000`，办公室 VR 不需要这个后端。关掉编辑器再重新编译打开后，OfficeVR 会跳过这块界面。

**能看见远处有一条蓝色指向线，自己却不在第一人称里**  
OntoTwin 的上帝视角摄像机抢走了镜头，人还在门口，射线在远处。同样需要重新编译；进去后应是头显第一人称，射线只在自己右手前方。

**VR Preview 弹出切换 GPU / 点确定后报错**  
这不是显卡没锁住。编辑器已经在 RTX 3080 Ti 上。SteamVR 合成器死后，OpenXR 读头显超时，就会谎报要换 GPU。点确定重启会直接报错。  
关掉 Unreal，再用 `StartEditor.bat` 打开（脚本会杀掉卡死的 `vrserver`）。弹窗一律点 **取消**。

**扳机没反应**  
先对准变绿的物体再扣。建筑墙体、地板默认不能点。能点的是家具（桌椅沙发电视等）和门口说明板。

**进 VR Preview 报 Out of video memory**  
这是显存不够，不是工程坏了。办公室原画质（Nanite + Lumen + 虚拟阴影 + 头显原分辨率）在编辑器 VR Preview 下，12GB 卡会爆。需要的机器见下面「设备」。  
临时保命可在控制台执行 `officevr.LowGI 2`，这不是推荐画质。

**太暗或太卡**  
控制台（\` 键）：
- `officevr.LowGI 0` 工程原光照（推荐观感）
- `officevr.LowGI 1` 降低 Lumen
- `officevr.LowGI 2` 关掉 Lumen 和额外阴影

**Ctrl+Alt+F11 报 LNK2011 / 路径还是 D:\UE56**  
这是 Live Coding，不是正常编译。工程已经搬到 `F:\Office_VR`，热补丁仍会去旧目录链接，必定失败。  
关掉 Unreal 编辑器（不要只按热键），再双击 `StartEditor.bat` 或 `.uproject`，让它完整编一次。不要打开 `D:\UE56`。

**StartEditor.bat 打不开**  
说明本机 UE 不在 `D:\UE_5.6`。改为双击 `.uproject`，让系统关联的 5.6 打开。

---

## 7. 更多文档

| 文件 | 内容 |
|------|------|
| `Pico4Ultra-UE-OfficeVR-开发计划.md` | 分阶段计划、技术选型、验收 |
| `Docs/OfficeVR_Phase1.md` | 阶段 1 实现说明 |
| `Docs/OfficeWebXR_开发方案.md` | 网页 WebXR + PICO Connect 串流 |
| `Docs/OfficeVR_Agent展示_开发方案.md` | 阶段性最终版：宿主 + 2 个占位人；凌云智仓是网页业务大脑（进程外 Sidecar）；Pico 麦经 Connect 虚拟设备做语音；连续转身体 / 修错位灰线；展示 1080p / 头显现版 |
| `Docs/调研_Pico4Ultra_语音交互.md` | 为何不用 Pico Speech SDK；Connect 虚拟麦 + Sidecar ASR/TTS 为最可行路径 |
| `Docs/调研_YouTube_UE56_社区DLSS5.md` | 社区视频：ReShade 注入 UE 5.6 编辑器做 DLSS 5（非官方，不进交付） |
| `OfficeWebXR/README.md` | 灰盒网页启动与 Pico 操作 |

一期范围：走动 + 点选属性。不做抓取装配、不做独立 APK。

---

## 8. 办公室 WebXR（当前 12GB 卡的网页路径）

UE VR Preview 仍可用，但 3080 Ti 12GB 容易显存不足。网页路径在 `OfficeWebXR/`：灰盒或烘焙场景 + Edge WebXR。头显出画用 SteamVR（PICO Connect 当驱动）；UE Preview 仍用 `StartEditor.bat`。网页里不能和数字人说话，语音仍走上一节的 Unreal。

1. **不要**和 Unreal VR Preview 同时开。
2. 双击 `OfficeWebXR/start.cmd`，等 SteamVR 认出头显，再用新开的 Edge 点「进入 VR」。
3. 无 `architecture.glb` 时先测灰盒。
4. 从 UE 导出正式场景：Output Log 执行 `py F:/Office_VR/Scripts/ExportOfficeWebXR.py`（Nanite 未关会先写出 `OfficeWebXR/data/nanite-blockers.json`）。

完整方案见 `Docs/OfficeWebXR_开发方案.md`。
