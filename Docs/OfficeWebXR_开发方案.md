# 办公室 WebXR（网页版 + PC 串流）开发方案

> 对应路线：从 UE 办公室导出烘焙场景 → 浏览器 Three.js 第一人称 → Chrome WebXR → PICO Connect OpenXR 串流到 Pico 4 Ultra。  
> 不改 `OfficeVRPawn` 作为运行时；UE 工程只作为场景与家具数据的源。  
> 实现时按任务逐项推进；本文件是唯一开发依据。

**Goal:** 在当前 3080 Ti 12GB PC 上，用浏览器 WebXR 跑办公室第一人称，Pico 手柄操作与现有 `OfficeVRPawn` 一致（左摇杆走、右摇杆 45° 转向、右摇杆下推瞬移、右手扳机查看属性、B 关面板）。

**Architecture:** UE 5.6.1 `Office_Demo` 导出为标准 glTF + Lightmap + 可点选目录；独立 Node 静态站用 Three.js 加载。桌面模式先验证漫游与点选，再开 `immersive-vr`。渲染在 PC Chrome，经本机已在用的 PICO Connect OpenXR 运行时进头显。不走 Pico 自带浏览器，不串流「桌面上的网页窗口」。

**Tech Stack:** Unreal 5.6.1（导出与烘焙）· Python Editor 脚本 · glTF 2.0 + PNG Lightmap · Node.js 静态服务 · Three.js r185（与展厅预览包同代）· WebXR Device API · Chrome · PICO Connect 10.x OpenXR · Pico 4 Ultra / Pico OS ≥ 5.14

## Global Constraints

- 交付通道：PC Chrome WebXR + PICO Connect 串流；禁止 Pico 头显浏览器本地渲染全场景。
- OpenXR 运行时与 `StartEditor.bat` 一致：`picostreaming-openxr.json`（PICO Connect），不要切回 SteamVR。
- 操作必须对齐 `Source/OfficeHost/OfficeVR/OfficeVRPawn.cpp`：走速 220、SnapTurn 45° / 冷却 0.35s、激光 1200cm、右摇杆 Y < -0.55 瞄准瞬移、松开落地。
- 属性字段必须对齐 `FInspectableItemData`：名称、类别、区域、尺寸、材质、资产编号、所属系统、维保周期、下次维保、备注。
- 家具数据源：`Content/Art/家具_PDF匹配/完整清单.json`；墙体/地板默认不可点选。
- 一期不做：抓取装配、独立 APK、OntoTwin 后端、人物巡游、路线解说、小地图、F7/F10 编辑、双屏视频。
- 不把 `ZHHZ_NEW-Windows-Preview` 改成办公室包；只允许复用 Lightmap/合批思路，运行时新建 `OfficeWebXR/`。
- 光照：运行时 0 盏动态灯，全部进 Lightmap（展厅「运行时灯 0」同一档）。
- 目标机：RTX 3080 Ti 12GB；VR 双目默认 `framebufferScaleFactor=0.85`，OOM 时降到 0.7，不恢复 Lumen/VSM。
- WebXR 仅 `localhost`；HTTP 即可（Chrome 对 localhost 放行 secure context）。
- `OfficeVRPawn` / `OfficeVRGameMode` 保留给原 UE 串流路径，本方案不删除、不作为网页运行时。

---

## 1. 背景与范围

办公室 UE 路径已经具备 Pico 操作，但 Nanite + Lumen + 虚拟阴影 + 头显双目在 12GB 上 OOM。展厅 `zhhz-new.html` 证明：烘焙 Lightmap + WebGL 可以在相近机器上以第一人称走动、点选物体。

本方案把这两件事接起来：

1. 把办公室从 UE 做成「和展厅同类」的网页场景（烘焙、第一人称）。
2. 给这个网页补上 WebXR，让 Pico 手柄进场景，而不是把显示器画面挂在 VR 里。

`zhhz-new.html` 本身没有 WebXR，也不能把 HTML 控制条带进头显。办公室网页必须从第一天就把「世界空间面板 + 手柄射线」列为交付，而不是事后把 DOM 贴上去。

### 1.1 做

| 项 | 说明 |
|----|------|
| 办公室烘焙场景网页 | 建筑 + 家具 glTF，Lightmap，本地点选目录 |
| 桌面第一人称 | WASD / 鼠标，用于无头显调试 |
| WebXR 第一人称 | 头显 6DoF 相机 |
| Pico 手柄 | 与 `OfficeVRPawn` 同一套绑定 |
| 世界空间属性板 | 跟随头显前方，字段与 UE 面板相同 |

### 1.2 不做

- 复制展厅 OntoTwin：人物、路线、字幕、语音、小地图、运行时编辑、Twin 后端。
- Pico 浏览器直开、PICO Connect 桌面镜像当「VR」。
- 把现有 `OfficeVRPawn` 迁进网页（C++ 用不了）；网页重写一份等价输入。
- 追求 Lumen / Nanite 观感。

---

## 2. 系统结构

```
UE 5.6.1  Office_Demo（源）
    │  GPU Lightmass + 导出脚本
    ▼
OfficeWebXR/data/
    architecture.glb
    collision.glb
    lightmaps/*.png
    spawn.json
    inspect-catalog.json
    inspect-bindings.json
    │
    ▼
Node server.mjs  :4193  静态文件
    │
    ▼
Chrome  office.html
    ├─ 桌面模式：Pointer Lock + WASD
    └─ VR 模式：renderer.xr.requestSession("immersive-vr")
            │
            ▼
Windows OpenXR ActiveRuntime = PICO Connect picostreaming-openxr.json
            │
            ▼
PICO Connect → Pico 4 Ultra
```

两个进程边界：

- **导出边界：** UE 写出 `data/` 后，浏览器不再依赖编辑器。
- **呈现边界：** Chrome 负责立体渲染与手柄；PICO Connect 只做 OpenXR 设备。

### 2.1 目录（新建）

```
OfficeWebXR/
  office.html
  office.css
  start.cmd
  server.mjs
  package.json
  src/
    main.js                 入口：渲染循环、桌面/XR 切换
    scene-loader.js         glTF + Lightmap
    catalog.js              加载 inspect-catalog / bindings
    locomotion.js           行走、snap turn、瞬移（桌面与 XR 共用）
    collision.js            地面/胶囊检测
    xr-session.js           WebXR 会话、参考空间 local-floor
    xr-input.js             Pico/oculus-touch 手柄映射
    laser.js                右手射线 + hover 高亮
    inspect-panel.js        世界空间属性板
    desktop-input.js        无头显调试
  tests/
    locomotion.test.mjs
    xr-input.test.mjs
    catalog.test.mjs
  data/                     导出产物，不手改
Scripts/
  ExportOfficeWebXR.py      UE 编辑器内运行
```

展厅文件只作对照，禁止直接改成办公室入口：

- 对照 Lightmap 思路：`ZHHZ_NEW-Windows-Preview/zhhz-new.js`
- 对照第一人称：`ZHHZ_NEW-Windows-Preview/zhhz-twin-runtime.js`（`first_person`）
- 对照静态服务：`ZHHZ_NEW-Windows-Preview/web/server.mjs`

---

## 3. 手柄与面板（必须与 UE 一致）

数据来源：`OfficeVRPawn.cpp` / `OfficeVRPawn.h`。

| 动作 | UE 绑定 | WebXR（oculus-touch / pico 同类） | 桌面调试 |
|------|---------|-----------------------------------|----------|
| 移动 | 左摇杆 XY，头显 yaw 为前方 | `left.gamepad.axes[2,3]`，无则 `[0,1]` | WASD |
| 走速 | 220 cm/s | 同一数值（场景单位 cm） | 同左 |
| 转向 | 右摇杆 \|X\| > 0.7 → ±45°，冷却 0.35s | 同左 | Q/E |
| 瞬移瞄准 | 右摇杆 Y < -0.55 或 T | 同左 | T |
| 瞬移落地 | 松开且地面法线 Z ≥ 0.55 | 同左 | 松开 T |
| 点选 | 右手扳机 Click | `right.gamepad.buttons[0].pressed` 上升沿 | 鼠标左键 |
| 关面板 | 右手 B / Esc | `right.gamepad.buttons[5]`（B） | 右键 / Esc / B |
| 激光 | 右手前向 1200cm，可点物变绿 | 右手 `XRInputSource` 射线 | 屏幕中心或鼠标 |
| 面板位置 | 头显前 (90, 18, -8) cm | 头显相机本地同偏移 | 同左（世界空间板，不要 DOM） |

Pico 在 Chrome 里的 `profiles` 常见为 `oculus-touch` / `generic-trigger-squeeze-thumbstick`。`xr-input.js` 必须按 profile 选轴，单测覆盖两套轴布局。

属性板文案格式与 `FInspectableItemData::ToPanelText` 相同：

```
{名称}

类别    {Category}
区域    {Room}
尺寸    {Dimensions}
材质    {Material}
资产编号  {AssetId}
所属系统  {SystemName}
维保周期  {MaintenanceCycle}
下次维保  {NextMaintenance}

{Description}
```

---

## 4. 导出数据合同

所有 JSON 用 UTF-8。坐标系：glTF 默认 Y-up；加载时 `scene.rotation.x = -Math.PI/2` 或导出时已转成 Three 习惯，**二选一写进 `spawn.json` 的 `gltfUp`**，加载器不得猜测。

### 4.1 `data/spawn.json`

```json
{
  "gltfUp": "z",
  "units": "cm",
  "playerStart": { "x": 0, "y": 0, "z": 92 },
  "capsuleRadius": 34,
  "capsuleHalfHeight": 92,
  "eyeHeightFromFloor": 164,
  "walkSpeed": 220,
  "snapTurnDegrees": 45,
  "laserMaxDistance": 1200
}
```

`playerStart` 从关卡 `PlayerStart` 读。胶囊与 `AOfficeVRPawn` 构造函数一致。

### 4.2 `data/inspect-catalog.json`

由 `完整清单.json` + `InspectCatalog` 内置 101/102/103 合并。每条：

```json
{
  "catalogId": 101,
  "displayName": "会议室电视",
  "category": "电器",
  "room": "接待区 / 会议室",
  "dimensions": "约 122 × 14 × 81 cm",
  "material": "塑料壳体 + 玻璃屏（CC0 场景资产）",
  "assetId": "OFC-101",
  "systemName": "办公家具",
  "maintenanceCycle": "12个月（示例）",
  "nextMaintenance": "2027-03-15（示例）",
  "description": "普通无支架电视，用于会议投屏示意。一期仅支持查看属性。",
  "matchKeys": ["SM_TV_Plain", "TV_Plain_NoStand", "电视"]
}
```

### 4.3 `data/inspect-bindings.json`

网格名 / Actor 名 → `catalogId`。导出脚本用与 `OfficeInspectBinder` 相同的名称匹配。未匹配的家具仍导出网格，但不可点选。

```json
{
  "bindings": [
    { "nodeName": "SM_TV_Plain", "catalogId": 101 }
  ]
}
```

### 4.4 网格

| 文件 | 内容 |
|------|------|
| `architecture.glb` | 建筑 + 家具，第二套 UV 为 Lightmap；可点选节点 `extras.officeInspect = true` |
| `collision.glb` | 仅地面/楼板简化碰撞（可见性关闭）。瞬移与行走用。一期允许比视觉网格粗。 |
| `lightmaps/` | 每套材质一张 PNG（或少量 atlas）。使用 glTF `KHR_materials_unlit` + `lightMap` 或 Three `MeshBasicMaterial.lightMap`。 |

**不复制展厅的逐顶点 UE Lightmap 解码**（`zhhzScale0` 那套）。办公室新导出走标准 UV2 Lightmap，降低运行时复杂度。若某次导出对不上 UV，再单独立项，不作为一期默认。

### 4.5 Nanite

Nanite 网格不能按展厅方式烤传统 Lightmap。导出前：

1. 建筑与要进 Lightmap 的静态物：Nanite 关闭，生成 Lightmap UV，Static/Stationary 灯，GPU Lightmass Build。
2. 仅视觉、不烤 GI 的小件可保持 Nanite，但 **Web 导出必须是普通 Static Mesh**；脚本对 Nanite Actor 报错并列出名单，不得静默跳过。

工程当前 `Config/DefaultEngine.ini`：`r.AllowStaticLighting=False`、`r.DynamicGlobalIlluminationMethod=1`。导出用 **独立 Editor 配置 / 命令行覆盖**，不要把 Lumen 工程默认永久改掉（保留原 UE 串流高配路径）。

建议命令行覆盖（仅导出会话）：

```
-ExecCmds="r.AllowStaticLighting 1; r.DynamicGlobalIlluminationMethod 0; r.ReflectionMethod 2; r.Shadow.Virtual.Enable 0"
```

---

## 5. 运行时模块

### 5.1 渲染

- `WebGLRenderer({ antialias: true, alpha: false })`
- `outputColorSpace = SRGBColorSpace`，`toneMapping = ACESFilmicToneMapping`（与展厅接近）
- `renderer.xr.enabled = true`
- `setAnimationLoop` 统一桌面与 XR（不要 `requestAnimationFrame` 与 XR 两套循环）
- XR 会话：`immersive-vr` + `optionalFeatures: ["local-floor"]`，失败则 `local`
- `framebufferScaleFactor` 默认 0.85

XR 会话期间：

- 禁用 `OrbitControls` / Pointer Lock 对相机的写入。
- 玩家根节点（胶囊）仍由 `locomotion.js` 更新；Three.js XR 相机作为子节点挂在根节点上（`local-floor` 下每帧把 `xr.getCamera()` 的位置加到根偏移）。

推荐玩家图：

```
playerRoot          ← locomotion 写 XZ 与 yaw
  xrOffset          ← 对齐 local-floor
    (XR camera)     ← 由 WebXRManager 更新
    leftPointer
    rightPointer
    laser
    inspectPanel
```

### 5.2 移动（`locomotion.js`）

纯函数，便于单测。单位 cm。

```js
export function snapTurnYaw(currentYaw, axisX, cooldownLeft, dt, degrees = 45, threshold = 0.7, cooldown = 0.35) {
  const nextCooldown = Math.max(0, cooldownLeft - dt);
  if (nextCooldown > 0 || Math.abs(axisX) <= threshold) {
    return { yaw: currentYaw, cooldown: nextCooldown, didSnap: false };
  }
  const yaw = currentYaw + Math.sign(axisX) * degrees * Math.PI / 180;
  return { yaw, cooldown, didSnap: true };
}

export function moveOffset(yaw, axisX, axisY, walkSpeed, dt) {
  const forwardX = Math.sin(yaw);
  const forwardZ = Math.cos(yaw);
  const rightX = Math.cos(yaw);
  const rightZ = -Math.sin(yaw);
  return {
    x: (forwardX * axisY + rightX * axisX) * walkSpeed * dt,
    z: (forwardZ * axisY + rightZ * axisX) * walkSpeed * dt,
  };
}

export function teleportAimActive(rightAxisY, keyHeld) {
  return rightAxisY < -0.55 || keyHeld;
}
```

行走：水平移动后对 `collision.glb` 做向下 ray，贴地；撞墙用胶囊 vs 网格。一期碰撞失败则允许穿墙但 **瞬移必须有合法地面**，否则与 UE 不一致。

### 5.3 手柄（`xr-input.js`）

```js
export function readThumbstick(gamepad, hand) {
  if (!gamepad || !gamepad.axes) return { x: 0, y: 0 };
  if (gamepad.axes.length >= 4) return { x: gamepad.axes[2] || 0, y: gamepad.axes[3] || 0 };
  return { x: gamepad.axes[0] || 0, y: gamepad.axes[1] || 0 };
}

export function isTriggerClicked(prev, next) {
  const was = prev?.buttons?.[0]?.pressed === true;
  const now = next?.buttons?.[0]?.pressed === true;
  return !was && now;
}

export function isBClicked(prev, next) {
  const was = prev?.buttons?.[5]?.pressed === true;
  const now = next?.buttons?.[5]?.pressed === true;
  return !was && now;
}
```

右手射线：`renderer.xr.getController(1)`（或按 `inputSource.handedness === "right"` 查找，禁止写死 index）。`laser.js` 用 `Raycaster`，只命中 `userData.inspectable === true` 的网格；墙地 `layers` 排除。Hover 用 emissive 或 saturation 提高模拟 UE Custom Depth 变绿。

### 5.4 点选面板

`inspect-panel.js` 用 `CanvasTexture` + 平面，不要 `Html`/`CSS2D`。显示/隐藏与 UE 相同：扳机打开当前 hover，B 关闭。打开时面板在头显前方固定本地坐标，不钉在家具上（与 `PanelRoot` 挂 VRCamera 一致）。

### 5.5 桌面模式

无会话时：`desktop-input.js` 提供 Pointer Lock、鼠标观察、WASD、左键点选、右键关面板。用于导出验收和 CI 截图，不替代 Pico 验收。

「进入 VR」按钮：`navigator.xr.isSessionSupported("immersive-vr")` 为真才显示。用户手势触发 `requestSession`。

---

## 6. 串流与浏览器

1. Pico 开 PICO Connect，确认 PC 已连接。
2. **WebXR（Edge）** 在本机实测需要 SteamVR 作为 OpenXR 运行时，PICO Connect 继续连着当 SteamVR 驱动。`OfficeWebXR/start.cmd` 会写入 `steamxr_win64.json` 并启动 SteamVR。
3. **Unreal VR Preview** 仍用 `StartEditor.bat`：`picostreaming-openxr.json`，并关掉 SteamVR。两套不要同时开。
4. 用 `start.cmd` 打开的专用 Edge 打开 `http://127.0.0.1:4193/office.html`（普通标签页往往没有 immersive-vr）。
5. 点「进入 VR」。头显应进立体办公室，而不是浏览器窗口的平面投影。

`start.cmd` 负责：设 OpenXR 注册表、杀 SteamVR 进程、启 Node、打开 Chrome。

若 Chrome 报 VR not found：先用 [WebXR 设备报告](https://immersive-web.github.io/webxr-samples/report/) 确认 `immersive-vr`。仍失败则检查 Connect 版本与 Chrome 是否走系统 OpenXR（不要 Firefox）。

---

## 7. 性能预算（3080 Ti 12GB）

| 项 | 一期上限 | 超了怎么降 |
|----|----------|------------|
| 可见三角 | 先按办公室实数计；超过 8M 可见则合批/合并材质 | 建筑合批，家具 GPU instancing |
| DrawCall | < 800（VR 双目） | 同材质合并 |
| Lightmap | 总像素 ≤ 6 × 4096² | atlas，不要 12 张全驻留除非测过显存 |
| XR 分辨率 | scale 0.85 | 0.7 |
| 阴影 | 关 | 已进烘焙 |
| 后处理 | 无 TSR / 无 VSM | — |

桌面窗口 60 FPS 不能代替 VR。验收必须戴头显记 GPU 显存（任务管理器 GPU 专用）。接近 10GB 就降 Lightmap 或 scale，不要等 OOM。

办公室比展厅小，按「全高清拷贝展厅 2800 万三角」做是错误预算。导出后先记一帧 stats，再定是否合批。

---

## 8. 任务分解

并行原则：WebXR 输入可在灰盒场景上先做（几个带目录的盒子 + 地面），不必等最终烘焙。视觉导出与手柄可两周重叠。

### Task 0: 仓库骨架与本地服务

**Files:**
- Create: `OfficeWebXR/package.json`
- Create: `OfficeWebXR/server.mjs`（从展厅 `web/server.mjs` 去掉 `/twin-api`）
- Create: `OfficeWebXR/office.html`
- Create: `OfficeWebXR/start.cmd`
- Create: `OfficeWebXR/src/main.js`（清屏色 + 一行 FPS）
- Test: 浏览器打开能看到 FPS

- [ ] **Step 1:** `server.mjs` 只服务 `OfficeWebXR/` 与 `data/`，端口 `4193`，无 Twin 代理。
- [ ] **Step 2:** `start.cmd` 启动 Node 并打开 Chrome `http://127.0.0.1:4193/office.html`。
- [ ] **Step 3:** 验收：无 404，控制台无报错。

### Task 1: 移动/转向纯函数单测

**Files:**
- Create: `OfficeWebXR/src/locomotion.js`
- Test: `OfficeWebXR/tests/locomotion.test.mjs`

- [ ] **Step 1:** 用 `node --test` 写失败用例：`axisX=1, cooldown=0` → yaw 增加 45°；`cooldown=0.2` → 不转；`rightAxisY=-0.56` → 瞄准为真；`-0.54` → 假。
- [ ] **Step 2:** 实现 `snapTurnYaw` / `moveOffset` / `teleportAimActive`。
- [ ] **Step 3:** `node --test OfficeWebXR/tests/locomotion.test.mjs` 通过。

### Task 2: 手柄映射单测

**Files:**
- Create: `OfficeWebXR/src/xr-input.js`
- Test: `OfficeWebXR/tests/xr-input.test.mjs`

- [ ] **Step 1:** 假 gamepad：`axes=[0,0,0.8,-0.9]` → 左/右手读到 x=0.8, y=-0.9；`axes=[0.8,-0.9]` → 同样结果。
- [ ] **Step 2:** 扳机/B 上升沿：prev pressed true → 本次不触发。
- [ ] **Step 3:** `node --test OfficeWebXR/tests/xr-input.test.mjs` 通过。

### Task 3: 灰盒场景 + 桌面第一人称

**Files:**
- Create: `OfficeWebXR/src/desktop-input.js`
- Create: `OfficeWebXR/src/collision.js`
- Create: `OfficeWebXR/data/dev-greybox.js`（程序化地面 + 3 个可点盒子）
- Modify: `OfficeWebXR/src/main.js`

- [ ] **Step 1:** 无 glTF 时加载灰盒；胶囊 34/92；WASD 走、鼠标看、Q/E 45°。
- [ ] **Step 2:** T 瞄准地面出标记，松开瞬移。
- [ ] **Step 3:** 左键打开世界空间面板，右键关闭。盒子带 `catalogId` 101–103 样例。
- [ ] **Step 4:** 无头显验收桌面操作清单（见第 9 节桌面列）。

### Task 4: WebXR 会话 + 灰盒进 Pico

**Files:**
- Create: `OfficeWebXR/src/xr-session.js`
- Create: `OfficeWebXR/src/laser.js`
- Modify: `OfficeWebXR/src/main.js`
- Modify: `OfficeWebXR/start.cmd`（写 OpenXR 注册表、杀 SteamVR）

- [ ] **Step 1:** 「进入 VR」→ `immersive-vr`；退出恢复桌面循环。
- [ ] **Step 2:** 玩家根节点 + `local-floor`；头显转动不写 Pointer Lock。
- [ ] **Step 3:** 左手移动、右手转向/瞬移、扳机点盒子、B 关面板、右手激光。
- [ ] **Step 4:** Pico 实机验收灰盒（见第 9 节 Pico 列）。**此任务通过后才允许把时间花在最终烘焙上。**

### Task 5: 目录加载与面板文案

**Files:**
- Create: `OfficeWebXR/src/catalog.js`
- Create: `OfficeWebXR/src/inspect-panel.js`
- Test: `OfficeWebXR/tests/catalog.test.mjs`
- 输入：先手写一份迷你 `inspect-catalog.json`

- [ ] **Step 1:** `toPanelText(item)` 与 `FInspectableItemData::ToPanelText` 逐行一致（单测快照）。
- [ ] **Step 2:** 绑定表按 `nodeName` 挂 `userData.inspectable`。
- [ ] **Step 3:** 未绑定网格点不中；墙地不在 bindings 中。

### Task 6: UE 导出脚本

**Files:**
- Create: `Scripts/ExportOfficeWebXR.py`
- Modify: 仅导出会话的光照 CVar（不要提交改死 `DefaultEngine.ini` 的 Lumen 默认，或单独提供 `Saved` 本地覆盖说明）
- Output: `OfficeWebXR/data/*`

脚本步骤（编辑器 `py Scripts/ExportOfficeWebXR.py`）：

1. 打开 `/Game/OfficeShell/Maps/Office_Demo`。
2. 列出 Nanite 网格；仍为 Nanite 则打印失败名单并 `raise`。
3. 读 `PlayerStart` → `spawn.json`。
4. 用与 `OfficeInspectBinder` 相同规则写 `inspect-bindings.json`；目录合并 `完整清单.json` + 内置 101–103。
5. 导出可见静态网格为 `architecture.glb`（含 UV2）。
6. 导出地面碰撞为 `collision.glb`。
7. Build Lighting 后把 Lightmap PNG 写入 `data/lightmaps/`，并在 glTF extras 记录贴图相对路径。

灯光：导出会话把要贡献 GI 的灯改为 Stationary/Static。不改动源关卡资产的话，脚本应复制到临时关卡再烤，避免污染 Lumen 演示关。

- [ ] **Step 1:** 先在一小块测试关（地面 + 一把椅）跑通导出。
- [ ] **Step 2:** 再对 `Office_Demo` 全量导出。
- [ ] **Step 3:** 网页加载 glTF，无粉材质、无缺 UV 黑块。

### Task 7: 场景加载器接正式数据

**Files:**
- Create: `OfficeWebXR/src/scene-loader.js`
- Modify: `OfficeWebXR/src/main.js`（有 `architecture.glb` 则不用灰盒）

- [ ] **Step 1:** 按 `spawn.json.gltfUp` 旋转。
- [ ] **Step 2:** 绑定 Lightmap 到 UV2。
- [ ] **Step 3:** 出生点与胶囊与 UE 门口一致（目视）。
- [ ] **Step 4:** 至少 5 件真实家具可点，面板字段正确。

### Task 8: 性能与显存

**Files:**
- Modify: `OfficeWebXR/src/main.js`（stats：draw、tris、`framebufferScaleFactor`）
- 可选：`?scale=0.7`

- [ ] **Step 1:** 桌面记 tris / draws。
- [ ] **Step 2:** VR 记 GPU 专用内存；>10GB 降 scale 或 atlas。
- [ ] **Step 3:** 双目无持续掉到 < 45 FPS（Connect 侧可先 72Hz）。

### Task 9: 文档与启动

**Files:**
- Modify: `README.md` 增加「办公室 WebXR」一节（启动、手柄、与 UE VR Preview 并存）
- Modify: `Docs/OfficeVR_Phase1.md` 交叉链接
- 本文件保持为方案正文

- [ ] **Step 1:** 写清：UE Preview 与 WebXR **不要同时占 OpenXR**。
- [ ] **Step 2:** 验收清单抄到 README。

---

## 9. 验收清单

### 9.1 桌面（无头显）

- [ ] `start.cmd` 打开页面，办公室或灰盒可见
- [ ] WASD 走，Q/E 每次 45°
- [ ] T 瞄准合法地面可瞬移，对墙无效
- [ ] 左键家具出属性板，墙地无板
- [ ] 右键 / Esc / B 关板
- [ ] 面板十个字段与 UE 一致

### 9.2 Pico（必须戴头显）

- [ ] Chrome「进入 VR」后面板立体，不是平面浏览器窗
- [ ] 6DoF 转头
- [ ] 左摇杆按头朝向走
- [ ] 右摇杆左右 45° 转向
- [ ] 右摇杆下推出现落点，松开瞬移
- [ ] 右手射线，可点物变色，扳机开板
- [ ] B 关板
- [ ] 12GB 卡不 OOM，连续走 5 分钟

### 9.3 回归

- [ ] 原 `ShanghaiOfficeShell_OpenSteel.uproject` 仍能按 `Docs/OfficeVR_Phase1.md` 打开（本方案未拆掉 Pawn）
- [ ] `ZHHZ_NEW-Windows-Preview` 未改入口、仍可独立启动

---

## 10. 风险

| 风险 | 可能结果 | 处理 |
|------|----------|------|
| Chrome 认不到 PICO Connect OpenXR | 无「进入 VR」 | `start.cmd` 写 ActiveRuntime；杀 SteamVR；换 Chrome 非 Firefox |
| Nanite 无法批量关 | 导出失败 | 先只导出已是 Static 的家具 + 简化建筑碰撞，视觉建筑分批转 |
| Lightmap UV 错 | 斑块/接缝 | 标准 UV2；不引入展厅自定义 shader，除非 Task 7 失败 |
| 双目仍爆显存 | OOM | 降 scale、减 atlas、家具合批；不要开阴影 |
| 手柄 profile 轴序不同 | 走变成转 | `xr-input.js` 双布局 + 实机校准页（显示 raw axes 10 秒） |
| 网页串流延迟 | 晕 | Connect 降码率；与 UE 串流同一网络条件 |
| 误改 `DefaultEngine.ini` | 原 Lumen 路径被毁 | 导出只用临时关卡或本地 Saved 覆盖，PR 检查 ini |

最大工期在 **Task 6（Nanite → 可烘焙网格 + Lightmap）**，不在 WebXR 胶水代码。灰盒 Pico（Task 4）应一周内能见手柄是否通；场景品质单独排期。

---

## 11. 与现有办公室 UE 的关系

| | UE VR Preview（已有） | 本方案 WebXR |
|--|----------------------|--------------|
| 操作实现 | `OfficeVRPawn.cpp` | `locomotion.js` + `xr-input.js`（行为克隆） |
| 场景 | `Office_Demo` 实时 | 同关卡导出的烘焙 glTF |
| 画质 | Lumen/Nanite（12GB 易 OOM） | Lightmap |
| OpenXR | 同一 PICO Connect 运行时 | 同一运行时 |
| 适用 | 高配卡 / 以后再开 | 当前 3080 Ti 目标交付 |

两套并存。评委机用 WebXR；有 24GB 卡时仍可用原 UE Preview。不要在本方案里关死 Lumen 工程默认。

---

## 12. 建议工期（一人）

| 阶段 | 任务 | 约 |
|------|------|----|
| 骨架 + 灰盒桌面 | 0–3 | 2–3 天 |
| 灰盒 Pico 手柄 | 4 | 2–4 天（含 OpenXR 联调） |
| 目录/面板 | 5 | 1 天 |
| UE 导出与烘焙 | 6–7 | 5–10 天（视 Nanite 数量） |
| 性能与文档 | 8–9 | 2 天 |

总计大约 2–4 周。若 Nanite 建筑无法按期转换，先交付「灰盒手柄已通 + 部分房间烘焙」，不要卡住手柄验收。
