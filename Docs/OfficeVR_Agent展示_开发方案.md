# Office VR：数字 Agent + 展示旁观

**版本：阶段性最终版 2026-09-20（裁定：ontology-online-tool）**  
状态：对方代码库已在 `F:\Office_VR\ontology-online-tool`。按源码完成接口裁定。本阶段换机仍 **不实现对话和麦克风**；阶段 B 只放不会说话的占位。

换机以本文为准。本机 Cursor 摘要：`canvases/OfficeVR-Agent-DLSS5-开发方案.canvas.tsx`（不随 Git）。

---

## 0. 工程事实（不要猜）

| 项 | 值 |
|----|-----|
| 仓库根 | `F:\Office_VR`（换机改盘符） |
| 工程 | `ShanghaiOfficeShell_OpenSteel.uproject`，**UE 5.6.1** |
| 启动 | `StartEditor.bat`（写死了 `D:\UE_5.6\...` 和 PICO Connect OpenXR json，换机必改） |
| 关卡 | `/Game/OfficeShell/Maps/Office_Demo` |
| 运行时 | `OfficeVRGameMode` / `AOfficeVRPawn` |
| 头显 | Pico 4 Ultra，OS ≥ 5.14 |
| 串流 | PICO Connect，`picostreaming-openxr.json`，不要和 WebXR/SteamVR 同时占 OpenXR |
| 现卡 / 目标卡 | 3080 Ti 12GB（VR Preview 会 OOM）/ **RTX 5090 32GB** |
| 一期路线 | PC 串流，不做独立 APK |
| 对方平台仓 | `F:\Office_VR\ontology-online-tool`（独立 `.git`，**禁止**并进本仓 Git） |

现有能力（**现状，不是目标**）：左摇杆按 **头显朝向** 行走；右摇杆 X **离散 SnapTurn 45°**（冷却 0.35s，体感像卡顿 45°/90° 转身）；右摇杆下推瞬移；右手射线、扳机看家具属性、B 关面板。Audio Listener 已绑 `VRCamera`。

没有：DLSS 插件、人物网格、麦克风采集、旁观 SceneCapture、UE 数字人插件。OntoTwin 相关插件为关闭，GameMode 会拆 Twin Actor，**不要启用**。

待改（不依赖凌云智栈，换机阶段 A 必须做完）：丝滑连续无限转身体；头显只控视线；左右摇杆配合移动；去掉头显里位置错误的黑灰指向线。详见 §4。

---

## 1. 已锁定

| ID | 决定 | 来源 |
|----|------|------|
| L1 | PC 串流 + Pico 4 Ultra，不做独立 APK | 2026-09-15 开发计划 |
| L2 | 头显像素 = **现版** Nanite/Lumen/VSM（`officevr.LowGI`）。禁止 DLSS 5 / FG / MFG / 泄露 NR 接到 OpenXR | 展示需求 + 调研 |
| L3 | 电脑屏幕可单独提高画质；第一阶段展示窗 **1080p** | 本次拍板 |
| L4 | **修正：** `ontology-online-tool` 提供的是 **网页业务智能体大脑**，不是 UE 网格/口型/TTS。本仓 **不** 自研 ACE/Convai/LLM；网格与口型本仓用占位，语音进出本仓补 | 源码裁定 2026-09-20 |
| L5 | 接入形态锁定为 **进程外 HTTP**。禁止把凌云智 Python/Node 编进 UE 插件，禁止 Pawn `#include` 对方类型 | 源码裁定 |
| L6 | 对话未接通之前：只放 **不会说话的人物占位**，不做会话、不做麦 | 本次拍板 |
| L7 | 第一阶段 `Office_Demo` 放 **2** 个 Agent 实体 | 本次拍板 |
| L8 | 对话接上之后：左手 **Y** 开始对话；右手扳机 **只** 用于家具属性；B 结束会话（若无会话则仍关属性面板） | 本次拍板 |
| L9 | 对话接上之后：麦优先 **Pico Connect 虚拟录音设备**，不通再改 PC 麦。OpenXR 不管麦 | 本次拍板 |
| L10 | 同时只允许 **1** 路对话（2 个实体可放置，不能同时说话） | 宿主规则，避免叠音 |
| L11 | 引擎保持 **5.6.1**。对方不是 UE 插件，不存在升引擎对接问题 | 源码裁定 |
| L12 | Agent 只走 UE 串流路径，不进 `OfficeWebXR/` | 现 OpenXR 互斥 |
| L13 | **身体转向 = 右摇杆连续 yaw，无限、无台阶。** 禁止 SnapTurn（现状 45° / 冷却 0.35s）。头显 **只** 控制视线，不写行走朝向、不写身体朝向 | 本次补丁 |
| L14 | **移动 = 左右手柄配合。** 左摇杆在 **身体朝向** 下前后左右平移；右摇杆 X 转身体；右摇杆 Y 下推瞬移暂时保留。行走不得再跟头显 yaw | 本次补丁 |
| L15 | 头显里跟随手柄、但位置完全不对的 **黑灰色指向线必须消除或改到正确瞄准原点**。Inspect 射线只能有一条，且必须从右手 **Aim** 向前 | 本次补丁 |
| L16 | 凌云智仓 **并列存放、独立 Git**。本仓 `.gitignore` 忽略 `ontology-online-tool/`。不提交对方 `.env`、`node_modules` | 源码裁定 |
| L17 | 两个工位人物对应两台 **业务智能体**（`business_agents.stable_id`），DSH preset = `lingyunzhi-business-agent`。`lingyunzhi-main-agent` 是编码助手，**不**当办公室 3D 角色 | 源码裁定 |
| L18 | UE **不准** 直打 `/LingYZBE/internal/dsh/*`（那是 DSH Host 的服务令牌通道）。对话走 session-gateway 的公开 Remote 协议，或本仓自写的 Sidecar 代打 | 源码裁定 |
| L19 | 对方仓 **没有** ASR / TTS / 骨骼网格。语音识别与播报由本仓（Windows）负责；对方只回收到 **文本** | 源码裁定 |
| L20 | 不改 `ontology-online-tool` 源码来「给 VR 开后门」。缺口用本仓 Sidecar 消化，或由对方自己发正式 kiosk API | 对方仓宪法 |
| L21 | **人设先占位。** 关卡只认 `office.agent.0` / `office.agent.1`，面板可用「Agent 0 / Agent 1」。不编造职位、姓名、职责。你补文案（名字、一句话职责、对应 `stable_id`）后再改映射表，不改 Pawn | 2026-09-20 确认 |
| L22 | **语音走 PICO Connect 虚拟录音设备 → 本仓 Sidecar ASR → 凌云智文本 → 本仓 TTS WAV → Slot 播放。** 不用 Pico Speech SDK（APK 路线），不用 OpenXR 采麦，ASR 默认 SenseVoice-Small **CPU**，不和 VR 抢 5090。会话中按住左手 Y 说话、松开识别发送。细则 `Docs/调研_Pico4Ultra_语音交互.md` | 2026-09-20 调研 |

---

## 2. `ontology-online-tool` 是做什么的（源码结论）

一句话：这是 **凌云智 OS / OrgBrain 的平台仓**。给工厂运营人员用浏览器管理业务本体、连数据库、用大模型对话执行业务动作。不是 Unreal 数字人工程。

全工作区号称八个独立仓；你放进来的 **只有平台仓这一个**。同级的 Shell / 组织首页 / 本体工作台 / APS 等仓不在 `F:\Office_VR`。主站可以靠 `frontend/vendor` 里已发布的工作台包跑，不需要把另外七仓源码拷进来。

### 2.1 目录

| 路径 | 实际职责 |
|------|----------|
| `backend/` | Python FastAPI「Onto Platform」：本体 Registry、身份权限、连接器、Action Worker、DSH 控制面 `/LingYZBE/` |
| `frontend/` | 主站 React/Vite（`/LingYZFE/`），端口文档为 5173 |
| `agent-platform/` | 把固定版本 DeepSeek DSH Runtime 与灵云智插件打成 Profile；会话网关、业务智能体 UI |
| `packages/platform-web/` | 给其他工作台用的前端 SDK，与 VR 无关 |
| `ontology/` | APS / 采购等已发布本体 JSON，不是人物模型 |
| `docs/` | ADR、离线交付、跨仓契约 |
| `deploy/` | 客户离线包，不是 UE 插件 |

官方架构概述（`docs/architecture/overview.md`）：发现异常 → 连接数据 → 自动处理。用户入口是 **浏览器**，不是头显。

### 2.2 这里的「Agent」指什么

两套 DSH Host preset（`agent-platform/packages/session-gateway/src/gateway.ts`）：

| preset | 含义 | 能否当办公室 3D 人 |
|--------|------|-------------------|
| `lingyunzhi-main-agent` | 编码/工作区助手（bash、文件、子代理） | 否 |
| `lingyunzhi-business-agent` | 业务智能体：绑定 `business_agents` 行、system prompt、模型路由 | **是（大脑）**，仍无身体 |

会话真相是 DSH Session，不是旧的 `/chat/stream`。公开对话入口是 session-gateway 的 Remote RPC：`session/create`、`session/prompt`、`session/cancel`，外加 cookie 证明是哪个登录用户。网关自己写明：**不拥有另一套 Agent 运行时**，只做所有权过滤。

内部还有 `POST /LingYZBE/internal/dsh/v1/chat/completions`（OpenAI 兼容流）。调用方必须是 DSH 服务令牌，并带已 reserve 的 `x-deepseek-harness-session-id`。这是 Host→平台 的模型通道，**不是**给 Unreal 的 SDK。

### 2.3 明确没有的东西（全仓检索）

无 Unreal / UE5、无骨骼网格、无数字人、无 TTS、无 ASR、无 OpenXR、无口型。`agent-platform/.env` 是本地密钥，已存在于磁盘，**禁止**拷进 Office_VR Git。

---

## 3. 怎么放到当前项目（裁定）

**位置：** 保持 `F:\Office_VR\ontology-online-tool`。这是导航用的并列目录，不是 UE `Plugins/`、不是 `Content/` 子关卡。对方 `docs/workspace/AGENTS.md` 写明：相邻目录不是运行时契约，禁止 `../其他仓/src` 源码耦合。

**Git：** 对方自带 `.git`。本仓忽略整个目录，两边独立 commit / PR。换 5090 时：UE 仓拉一份，凌云智仓按他们自己的 README 再 clone 或拷贝（含被 Git 忽略的 `.env`，用 U 盘/密码器，不进 Git）。

**运行时：** 两套进程，一台 5090 上并排跑。

```
Pico 4 Ultra ← PICO Connect OpenXR ← Unreal 5.6.1（Office_Demo + 占位人）
                                      │
                                      │ HTTP（本仓 Adapter → Sidecar）
                                      ▼
              ontology-online-tool：PostgreSQL / Neo4j / FastAPI / DSH / session-gateway
```

官方 Linux 一键入口是 `node scripts/local-stack.mjs preflight|start|status|stop`，依赖 Node 22.9+、uv、tmux、Docker。**Windows 5090 不能假定这条命令能跑。** 换机按对方自己的 Docker 路径：

- 后端：`backend/README.md` 的 `docker compose -f docker/compose.yaml`
- DSH：`agent-platform/README.md` 的 `corepack pnpm start:local` 或 `deploy/compose.yml`
- 主站：5173；后端文档端口 8080；客户离线单端口曾用 8081（`/LingYZFE/` + `/LingYZBE/`）
- DSH 网关默认 3080，原生运行时 3081；**3080/3081 不是给人打开的首页**

先在 5090 用浏览器登录主站、开一路业务智能体对话，证明栈活着，再让 UE 接 Sidecar。不要颠倒。

**禁止：**

- 把对方仓嵌进 `.uproject` Plugins
- 启用 OntoTwin 人物包冒充「他们的网格」
- UE 直连 `/LingYZBE/internal/dsh/*`
- 为了 VR 去改对方 session-gateway / FastAPI（除非对方自己开 PR）
- 把 `lingyunzhi-main-agent` 摆进办公室当接待员

---

## 4. 职责（按源码重划）

```
ontology-online-tool                     本仓 Office_VR
· 本体、连接器、RBAC                     · 办公室场景、家具 Inspect、VR Pawn
· DSH 业务智能体（文本进、文本出）        · 关卡 2 个占位人（网格本仓负责）
· 浏览器工作台                           · 适配器 + 可选 Sidecar
                                         · Pico 麦采集、本机 ASR、本机 TTS
                                         · 头显现版出画 + 1080p 旁观
```

对方 **不负责**：UE Actor Spawn、口型、3D 动画。之前「网格由对方提供」作废，除非另外再给一套人物资产。

本仓 **禁止**：下载 ACE/Convai、启用 OntoTwin 人物包、把左手 Y 绑到别的功能、在阶段 B 实现 PTT/ASR。

---

## 5. 运动与射线（本仓 Pawn，不依赖凌云智）

改 `Source/OfficeHost/OfficeVR/OfficeVRPawn.h` / `.cpp`。**不要等对话栈。**

### 5.1 现状（代码事实）

`AOfficeVRPawn::UpdateLocomotion`：

- 左摇杆 `MoveX/MoveY`：用 `VRCamera` 的 yaw 构造前进/右向，再 `AddMovementInput`。头一转，走的方向跟着转。
- 右摇杆 X：`Abs > 0.7` 且冷却到 0 时 `AddActorWorldRotation(Sign * SnapTurnDegrees)`。`SnapTurnDegrees = 45`，冷却 `0.35s`。这就是「只能一段一段转身」；推两次会像 90°。**不是**丝滑无限转。
- 右摇杆 Y `< -0.55`：瞬移瞄准，松开落地。本补丁 **先保留**。
- `VRCamera->bLockToHmd = true`，Pawn 不吃 Controller 旋转。头显已经只转相机；错的是 **走和转还绑在头上 / SnapTurn 上**。

`UpdateLaserAndHover`：

- `LaserMesh` 挂在 `RightController` 上，网格是引擎默认 `Cylinder`（未指定 Unlit 材质 → 头显里就是 lit 的 **深灰/黑灰**）。
- 每帧 `SetVisibility(true)`，相对位置 `(Distance*0.5, 0, 0)`，相对旋转 `FRotator(0, 0, 90)`（Roll 90），缩放 Z = 长度。引擎圆柱沿 **局部 Z**，这条线和手柄前向很容易 **差一个轴**，看起来会「跟着手柄动，但完全不在手柄指的地方」。
- `RightController` 的 Motion Source 是 `IMotionController::RightHandSourceId`（OpenXR 上通常是 **Grip**，不是 **Aim**）。Pico 手柄壳体朝向 ≠ 射线瞄准朝向。
- 工程启用了插件 **`XRVisualization`**。它会再画一套设备/指针可视化，可能和第二条灰线叠在一起。

### 5.2 目标手感（验收口径）

| 输入 | 目标 |
|------|------|
| 头显 | 只转视线（已有 `bLockToHmd`）。扭头 **不** 改变行走世界方向，也 **不** 把身体 yaw 写成头 yaw |
| 左手柄摇杆 | 相对 **身体朝向**（Pawn yaw / VROrigin 水平朝向）前后左右平移。推满连续走，松杆即停 |
| 右手柄摇杆左右 | **连续** 改身体 yaw，无角度台阶、无冷却卡顿，可无限转圈。松杆立即停转 |
| 右手柄摇杆下 | 仍为瞬移瞄准（与现逻辑相同，除非实现时与连续转冲突再单开一条） |
| Q / E（无头显） | 改为连续转，不再每次 45° |
| 右手射线 | 头显里最多一条检查线，从右手 **Aim** 原点沿瞄准轴向前；命中可查看物体变绿 |

角速度做成 Pawn 上可调 float（建议默认约 120°/s，死区约 0.15）。验收看手感是否丝滑，不要再出现 45°/90° 跳变。

身体朝向与头朝向必须拆开：例如身体朝桌子、头看左侧显示器时，左摇杆前推仍朝桌子走，而不是朝头看的方向走。

### 5.3 错位黑灰指向线：排查顺序（实现时按这次序，不要猜）

1. 临时把 `LaserMesh->SetVisibility(false)`。  
   - 线消失 → 就是本仓圆柱（轴/Grip 错了）。  
   - 线还在 → 关掉 `XRVisualization` 或关 OpenXR 设备可视化再测。
2. 本仓圆柱：不要用相对 Roll 90 去「猜」圆柱轴。改为 **世界空间** 摆 mesh（起点 = Aim 位置，朝向 = Aim forward，长度 = 命中距离），或换对准 X 轴的细杆，并换成 **Unlit** 浅色，避免再出现黑灰 lit 圆柱。
3. Motion Source：右手检查射线改用 OpenXR **Aim**（常见名字 `RightAim` / `aim` pose），Grip 只用来画手柄模型（若以后要画）。`GetAimTransform()` 必须和看得见的那条线同一套 transform。
4. 确认没有第二条线：XRVisualization、OpenXR 运行时指针、OntoTwin（GameMode 已拆 Twin，但若有人误开插件会再出远距离蓝线，那是另一条老问题）。

**关门：** 头显里射线从右手柄前端笔直射出，扭动手柄时线跟着指尖/瞄准轴走，不再出现旁边平行/交叉的黑灰线。

### 5.4 明确不做（本项）

- 不把行走改成「双手柄在空中划船/拖拉世界」（用户说的配合是 **双摇杆分工**，不是手势位移）。
- 不恢复 SnapTurn「防晕」模式，除非以后单独加选项；本展示默认连续转。
- 不在本项里改 Agent 对话键（左手 Y 仍预留、不绑定）。

---

## 6. 架构

```
Office_Demo
  家具 + Inspectable
  AgentSlot_0 / AgentSlot_1     ← 本仓占位 SkeletalMesh
        office.agent.0 / office.agent.1   ← 本仓稳定 ID，人设占位
        以后你补给名字、职责、凌云智 stable_id，只改映射表
        接通后两边都走 preset lingyunzhi-business-agent

出画
  双目现版 ── OpenXR ── PICO Connect ── Pico 4 Ultra
  旁观 1080p ── DXGI 窗口 ── 参观者电脑屏

对话（阶段 D，D0 网页跑通且 §9.1 麦验收过）
  点左手 Y ── 对准 Slot ── BeginTalk：Sidecar 打开并保持 Pico 虚拟麦
  按住 Y ── 截 PCM（不重新开关设备）── 松开 → SenseVoice CPU → 文本
  session/prompt ── 字幕 + WinRT/SAPI zh-CN WAV ── Slot UAudioComponent
  B ── EndTalk：关麦流
  Windows 默认播放 = Pico 虚拟扬声器；Listener=VRCamera
```

硬边界：DLSS 5 只能等官方插件，且只绑旁观 viewport；泄露 `nvngx_dlssnr.dll` 禁止进交付机。

---

## 7. 适配器（本仓写，不进对方仓）

Pawn **不准** 直接打 DSH。只调用 `IOfficeAgentAdapter`。实现类里走本仓 Sidecar 的小 HTTP，例如 `http://127.0.0.1:<sidecar>/v1/...`。地址和密钥只进本机配置，不进 Git。

| 宿主事件 | 含义 | 落到凌云智的哪一层 |
|----------|------|-------------------|
| `BindSlots` | 关卡两个 Slot 绑 `office.agent.0/1`。`stable_id` 人设未到之前留空或占位符，接通对话时再填 | 本仓配置，不是对方 Spawn |
| `BeginTalk(AgentId)` | 左手 Y，射线对着该人，且当前无会话 | Sidecar：`session/create` + 该 `stable_id` 的 `business-agent-sessions/reserve`（由网关代做，UE 不碰 internal） |
| `PushText(text)` / 以后 `PushTalk` | 用户一句话 | `session/prompt` |
| `EndTalk()` | B | `session/cancel`，丢掉 Session 所有权 |
| `OnAgentReply(text)` | 助手文本回来 | 本仓 TTS，不是对方 PCM |

`AgentId` 仍用 `office.agent.0` / `office.agent.1`。

Sidecar 是本仓小进程（Python 或 Node 均可，建议单独目录 `Tools/OfficeAgentSidecar/`，不要塞进 `ontology-online-tool`）。它扮演「一个已登录用户的无头客户端」：cookie + session-gateway 已公开的 `{type:'client-request', rpcId, method, payload}` 信封。这是消费现有协议，不是给对方加 REST。

若 Sidecar 因 cookie/Origin/生产门禁搞不定：停在阶段 B，把失败日志交给凌云智维护者，请他们出正式 kiosk API。**不要**用服务令牌从 UE 打 internal chat/completions 来「先跑起来」。

---

## 8. 占位实体（换机阶段 B）

- 新增可放置 Actor（建议名 `AOfficeAgentSlot`）：SkeletalMesh 占位 + `AgentId` + 碰撞（Visibility 通道，供现有射线打到）。
- **不要** 用 `UInspectableComponent`。扳机打到 Slot 时：本阶段无反应（或仅激光变色），不打开家具面板。
- 关卡放 2 个，工位旁，不要挡门。
- 网格：从 UE 5.6 模板迁 `SKM_Manny_Simple`，或胶囊。没有「对方网格到了再替换」这条路径，除非另外给人物资产。
- 人设：槽位显示名用「Agent 0 / Agent 1」。等你补名字、一句话职责、凌云智 `stable_id` 后再改配置，不改交互代码。
- 左手 Y：本阶段 **预留、不绑定**。实现对话时再绑 `EKeys::OculusTouch_Left_Y_Click`（及 Index/WMR 同位置键，与现有扳机多键策略一致）。

射线颜色：命中家具仍绿；命中 AgentSlot 用另一种色（实现时避开现有白/绿即可）。

---

## 9. 对话与语音（阶段 D；调研锁定见 `Docs/调研_Pico4Ultra_语音交互.md`）

Pico 4 Ultra **有 4 个麦**（官方规格）。PC 串流下它们出现为 Windows 录音设备，**不是** OpenXR 流，也 **不是** Pico 官方 Speech-to-text（那条要独立 APK，已排除）。

### 9.1 麦怎么进电脑（先于写 UE 对话）

PICO Connect 安装虚拟音频驱动后，录制列表里会出现名称含 `PicoStreamingMicrophone` 或 `Pico Streaming Virtual Audio` 的设备（版本不同，Sidecar 按子串 `Pico` 匹配）。没有设备时运行 Connect 目录下的 `AudioDriver*Install`，不是改 Unreal。

戴头显用系统「声音录音机」对着该设备说话必须有电平。失败则退 **PC 麦**，面板显示实际设备名。这不是引擎 bug。

社区（VRChat + Connect）证实：反复 Open/Close 这台虚拟麦会堆延迟。因此 **整段会话 Sidecar 保持占用设备**；按住 Y 只是在已打开的流上截窗口。

### 9.2 谁采、谁认、谁读

Pawn **不采 PCM**（UE AudioCapture 很难稳定选中 Pico 设备）。

```
头显 4 麦 → PICO Connect → Windows WASAPI
  → Sidecar 保持打开
  → 松开 Y：SenseVoice-Small CPU 转写
  → 文本 → 凌云智 session/prompt
  → 回复文本 → WinRT/SAPI zh-CN → WAV
  → UE 在该 Slot 的 UAudioComponent 播放
```

ASR 不进 5090 的 CUDA，避免和 Nanite/Lumen 抢卡。Whisper GPU 不作默认。TTS 不走凌云智。口型本阶段不做。

### 9.3 按键

| 键 | 行为 |
|----|------|
| 右手扳机 | 只开家具属性 |
| 对准 + **点** 左手 Y | 无会话 → `BeginTalk`，Sidecar 打开并保持 Pico 麦；已有会话则忙碌 |
| 会话中 **按住** 左手 Y | 截取 PCM |
| **松开** Y | ASR → 发给凌云智 → 字幕 + TTS |
| B | 有会话 → `EndTalk` 并关麦流；否则关属性面板 |
| 左摇杆 / 右摇杆 | 按 §5。**不要**退回 SnapTurn 或按头行走 |

同时第二个人按 Y：忽略或提示忙碌，不准开第二路。

### 9.4 阶段切开（避免麦没过就写 Pawn）

- **D1 文本：** Sidecar 能把一句打字送到凌云智并拿回字（证明大脑）。可与 9.1 并行。  
- **D2 麦链路：** 调研文 §机器验收 1–5（录音机电平 → Sidecar WAV → SenseVoice → 头显听到 TTS）。  
- **D3 接上 Y：** 才改 Pawn。关门：对准一人按住 Y 说话、松开后头显里听到本仓 TTS，面板有识别稿和回复；另一人忙碌；家具扳机仍可用；90 Hz 可接受。

Windows 默认 **播放** 必须是 Pico 虚拟扬声器。Listener 已在 `VRCamera`。

凌云智侧前置（本仓做不到就列给对方维护者）：

1. 库里至少两条 `business_agents`，`status=ENABLED`，各有 revision / system_prompt / model_consumer_id  
2. 展示用账号能登录主站并把这两人聊出字  
3. session-gateway 允许本机 loopback Sidecar（Origin / cookie Secure 在 Windows 上的表现要实测）

---

## 10. 展示旁观 + DLSS

第一阶段目标分辨率 **1920×1080**。

顺序：

1. 5090 上现版 VR 稳 90 Hz，记 `stat unit` 和 GPU 专用内存  
2. Spectator 单眼拷贝，参观者能看清射线、面板、两个人物  
3. 需要 16:9 再 SceneCapture：关掉 Capture Every Frame，Tick 里 `CaptureScene`，Always Persist Rendering State（UE 5.6.1 启动器 **没有** Epic late-update 修复，Capture Every Frame 会拖影）  
4. 可选：官方 **DLSS 4.5** 只作用于旁观  
5. 官方 DLSS 5 Neural Rendering：插件未公开则停。发布后只绑旁观 viewport。1080p 比 4K 便宜（4K 上 DF 测 5090 约 +8.3 ms），仍须实机  

禁止：主视口 DLSS 5、FG/MFG、ReShade 泄露 NR。

社区教程对照（仅调研，不进交付）：有人用 ReShade 6.8 add-on + RenoDX Discord 的 NR DLL 注入 `UnrealEditor.exe`，在 **编辑器视口** 里开 DLSS 5。逐步记录见 `Docs/调研_YouTube_UE56_社区DLSS5.md`（视频 https://www.youtube.com/watch?v=zZEs68BXD-M）。视频未测 VR/OpenXR。

---

## 11. 阶段与关门（换机按序，不要铺开）

### 阶段 A — 5090 现版 VR + 运动/射线

- 装 UE 5.6.1、Git 拉仓库、改 `StartEditor.bat` 引擎路径和 OpenXR json  
- 驱动 ≥ 616.64；Pico OS ≥ 5.14  
- 按 §5 改 `OfficeVRPawn`：连续无限转身体、头显只管视线、左摇杆按身体走、消灭错位黑灰线  
- **关门：** `Office_Demo` 能逛、能点家具，`officevr.LowGI 0` 不再 12GB OOM；右摇杆可丝滑无限转圈；扭头不改变走的方向；头显里检查射线从右手 Aim 射出，无第二条错位灰线  

### 阶段 B — 2 个不会说话的 Agent 槽位（本次换机终点之一）

- `AOfficeAgentSlot` ×2，射线可打中，扳机不打开属性  
- **关门：** 头显里能看到两个人站在工位；家具点选未被破坏；左手 Y 仍无功能  

### 阶段 C — 1080p 旁观（可与 B 后半并行，不得阻塞 A）

- Spectator 1080p 单眼或手动 Capture  
- **关门：** 电脑屏能看懂 VR 操作；头显无新增拖影  

### 阶段 D0 — 5090 上单独拉起凌云智栈（可与 C 并行，不进 UE）

- Docker：Postgres / Neo4j / 后端；再按 README 起 DSH 与主站  
- 浏览器登录，对两个业务智能体各打出一句话回复  
- **关门：** 不经过 Unreal，纯网页证明大脑可用。Windows 上 `local-stack.mjs` 失败不算对方仓坏了，改走 compose  

### 阶段 D — VR 对话（D0 关门后单独开一轮，按 §9.4）

- D1 文本 Sidecar；D2 麦/ASR/TTS 验收（不经 Unreal）；D3 左手 Y 按住说话  
- **关门：** 对准其中一人按住 Y 说话，松开后头显里听到本仓 TTS，面板有识别稿和回复；另一人忙碌；家具扳机仍可用；90 Hz 可接受  

### 阶段 E — 旁观画质（官方插件齐全后再做）

- DLSS 4.5 旁观 → 以后 DLSS 5 旁观  
- **关门：** 头显对比证明未开 Neural Rendering  

本次换机交付 = **A（含运动/射线）+ B 关门，C 能做则做，D0 能做则做**。D/E 不阻塞搬机。

明确不做：抓取、独立 APK、WebXR Agent、OntoTwin、自研 LLM、把凌云智源码编进 UE、把编码助手 preset 当办公室人。

---

## 12. 允许 / 禁止改的文件

允许：`OfficeVRPawn.h/.cpp`（§5）；`Source/OfficeHost/OfficeVR/` 新增 Slot / Adapter；`Tools/OfficeAgentSidecar/`（新）；`Office_Demo` 放置；`StartEditor.bat` 路径；本仓 `.gitignore`；阶段 E 官方 DLSS 4.5 插件目录。需要时关闭或配置 `XRVisualization`（先按 §5.3 验证）。

禁止：启用 OntoTwin*；替换 `DefaultPawn`；主视口 DLSS 5/FG；泄露 NR DLL；在 Pawn 里写死 DSH 协议；把 SnapTurn 当最终手感留下来；修改 `ontology-online-tool` 来迁就 VR；把对方仓加入本仓 Git。

---

## 13. 5090 机器清单

必装：UE 5.6.1、VS 2022、Git、本仓库、PICO Connect、驱动 ≥ 616.64。

阶段 D0 另装：Docker Desktop、对方仓自己的 Python/Node 锁版本、**独立的** `backend/.env` 与 `agent-platform/.env`（权限收紧，不进 Git）。

阶段 D2 另装：确认 PICO Connect 音频驱动后 Windows 录制里有 Pico 设备；Sidecar 用的 Python + FunASR SenseVoice-Small（CPU）；Windows 中文 TTS 语音包。默认播放设备改到 Pico 扬声器。

不要装：ACE、Convai、OntoTwin、非官方 DLSS 5。不要把凌云智装进引擎插件目录。

---

## 14. 给凌云智维护者的缺口清单（本仓读源码后仍缺的）

这些不是猜测，是对照 VR 需求后对方仓没有、或仅内部使用的部分。答案贴回附录再加码 Sidecar：

1. Windows 5090 上除 Linux `local-stack.mjs` 外，哪一份 compose 是当前可启动的集成组合？  
2. session-gateway 是否允许 127.0.0.1 非浏览器客户端持登录 cookie 调 `session/create` / `session/prompt`？Origin / Secure cookie 规则是什么？  
3. ~~两个展示用 `stable_id` 和人设~~ **已锁：先占位。** 你稍后补给：显示名、一句话职责、对应 `business_agents.stable_id`。未到之前阶段 B 的人只标 Agent 0/1，阶段 D 不编造人设去连错的业务智能体。
4. 若 Sidecar 不被允许：他们能否提供 **正式** 的 kiosk/service 对话 API（非 `/internal/dsh`）？  
5. 助手回复文本在事件流里的稳定字段名（避免我们去 scrape 网页 DOM）？  

---

## 15. 来源

- 本仓 `README.md`、`OfficeVRPawn.cpp`（SnapTurn 45°、按头行走、Cylinder 激光、Grip MotionSource）  
- `ontology-online-tool/CONTEXT.md`、`docs/architecture/overview.md`、`CONTRACT.md`、`docs/workspace/CONTRACT.md`  
- `agent-platform/README.md`、`packages/session-gateway/src/gateway.ts`（preset 名、`session/create|prompt|cancel`、cookie 所有权）  
- `backend/onto_platform/dsh_gateway/api.py`（internal chat/completions、business-agent-sessions）  
- `backend/tests/integration/test_business_agent_direct_chat.py`  
- NVIDIA DLSS 开发者页仍为 4.5；DLSS 5 UE 插件 2026-09 论坛未发布  
- ReShade 作者：不支持 OpenXR  
- Epic：5.6 deferred VR smear 未进 5.6.1  
- Pico 4 Ultra 官方规格：4 麦克风；PICO Connect 虚拟录音设备；SpeechService 仅 APK  
- 调研全文 `Docs/调研_Pico4Ultra_语音交互.md`  
- Pico Connect 麦走 Windows 设备，社区有开关麦堆积延迟的案例  
- FunASR SenseVoice-Small：中文 CER 优于 Whisper，可 CPU 推理  
- Digital Foundry：5090 4K DLSS 5 约 +8.3 ms  

---

## 附录：拍板记录 2026-09-20

| 项 | 选定 |
|----|------|
| 接口未接通时 | 只放不会说话的占位，不做会话与麦 |
| 对方仓实际是 | 凌云智 OS 平台：本体 + DSH 业务智能体（网页），不是 UE 数字人 |
| 放置方式 | 并列目录 + 独立 Git + 进程外；不进 Plugins |
| 开始对话 | 左手 Y；扳机只用于家具 |
| 麦克风 | Pico Connect 虚拟设备进 Sidecar；失败改 PC 麦。不用 Pico Speech SDK / OpenXR 采麦 |
| ASR / TTS | SenseVoice-Small CPU + WinRT/SAPI zh-CN；会话中按住 Y 说话 |
| 第一阶段人数 | 2，映射两条 business_agents，preset=`lingyunzhi-business-agent` |
| 展示窗 | 1080p |
| 转身 | 取消 SnapTurn；右摇杆连续无限转身体 |
| 视线 vs 移动 | 头显只控视线；左摇杆按身体朝向走 |
| 错位灰线 | 阶段 A 必须修掉（本仓圆柱轴 / Grip vs Aim / XRVisualization） |
| Agent 接口裁定 | 已完成：进程外 Sidecar 消费 session-gateway；禁止 UE 打 internal |
| 人设 | 先占位 Agent 0/1；名字、职责、stable_id 你后补，只改映射表 |
