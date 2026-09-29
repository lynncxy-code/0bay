# Pico 4 Ultra × UE 5.6.1 办公室场景 VR 开发计划

> 用途：换电脑继续开发时的完整技术备忘与落地路线  
> 更新日期：2026-09-15  
> 状态：调研完成，待工程落地

---

## 1. 目标与边界

### 1.1 产品目标

把现有 **Unreal Engine 办公室区域场景** 运行到 **Pico 4 Ultra** 上，并支持：

1. **走动**：在虚拟办公室中移动（物理走动 / 摇杆连续移动 / 瞬移，至少一种可用）
2. **基础交互**：射线点选物体，查看物体属性（名称、材质、尺寸、所属系统、维保信息等）

可理解为「可逛的办公室 VR + 物体信息查询」，不是完整物理沙盒。

### 1.2 已知工程约束

| 项 | 现状 |
|----|------|
| 引擎 | **UE 5.6.1** |
| 场景类型 | 办公室区域（类似游戏关卡） |
| 光照 | **当前无光**；后续可能会加 |
| 目标设备 | Pico 4 Ultra（Snapdragon XR2 Gen 2，12GB，约 2160×2160×2，90Hz，Wi‑Fi 7） |
| 未决项 | 最终是否必须 **完全脱机单机**（见 §3 决策） |

### 1.3 明确不做（一期）

- 复杂物理抓取 / 装配仿真
- 全身动捕、手势为主交互（手柄射线足够）
- MR 透视叠信息（可作为远期选项）
- 一上来就上 Lumen / 硬件光追到独立 APK

---

## 2. 可行性结论（摘要）

**需求整体可行。** 真正要选的不是「能不能上 Pico」，而是：

| 路线 | 含义 | 何时选 |
|------|------|--------|
| **A. 独立 APK** | 场景真正跑在头显 Android 上 | 必须脱机、可发版、接受移动端画质 |
| **B. PC 串流** | UE 在 Windows 渲染，投到 Pico | 要高画质 / 后续加复杂光 / 旁边有 PC |
| **C. 云端 / WebXR** | Pixel Streaming / CloudXR | 多设备、少安装、有机房网络 |

**当前建议（在「是否必须脱机」未定时）：**

1. **先走 B（串流）** 验证走动 + 点选属性（最快）
2. 并行把交互与数据做成与平台无关的模块
3. 若业务要求脱机，再开 A（独立打包 + 移动端降配）
4. 后续加光：独立端用 **烘焙**；演示/高配用 **PC 档动态光（可含 Lumen）**

---

## 3. 决策树（换机后先确认这一条）

```
最终交付必须完全脱机、单头显？
├─ 是
│   → 主路径 A（独立 APK）
│   → 光照只规划：Static/Stationary + Lightmap
│   → 禁止依赖：Lumen / 硬件 RT / 大量 Movable 灯
│
└─ 否（办公室旁可放 PC / 可接受联网）
    → 主路径 B（PICO Connect 或 Business Streaming）
    → 光照可后续上 Lumen 甚至 RT（仅 Windows 目标）
    → 独立 APK 作为二期或精简档
```

把答案写在这里，避免换电脑后摇摆：

- [ ] **必须脱机** → 锁定路线 A
- [x] **有 PC 即可** → 锁定路线 B（推荐先做）—— **一期已锁定**
- [ ] **两者都要** → 同一工程双配置（PC High + Mobile）—— **后期再开，不阻塞阶段 1**

**2026-09-15 决策记录：**

- 一期：PC 串流（PICO Connect，无企业账号 / 无 Business Streaming）
- 后期：可考虑独立 APK + 串流双配置，截止日期暂无
- 头显：Pico 4 Ultra，Pico OS ≥ 5.14
- 场景现状：已用 Nanite / Lumen，允许在工程里关掉；一期串流验证时先关，避免交互被渲染档位绑死

---

## 4. 技术选型明细

### 4.1 设备与 SDK 版本对齐

| 组件 | 要求 |
|------|------|
| 头显 | Pico 4 Ultra |
| 系统版本 | **≥ 5.14.0**（Integration SDK 3.3 对 Ultra 的要求） |
| UE | **5.6.1**（已确认） |
| 独立开发 SDK | **PICO Unreal Integration SDK 3.3.x**（官方支持 UE 5.5 / **5.6**） |
| 可选插件 | **PICO Unreal OpenXR Plugin**（更偏标准 OpenXR；官方有交互 Sample） |
| PC 串流 | **PICO Connect**（消费级）或 **PICO Business Streaming**（企业） |

官方资源：

- 开发者中心：https://developer.picoxr.com/
- Integration SDK Releases：https://github.com/Pico-Developer/PICO-Unreal-Integration-SDK/releases
- OpenXR 交互 Sample：https://github.com/picoxr/PICO_UE5_OpenXRSample
- Pico 4 Ultra 开发资源页：https://developer.picoxr.com/pico4-ultra/

### 4.2 路线 A：独立 APK（Android）

**目标平台：** Android ARM64 + Vulkan  

**推荐起步：**

1. 从 UE **VR Template** 建交互骨架，或把现有办公室 Level 迁入 VR Pawn
2. 安装并启用 **Integration SDK 3.3**（和/或 OpenXR Plugin）
3. 显式配置 **Pico Ultra 手柄** Enhanced Input 映射（不要只依赖系统 remap）
4. 打包 APK，用 Developer Center / adb 安装到头显

**独立端渲染硬约束：**

- 关闭 / 不依赖：**Nanite、Lumen、硬件光线追踪**
- 使用：**Mobile Forward**、MSAA、Fixed Foveated Rendering（FFR）
- 控 Draw Call、面数、半透明、动态阴影
- 材质走 Mobile 质量档；纹理 ASTC

**参考性能预算（同档 XR2 Gen2，偏保守）：**

| 指标 | 建议起点 |
|------|----------|
| 目标帧率 | 72 或 90 Hz（先稳 72） |
| 可见三角（量级） | 尽量控制在较低六位数级可见面，按场景再 profiling |
| Draw Call | 尽量压到数百级以内 |
| 动态阴影灯 | ≤ 1 盏方向光（若需要） |

> 精确数字以实机 Unreal Insights / Pico 性能工具为准；上表只用于立项预期。

### 4.3 路线 B：PC 串流（推荐一期验证）

**目标平台：** Windows 64  

**步骤概要：**

1. UE 工程保持 Windows 运行（VR Preview / 打包 Win64）
2. PC 安装 **PICO Connect** 或企业 **Business Streaming**
3. 头显连接同一网络（建议 Wi‑Fi 6/7 独享路由）或有线
4. 若用 Business Streaming：确认 OpenXR Runtime 为文档要求的 Streaming Runtime
5. 在头显中进入串流后启动 UE VR

**优点：** 可保留较重场景与后续复杂光照；不必先搞定 Android 工具链。  
**缺点：** 依赖 PC 与网络；不能单机离线发版。

### 4.4 路线 C：云端 / 浏览器（备选）

| 方案 | 说明 |
|------|------|
| UE Pixel Streaming + WebXR | Pico 内置浏览器支持 WebXR；适合演示，交互成熟度不如原生 |
| NVIDIA CloudXR | 企业级高保真串流；需额外基础设施 |

一期除非有「多头显共用渲染机」需求，否则不优先。

---

## 5. 功能设计：走动 + 物体属性

### 5.1 走动（Locomotion）

按场地与需求选组合：

| 模式 | 适用 | 实现要点 |
|------|------|----------|
| 房间尺度物理走 | 真实空间 ≈ 虚拟办公室局部 | 配置 Guardian / Play Area |
| 摇杆连续移动 | 办公室比真实房间大 | VR Pawn + 左手柄摇杆；注意抗眩晕（加速度、Vignette 可选） |
| 瞬移（Teleport） | 快速移动、降眩晕 | 弧线预览 + NavMesh/可行走面检测 |
| 企业 LBE 大空间 | 场地级自由行走 | 需 Enterprise / LBE 方案，成本高，非一期默认 |

**一期建议：** 摇杆移动 + 瞬移双模式；物理走作为加分项。

官方/参考实现：

- UE VR Template 自带基础移动
- [PICO_UE5_OpenXRSample](https://github.com/picoxr/PICO_UE5_OpenXRSample)：传送、射线、抓取、手势等

### 5.2 物体属性交互（Inspect）

最小可用链路：

```
手柄 Forward 射线
  → LineTrace / Widget Interaction
  → 命中实现了 Inspectable 的 Actor
  → 读取属性（Data Asset / 组件 / JSON）
  → 在 3D Widget 或手腕/侧边面板显示
  → Trigger 确认 / B 键关闭
```

**建议数据结构（示例）：**

```text
InspectableItem (Data Asset 或 Primary Data Asset)
- DisplayName: string
- Category: string          // 家具 / 电气 / HVAC ...
- Material: string
- Dimensions: vector / string
- AssetId: string
- Description: string
- ExtraKV: map<string, string>   // 扩展字段
```

**Actor 侧：**

- 组件 `UInspectableComponent`：持有 Data Asset 引用 + 可选运行时覆盖
- 接口 `IInspectable`：`GetInspectData()`，方便蓝图/C++ 混用
- 高亮：简单 Overlay / 描边材质，注意 Mobile 开销

**不要做的一期范围：** 双持抓取、远程抓取物理、复杂 UI 框架。

---

## 6. 光照策略（当前无光 → 后续可能加）

### 6.1 现在（无光）

- 保持简单：Unlit / 基础 Lit + 环境光即可先跑通 VR
- **不要**为了「以后可能好看」提前打开 Lumen 项目默认依赖
- 优先锁帧率与交互

### 6.2 以后加光 —— 按目标拆档

| 档位 | 用途 | 允许 | 禁止/慎用 |
|------|------|------|-----------|
| **Mobile / 独立 APK** | 脱机发版 | Static/Stationary + Lightmap；极少动态灯；Emissive 模拟灯具 | Lumen、硬件 RT、大量 Movable 点光 |
| **PC / 串流** | 演示、评审、高观感 | Lumen、更丰富动态光、甚至 RT | 无硬禁止，以显卡与串流码率为准 |

**办公室实用做法（独立端）：**

- 天花灯/灯带：烘焙进 Lightmap
- 需要「开关灯」交互：切换 Emissive 强度或极少数动态灯，避免整层动态 GI

**工程建议：** 用不同 **Scalability / 自定义 Config / 平台 if** 隔离两套光照，避免互相污染。

---

## 7. UE 5.6.1 已知风险

### 7.1 Android 打包 Manifest 重复 xmlns（高优先级）

UE 5.6 存在回归：部分插件 APL 根节点带 `xmlns:android`，打包时与引擎写入的命名空间重复，导致：

```text
AndroidManifest.xml is invalid: xmlns:android is a duplicate attribute name
```

社区在 **UE 5.6 + PICO OpenXR** 上复现；升 SDK 不一定能修（根因偏引擎侧）。

**应对：**

1. 优先用 **PC 串流** 推进玩法，不阻塞
2. 独立打包时：按社区方案修改引擎 `UEDeployAndroid.cs` 过滤重复 xmlns，或关注 Epic/Pico 后续补丁
3. 参考讨论：Epic 论坛 “UE 5.6 + PICO OpenXR: xmlns:android is a duplicate attribute name”
4. 临时 workaround 仓库示例：`https://github.com/sdqsz/UnrealEngine5.6AndroidFix`

### 7.2 其他

- FFR + 3D Widget：Integration SDK 文档提到 UE 5.5 上曾有画面异常，Widget 可改 Transparent Blend 规避（5.6 实机再验证）
- `r.PSOPrecaching=1` 升级工程偶发崩溃时，可试 `r.PSOPrecaching=0`
- 独立端 Stationary/烘焙阴影在部分 UE 5.5–5.6.1 Mobile 路径上有社区反馈异常，加光后务必 **实机验证阴影**

---

## 8. 分阶段实施计划

### 阶段 0：换机环境准备（0.5–1 天）

- [ ] 安装 **UE 5.6.1**（与现工程一致）
- [ ] 同步工程源码 / Perforce / Git / 网盘拷贝（含 `.uproject`、Content、Plugins、Config）
- [ ] 安装 Android 工具链（仅当要打 APK：对应 NDK/SDK/JDK 按 UE 5.6 文档）
- [ ] Pico 升级系统 **≥ 5.14**
- [ ] PC 安装 **PICO Connect**（或企业 Streaming）
- [ ] 下载 **Integration SDK 3.3** 与（可选）OpenXR Sample 作对照

### 阶段 1：串流跑通 + 交互原型（3–5 天，优先）

- [ ] Windows 下 VR 预览 / 串流进 Pico
- [ ] 接入 VR Pawn：摇杆移动 + 瞬移
- [ ] 实现 `Inspectable` + 射线 + 属性面板
- [ ] 给办公室关键物体挂上 5–10 个示例数据
- [ ] 确认 90/72Hz 体感可接受（串流画质可先降码率保延迟）

**阶段 1 完成定义：** 能在 Pico 里走进办公室，对准桌子/设备弹出属性。

### 阶段 2：数据与内容规范化（2–4 天）

- [ ] 属性数据改为 Data Asset / 表驱动，避免写死在关卡
- [ ] 统一高亮与 UI 样式
- [ ] 可行走区域 NavMesh / 碰撞整理
- [ ] 性能粗测：stat unit / GPU，记下瓶颈（面数、透明、UI）

### 阶段 3：光照（按决策分支）

**若走独立端：**

- [ ] 布置 Static/Stationary 灯并烘焙
- [ ] 关掉 Lumen/RT 相关默认
- [ ] Mobile 预览 + 实机对比阴影与曝光

**若走 PC 演示档：**

- [ ] 单独 PC 光照配置（可开 Lumen）
- [ ] 与 Mobile 配置互不影响

### 阶段 4：独立 APK（仅当需要脱机）

- [ ] 集成 Integration SDK 3.3 / OpenXR Plugin
- [ ] 项目设置：Vulkan、ARM64、Multiview、MSAA、FFR
- [ ] 处理 §7.1 Manifest 问题
- [ ] 打 Shipping/Development APK，实机安装
- [ ] 对照串流版做功能与帧率回归

### 阶段 5：打磨与交付

- [ ] 眩晕与舒适度（瞬时转向角度、移动速度）
- [ ] 边界安全（Guardian）说明
- [ ] 打包清单、版本号、已知问题列表
- [ ] （可选）企业设备管理 / 静默安装流程

---

## 9. 工程目录与模块建议

在现有办公室工程中建议新增（名称可按项目规范调整）：

```text
Content/
  VR/
    Characters/BP_VRPawn_Office
    UI/WBP_InspectPanel
    Input/IMC_PicoUltra, IA_* 
  Data/
    Inspectables/DA_* 
  Blueprints/
    Components/BPC_Inspectable
Plugins/   (若用 SDK)
  PICOXR / PICOOpenXR / ...
Config/
  DefaultEngine.ini   # 平台渲染开关
  DefaultInput.ini
```

**代码/蓝图原则：**

- 交互逻辑不写死「只有 Pico」——输入走 Enhanced Input，部署走 OpenXR
- 属性查询与渲染档位解耦
- 避免一期引入过多 Marketplace 重型 VR 框架（除非团队已有标准）

---

## 10. 验收标准（一期）

| ID | 标准 | 通过条件 |
|----|------|----------|
| A1 | 上头显 | Pico 4 Ultra 可见办公室场景并稳定运行 ≥ 10 分钟无严重卡死 |
| A2 | 走动 | 能从入口移动到至少 3 个功能区域 |
| A3 | 点选 | 射线命中物体后显示属性面板，字段正确 |
| A4 | 关闭 | 能关闭面板并继续移动 |
| A5 | 帧率 | 独立端目标 ≥ 72FPS；串流端以体感延迟可接受为准 |
| A6 | 光照（若已做） | 独立端无 Lumen 依赖；PC 档可另议 |

---

## 11. 风险清单

| 风险 | 影响 | 缓解 |
|------|------|------|
| 办公室原场景面数/材质过重 | 独立端无法保帧 | 先串流；独立端做 LOD/合并/烘焙 |
| 后续强行上 Lumen 到 APK | 失败或严重掉帧 | 双档配置，独立端只 bake |
| UE 5.6 Android Manifest bug | 无法出包 | 引擎补丁 / 先串流推进 |
| 串流网络不稳 | 眩晕、操作延迟 | 有线或 Wi‑Fi 6/7 独享路由 |
| 真实场地小于虚拟办公室 | 无法纯物理走 | 摇杆 + 瞬移 |

---

## 12. 换电脑当天最小操作清单

1. 装 UE **5.6.1**，打开工程能进编辑器  
2. 装 **PICO Connect**，头显串进 PC  
3. 用 VR Template 或现有 Level 验证 **HMD 跟踪**  
4. 读本文件 §3，勾选是否必须脱机  
5. 按阶段 1 做 **移动 + Inspectable**，不要先纠结光照与出包  

---

## 13. 参考链接速查

| 主题 | 链接 |
|------|------|
| Pico 4 Ultra 开发资源 | https://developer.picoxr.com/pico4-ultra/ |
| Integration SDK Releases | https://github.com/Pico-Developer/PICO-Unreal-Integration-SDK/releases |
| OpenXR Sample | https://github.com/picoxr/PICO_UE5_OpenXRSample |
| OpenXR 文档入口 | https://developer.picoxr.com/document/unreal-openxr/ |
| UE5 Feature Support | https://developer.picoxr.com/document/unreal/ue5-feature-support/ |
| Pico 硬件规格 | https://www.picoxr.com/global/products/pico4-ultra/specs |
| 5.6 Manifest 问题讨论 | Epic 论坛搜：UE 5.6 + PICO OpenXR xmlns:android |

---

## 14. 变更记录

| 日期 | 变更 |
|------|------|
| 2026-09-15 | 初版：深度调研 + UE 5.6.1 / 无光后续可加 的收敛计划 |
| 2026-09-15 | 锁定一期路线 B（PICO Connect 串流）；后期可双配置；Nanite/Lumen 可关；无企业 Streaming；截止日期暂无 |
| 2026-09-15 | 阶段 1 落地：OfficeVRGameMode/Pawn、射线点选、家具属性、OpenXR；工程已迁到 F:\Office_VR |

---

## 15. 待你在新电脑上补全的信息

复制工程后请填写，便于下次继续迭代：

```text
工程路径：F:\Office_VR\ShanghaiOfficeShell_OpenSteel.uproject
主关卡：/Game/OfficeShell/Maps/Office_Demo（出生点用关卡内 PlayerStart，按公司大门理解）
仓库/同步方式：（待补）
是否必须脱机：[ ] 是  [x] 否（一期串流；后期再考虑双配置）
头显型号确认：Pico 4 Ultra
当前 Pico OS 版本：≥ 5.14
是否有企业账号 / Business Streaming：否（用 PICO Connect）
办公室场景是否已用 Nanite：[x] 是（一期不关；串流跑在 PC）
是否已用 Lumen：[x] 是（一期默认 officevr.LowGI=1 降质量，可设 2 关掉）
一期截止日期：暂无
```

---

**一句话策略：**  
UE 5.6.1 官方可对 Pico；先串流验证「走动 + 看属性」，独立 APK 按移动端烘焙光规划；复杂光只放 PC 档。出包时警惕 5.6 Manifest 坑。
