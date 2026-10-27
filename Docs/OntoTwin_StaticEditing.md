当前办公场景已切换为本地静态编辑，统一回写暂缓。

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
