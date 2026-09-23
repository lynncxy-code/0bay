# 海炬北区外景与建筑楼层交互验收

2026-09-23 最新局部替换见 [体育馆/理科院落验收](Acceptance/Landmarks/ACCEPTANCE.md)：新增地标 27 项与园区 12 项通过；九层 23 项交互通过，既有 Z=3910 cm 与历史 Z=4320 cm 的固定断言差异单独记录，主楼地图未改。下方 2026-09-22 的楼层高度与 24/24 结果为当时状态。

## 结果

- 2026-09-22 最新增量：约 500 米园区模型地面、入口铺装、绿化/停车/长椅/路灯，以及全部 1,677 栋周边建筑和主楼的单栋悬停/点击高亮已加入。实现与操作说明见 [Campus500/README.md](Campus500/README.md)。新增园区交互 12/12、原九层流程 24/24 均通过 DX12 Standalone 实机检查。
- 完整 V3 蓝色 GLB 已导入：158 个 Static Mesh、15 个 Material Instance、4 张内嵌 Texture，UE 内容体积约 13.64 MB。
- 外景独立子关卡为 `/Game/HaijuNorthBeta/Maps/L_HaijuNorthBeta_Exterior`，由 `/Game/OfficeShell/Maps/Office_Demo` 以 `LevelStreamingAlwaysLoaded` 加载。
- 外景放置为 `(14485.456, -5394.377, 0) cm`、Yaw `20.9°`、Scale `1`。Interchange 已把米制 GLB 转为 UE 厘米和 Z-up，没有重复乘 100。
- `Ground_light_blue` 的 GLB `baseColorFactor` 为 `[0.34, 0.52, 0.68, 1.0]`。当前仅将 `Ground_7` Actor Z 调为 +3 cm，使底板表面到 +1 cm 覆盖原卫星地表；另补南侧地面以覆盖主楼周围约 500 米，新增地面不使用卫星纹理。
- 158 个背景组件均为 `NoCollision`，不生成 overlap，不影响导航，不参与运行时点击；为控制大范围外景开销，关闭 cast shadow 和 distance-field lighting。
- 新增子关卡前已备份 `Office_Demo.umap`，SHA-256 为 `632AA334D94A3C2BDA792F0F4A2A022C29BE7BEFA45B98CB288BE4220B53F7A7`。

## 对齐证据与精度

使用 Office 卫星 Landscape 的 7.056 km 覆盖宽度、Yaw `20.9°`，结合建筑轮廓、道路网和体育馆控制点做视觉最小误差拟合。体育馆预测点与人工控制点相差约 `10.77 px`，底图比例约 `3.5228 m/px`，对应约 `37.9 m`。这是当前素材条件下的可解释视觉配准，不是测绘级定位。

证据：

- `Acceptance/Satellite_Alignment_Overlay.png`
- `Acceptance/UE_Building_Overview.png`
- `Acceptance/UE_Floor09_Selected_Glass.png`

## 回归结果

- 只读清单对比了改动前的 352 个已有演员：类、位置、旋转、缩放变化均为 0。
- 新子关卡演员数 158；Always Loaded、碰撞、overlap、导航、阴影和距离场设置全部通过自动检查。
- DX12 Editor Game 自动交互检查 24/24 通过，明细见 `Acceptance/DX12_Runtime_Test.json`：建筑 BP、九层代理、Z=4320、无可见 F9 提示的入口、预览输入、相机切换、楼层拾取、抽屉位移、液态玻璃弹窗按钮、反复进入/返回、Pawn/相机/输入恢复均通过。
- `当前楼层` 弹窗使用真实 `UBackgroundBlur`，中性蓝黑 tint alpha 为 `0.50`，落在指定 skill 的 Balanced Screen `44–58%` 范围；文字和按钮位于 blur 之上，装饰层为 `SelfHitTestInvisible`。左上角“建筑总览 / F9进入建筑”提示不存在，灰色建筑底座不存在。
- 原外景导入阶段 Map Check：0 error、0 warning；本次新增内容通过保存后重新加载、模型/碰撞/Always Loaded 检查，明细见 `Acceptance/Campus_Verification.json`。

未执行 Cook、Development/Shipping 打包和 125–200% DPI 矩阵，符合本轮“不打包”约束。当前弹窗仍使用工程现有 `CoreStyle` 字体；DX12 Editor Game 中文显示正常，但严格的 Shipping 字体 Cook 验收需在后续打包阶段完成。运行日志仍有 Office_Demo 全局 VSM marking queue 和 one-pass lights 警告；背景组件已关闭投影阴影，自动交互测试未发现可见错误。

## 生成二进制隔离说明

OfficeShell 工程的 `Binaries/Win64` 曾同时包含外部插件与项目输出的插件副本，命令行加载时触发重复类注册。为继续导入，原 `UnrealEditor.modules` 已备份到 `Saved/HaijuNorthBeta/UnrealEditor.modules.before.json`，当前生成清单仅保留 `OfficeHost`。以下 6 个生成文件移至 `Saved/HaijuNorthBeta/ProjectBinaryPluginDuplicates/`：

- `UnrealEditor-OntoTwinIndustrialBehavior.dll`
- `UnrealEditor-OntoTwinIndustrialBehavior.pdb`
- `UnrealEditor-OntoTwinSync.dll`
- `UnrealEditor-OntoTwinSync.pdb`
- `UnrealEditor-OntoTwinSyncEditor.dll`
- `UnrealEditor-OntoTwinSyncEditor.pdb`

源插件和源码没有修改。若以后改回项目本地插件二进制：先关闭 OfficeShell 的 Editor/Commandlet，确认外部同名插件不再被加载，再把上述 6 个文件复制回 `Binaries/Win64`，并用备份覆盖 `Binaries/Win64/UnrealEditor.modules`。在当前外部插件仍启用时不要恢复，否则会重新出现重复类加载崩溃。

## 变更范围

- 主地图增加外景与园区细节的 Always Loaded 子关卡引用。
- 新增：`Content/HaijuNorthBeta/`、`Art/HaijuNorthBeta/`、`Tools/HaijuNorthBeta/`、`CampusInteraction.h/.cpp`。
- 主楼预览接入单栋拾取、悬停/选择清理、Esc 清除选择，扩大滚轮缩放范围；启用 UE 内置 ProceduralMeshComponent，仅两个共享网格用于高亮。
- 已有 352 个 Office_Demo 演员变换及类保持不变；原外景仅 Ground_7 Actor 标高改变。OntoTwin 插件源码与业务绑定未修改。
- 未 Commit、未 Push、未打包。

最新截图：`Acceptance/Campus_Ground.png`、`Acceptance/Campus_Building_Highlight.png`、`Acceptance/Campus_Main_Highlight.png`、`Acceptance/Campus_Floor09.png`。园区设施为设计性补充，不代表实测。
