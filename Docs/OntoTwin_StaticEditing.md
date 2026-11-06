当前办公场景采用静态美术布局与摄像头动态实例混合模式。以下先说明 2026-11-02 的摄像头恢复，其后为原静态转换记录。

2026-11-02 摄像头恢复：

- 当前主关卡为 `/Game/OfficeShell/Maps/Office_Demo_ArtMerged`；运行管理器位于 `L_OntoTwin_Runtime`。
- 沿用原实例 ID 17–30，通过平台模型绑定与空间回写 API 保存当前模型、位置、旋转和缩放。未修改其余 63 个实例，也未改动 Web 交互版本 16 的业务成员与点击绑定。
- `bUseLevelGeometry=true` 时，`LevelGeometryDynamicInstanceIds` 指定的实例仍从数据库生成 `ATwinInstance`，其余对象继续使用静态关卡布局。空名单保留原先完全静态的行为。
- 摄像头使用当前固定姿态网格和原材质，动态指数据库驱动的实例身份与生命周期；本次未增加摄像头机械转动动画。
- 动态实例生成后，其带 `OntoTwinInstanceId` 标签的静态副本只在运行时隐藏并关闭碰撞；编辑器原对象保留作备份。退出运行时恢复副本状态。以后摄像头的运行坐标以平台数据为准。
- 摄像头网格启用复杂碰撞供 Visibility 射线拾取。点击仍沿用原 `open_detail` 绑定，打开 `monitor-test.html?camera_id=<原实例ID>`。
- CLI 工具位于 `Tools/OntoTwinRestoration/`：`restore_camera_records.py` 写回记录，`configure_dynamic_cameras.py` 配置关卡，`verify_dynamic_cameras.py` 后台集成验证。
- 备份与验证记录在本机 `Saved/CameraDynamicRestore/`。原项目数据为 `project-before.json`，原关卡/网格在 `files-before/`。需要回退时，清空动态实例名单并按备份逐个恢复这 14 个实例；不要用整份旧项目覆盖后续其他业务修改。
- 验证：UE 编辑器目标编译通过；无桌面操作的 `-RenderOffscreen` 运行检查通过，14 个动态实例、14 个隐藏静态副本、模型/材质/变换一致、14 个 Visibility 射线命中、14 个详情页打开成功。

摄像机视角联动（2026-11-02）：

- 摄像头实例 17–30 对应关卡中原有的 `OntoTwin_Camera_17`–`OntoTwin_Camera_30`。`bind_camera_views.py` 为这些 CameraActor 保存 `OntoTwinViewForInstance=<ID>` 标签，运行与打包后均按标签匹配，不依赖编辑器名称。
- 打开对应实例详情或通过 Web 选择实例时，使用目标摄像机组件当前的世界位置、朝向、视场角和画面比例，平滑进入观察视角。视频页和点击绑定保持原样；没有关联摄像机的实例继续采用原来的模型聚焦行为。
- 漫游中的切换沿用观察 Pawn，可继续移动和切回人物视角。连续点击以最后一次选择为准。源 CameraActor 的位置与镜头参数不被此联动修改；若视频融合模块应用标定，联动使用应用标定后的当前视角。
- 绑定前关卡备份、编号清单和后台运行验证位于 `Saved/CameraViewBinding/`；测试脚本为 `verify_camera_views.py`。

原静态转换记录（2026-10-27）：

当时办公场景切换为本地静态编辑，统一回写暂缓。

建筑外壳也已拆开：主关卡中的 `BP_OfficeShell` 实例已替换为 23 个普通 `StaticMeshActor`，位于 `Building/OfficeShell_Editable`。包括 CeilingTrays、CeilingCoves、FeatureCeiling 和 Skirting，均可独立选择编辑。按原网格部件拆分，未进一步拆开单个网格中的各片墙体。原蓝图及网格资产仍保留；保存后的模型、材质、世界变换、碰撞和已检查渲染属性与转换前一致，验证记录见 `Tools/ActorConversion/verification.json`。

- 主关卡：`/Game/OfficeShell/Maps/Office_Demo`
- 模型子关卡：`/Game/OfficeShell/Maps/L_OntoTwin_StaticEditing`，随主关卡始终加载。
- 大纲文件夹：`OntoTwin_StaticEditing`，按办公桌、办公椅、摄像头、外部模型分组。
- 当前数据集：`ds_1788856616035`，UE 工程身份：`ueproj_0bay_office_opensteel_7e0ae9f7`。
- 共 77 个普通 `StaticMeshActor`：30 张办公桌、30 把办公椅、14 个摄像头模型、3 个外部模型。

可以直接移动、旋转、缩放，替换静态网格体，修改材质、贴图和碰撞。摄像头已生成固定姿态的静态网格；外部模型的网格、材质实例和原始贴图已保存到 `/Game/OfficeShell/StaticEditing`，不依赖运行时临时资源。数据集驱动的旋转、状态指示和告警效果暂停。

修改后使用“保存所有”，让主关卡、模型子关卡以及修改过的资源一并保存。若替换的是同一个实例，建议在原 Actor 的“静态网格体”属性中换模型，以保留对应关系。Actor 标签中的 `OntoTwinInstanceId` 和 `OntoTwinDataset` 用于后续匹配；复制、新增、删除的对象将在统一回写前单独核对。

原来的 `OntoTwin Scene Manager` 已完整移到 `/Game/OfficeShell/StaticEditing/Archive/L_OntoTwin_ControlBackup_20261027`。该备份关卡不再挂接到主关卡，因此打开或播放 Office_Demo 时不会启动它的实例轮询、接管或回写。需要恢复联网控制时，应先完成修改核对与数据提交，再恢复管理器并处理静态副本，避免重复显示或覆盖编辑成果。

本次没有向后端提交任何实例、材质、模型绑定或空间变更。统一回写尚未执行。

回写前所需资料保存在 `Tools/OntoTwinStaticEditing/Session_20261027/`：

- `snapshots.json`：转换时的数据集快照。
- `manifest.json`：77 个实例与关卡 Actor、原模型、固化模型、变换、材质和碰撞之间的对应表。
- `scene_before.json`：原有关卡对象的模型、材质、变换及可读取的灯光属性基线。
- `session.json`：本地编辑状态及管理器归档位置。
- `assets.json`、`reload_verification.json`：新增资产与保存后重新加载的验证结果。
- `runtime_verification.json`：独立编辑器会话的无渲染运行检查；77 个静态模型持续存在，管理器和动态实例均为 0，连续采样期间模型、变换及材质不变。
- `content_inventory.json`：转换后的 UE 资源文件大小与修改时间清单，便于回写阶段找出网格、贴图、材质等资源文件的变更；此清单不是资源文件备份。

本机完整主关卡备份：`Saved/Backups/Office_Demo_before_ontotwin_static_20261027.umap`；`Saved` 不纳入 Git，GitHub 上可从此次变更之前的提交恢复原关卡。管理器的独立备份关卡已纳入 Git。回写阶段应先生成变更清单，并与届时的数据集状态比较；网格、贴图、材质图和灯光等 UE 资源修改需要连同资源文件及引用关系核对，不能仅用位置回写按钮提交。
