# OntoTwinSync 插件使用说明

OntoTwin Nexus 的 UE5 数字孪生同步插件。把整个 `OntoTwinSync/` 文件夹拷进任意 UE5 工程的 `Plugins/` 目录即可，不再需要挑选单个源文件。

源码迁移自仓库根目录 `ue5/`（原文件保留作参考，不再维护）。

## 4.4 Blueprint 容器化表现

`I3D_Representable` 可同时下发具体 Static Mesh 与项目 Blueprint 容器：

```json
{
  "asset_id": "/Game/SCC/Art/SM_Machine.SM_Machine",
  "container_blueprint_id": "/Game/SCC/Program/Function/Machine/BP_Item_base_SCC_Machine.BP_Item_base_SCC_Machine",
  "container_slot": "primary",
  "is_visible": true
}
```

容器 BP 必须添加 `OntoTwin 表现宿主`（`UTwinRepresentationHostComponent`），并把
`primary` 槽位的目标组件指向需要接收模型的 `StaticMeshComponent`。运行时结构为：

```text
ATwinInstance
└── 项目容器 BP 子 Actor
    └── HostComponent(primary) → 具体 Static Mesh
```

没有 `container_blueprint_id` 的旧快照继续走 4.3 的直接资产加载路径。4.4 首版容器模式
只接受已 Cook 的 Static Mesh，且不与 `render_parts` 组合模型同时使用。父 `ATwinInstance`
仍是唯一空间变换源；容器 BP 保留自身 Tick、碰撞和项目交互逻辑。

## 安装

1. 把本文件夹整体复制到目标工程：`<你的工程>/Plugins/OntoTwinSync/`
2. 右键 `.uproject` → Generate Visual Studio project files
3. 打开工程，UE 提示编译插件 → 确认
4. 菜单 Edit → Plugins，确认 "OntoTwin Sync" 已启用（依赖 Niagara 插件，会自动启用）

> 工程是纯蓝图工程也可以——放入 C++ 插件后 UE 会自动转为代码工程并要求装 VS 工具链。

## 3.9 只读小地图接入

1. 在 Web“人物漫游 > 运行开关”中开启“启用小地图”，保存并应用。
2. 在目标 UE 关卡放置插件 Actor `TwinMinimapAnchor`，实例命名为 `TwinMinimapAnchor_All`。
3. 保持 `MinimapId = minimap.default`，再通过继承的 Camera Component 调整位置、旋转、透视/正交模式及取景范围。
4. `CaptureWidth` / `CaptureHeight` 默认 `1024 × 768`，可在 `256–2048` 范围内调整。
5. 运行并进入人物漫游。小地图会出现在右上角；第一人称、过肩和全局视角共用同一个人物标记。

插件只在进入漫游或运行中从关闭切到开启时捕获一次场景，随后停止地图画面更新，仅以 20 Hz 更新人物位置和方向。小地图不接收输入；缺少或重复 `minimap.default` 锚点时只关闭小地图，不会阻断人物漫游。

Web“UE 运行状态”会显示小地图当前状态。若显示“缺少地图视角”或“地图视角重复”，检查当前关卡是否恰好只有一个 `MinimapId = minimap.default` 的 `TwinMinimapAnchor`。

当前关卡的三个相机锚点职责如下：

- `TwinMinimapAnchor_All`：`MinimapId = minimap.default`，只提供一次性小地图取景。
- 漫游视角锚点：`CameraId = camera.god.default`，用于 F7 漫游中的默认上帝视角。
- 固定视角锚点：`CameraId` 与 `StartupViewCameraId` 一致（默认 `camera.startup.default`），用于运行时开局、F7 退出漫游及 F10 退出场景编辑后的固定视角；缺失时兼容回退到 `camera.god.default`。

相机专属隐藏可在 Project Settings 的 `OntoTwin Camera Visibility` 中配置，也可在单个
`TwinMinimapAnchor` / `TwinGodViewAnchor` 的 `Hidden Actor Names` 与 `Hidden Level Names`
中追加。Actor 规则同时匹配运行时对象名和编辑器标签，并默认递归包含附属 Actor；Level
规则填写流送关卡短名，例如 `L_SCC_ENV_Site`。小地图通过 SceneCapture 隐藏，God/Startup
视角通过本地 PlayerController 隐藏，并在离开对应视角时恢复；隐藏关卡中的灯光组件也会
随视角临时关闭和恢复。

漫游默认视角为上帝视角；按 `V` 依次按“上帝视角 → 过肩视角 → 第一人称 → 上帝视角”循环。`F7` 仍只负责进入或退出漫游。

小地图默认从锚点视野的四边各裁剪 20%，玩家方向标记使用最高 70% 不透明度的红色呼吸三角。运行时右上角地图按钮可将面板在约 220ms 内折叠为图标或重新展开；第一人称模式下先按 Tab 进入 HUD 交互。

## 编辑器鼠标世界坐标工具

插件自带 Editor Utility Widget：`/OntoTwinSync/EUW_MouseWorldCoord`，以及宿主无关的 Editor-only C++ 类 `UOntoTwinMouseWorldLibrary`。

1. 打开 UE 编辑器。
2. 选择 **Tools → OntoTwin Mouse World Coordinates**，即可直接打开工具。
3. 在 Level 视口中移动鼠标，查看世界坐标、表面法线、命中 Actor 和视图类型。

也可以在内容浏览器开启“显示插件内容”，然后右键 `OntoTwinSync Content/EUW_MouseWorldCoord` 选择“运行编辑器工具控件”。正交视图未命中物体时，可用指定轴深度计算坐标；透视视图打空时不更新坐标。

该能力属于插件自身的 `OntoTwinSyncEditor` 模块，只在 UE 编辑器中加载，不依赖 `DigitalFactoryBaseEditor`，也不会进入 PIE Runtime 逻辑或 Shipping 可执行文件。

## 包含的三个类与对应前端页面

| 类 | 对接后端 API | 对应前端页面 | 用法 |
|---|---|---|---|
| `ATwinSceneManager`（孪生场景管理器） | `GET /api/v2/state/snapshots` 轮询 | instance.html / ontology.html / coord_workbench.html（2.x 全家） | **关卡里放 1 个即可**，自动 Spawn/销毁/驱动所有孪生体 |
| `ATwinInstance`（孪生实例） | 不发 HTTP，由 Manager 驱动 | 同上 | 不手动放置；可在编辑器用 Manager 的"📸 快照固化到关卡"按钮生成持久 Actor |
| `UDigitalTwinSyncComponent`（⚠ legacy，1.x） | `GET /api/state` + `POST /api/update` | index.html 演示页 | 加到单个 Actor 上，Tags[0] 填实例 ID（如 `vehicle_01`） |

## 典型工作流（2.x，日常用这个）

1. 启动 Flask 后端（默认 `http://localhost:5000`）
2. 在前端 ontology.html 给 ObjectType 绑定 `ue_asset_path`（UE 内容路径，如 `/Game/Meshes/SM_Forklift.SM_Forklift`）——**没绑的类型 UE 不会渲染**
3. 在 coord_workbench.html 或 instance.html 创建实例
4. UE 关卡中放置 `TwinSceneManager`，细节面板确认：
   - 后端基础URL：`http://localhost:5000`
   - 场景ID：**留空** = 跟随后端当前激活的数据集（单工程常用）；填具体场景名 = 只拉该场景
   - 孪生体蓝图类：选你的 `BP_TwinInstance`（不选则用 C++ 基类）
5. 点 Play：实例自动出现；前端改状态（位置/材质/动画），UE 0.5 秒内跟随

### 多工厂 / 多 UE 工程

后端实例按 `scene_id` 分文件持久化（`backend/data/scenes/{scene}.json`），重启不丢。
多个 UE 工程对接同一后端时，各自在 `TwinSceneManager` 的**场景ID**里填不同的场景名，
即可各拉各的场景、互不串台。一个场景文件自带渲染配置，可整包拷到另一台后端/工程直接用。

## 1.x legacy 组件什么时候用

- 配合 index.html 的单实例演示（vehicle_01 / equipment_01 / tooling_01）
- 需要 **UE→后端回写**时：组件在 BeginPlay 把 Actor 的真实世界坐标 `POST /api/update` 写回后端——这是目前全项目唯一的回写通道

## 迁移备注（相对原 ue5/ 目录的差异）

- API 宏 `TEST0316_API` → `ONTOTWINSYNC_API`，与具体工程模块名解耦
- 删除了 `DigitalTwinSyncComponent.cpp` 里的 `#include "test0316.h"`
- 模块依赖去掉了与同步无关的 `InputCore` / `EnhancedInput`
- 所有源文件统一为 UTF-8 BOM 编码（中文注释在 MSVC 下不再有编码风险）
- 逻辑零改动

## 已知限制

- `SetActorLabel` 调用未包在 `WITH_EDITOR` 里（沿袭原代码），打包发行版会编译失败；编辑器内开发/PIE 不受影响
- 轮询为 HTTP 短轮询（0.5s），无推送机制
