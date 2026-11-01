# OntoTwin 4.0 Scene Interaction — UE 项目接入

本目录提供平台侧通用运行能力；人物 Mesh、动画、皮肤、路线和相机仍由具体 UE 项目提供。

## 最小资源

1. 创建 `UTwinCharacterAsset`，资产名 `ObserverBase`，对应后端 `TwinCharacter:ObserverBase`。
2. 创建两个 `UTwinSkinAsset`，资产名 `ObserverGray`、`ObserverGreen`，对应后端 `TwinSkin:*`。
3. 人物与皮肤使用同一个 Skeleton，并为人物配置可打包的 Animation Blueprint。
4. 关卡放置 `ATwinRoamingSpawnAnchor`，将 `SpawnId` 设为 `spawn.character.default`。把 Actor 放在目标楼层地面附近；默认只在锚点上方 1 米至下方 10 米的局部范围内寻找地面，避免跨楼层误投射。
5. `ATwinRoamingRoute.bSplineAtGroundLevel=true` 时，Route Actor 与各 Spline 点的 Z 表示地面表面高度，不要手动加人物胶囊半高。自动路线只从 Spline 读取平面位置和朝向，贴地高度继续由 CharacterMovement 维护。
6. 关卡放置 `ATwinRoamingRoute`，将 `RouteId` 设为 `route.test.default`。默认认为 Spline Z 位于地面；若点已是胶囊中心高度，关闭 `bSplineAtGroundLevel`。
7. 关卡放置一个 `ATwinGodViewAnchor`，将 `CameraId` 设为 `camera.god.default`；它用于 F7 漫游中的默认上帝视角。
8. 如需独立固定视角，再放置一个 `ATwinGodViewAnchor`，将 `CameraId` 设为组件的 `StartupViewCameraId`（默认 `camera.startup.default`）；进入游戏、F7 退出漫游及 F10 退出场景编辑后都会恢复到该锚点。锚点缺失时兼容回退到 `camera.god.default`。
9. 关卡只需原有一个 `ATwinSceneManager`；4.0 组件由其构造函数自动创建。

## Asset Manager

实际宿主项目必须让 Asset Manager 扫描 `TwinCharacter` 和 `TwinSkin`，并确保 Development/Shipping 构建会 Cook 这些资产。示例目录和规则应在宿主项目中按实际 Content 路径配置，不在插件源码里猜测：

```ini
+PrimaryAssetTypesToScan=(PrimaryAssetType="TwinCharacter",AssetBaseClass=/Script/OntoTwinSync.TwinCharacterAsset,bHasBlueprintClasses=False,bIsEditorOnly=False,Directories=((Path="/Game/OntoTwin/SceneInteraction/Characters")),Rules=(Priority=0,ChunkId=-1,bApplyRecursively=True,CookRule=AlwaysCook))
+PrimaryAssetTypesToScan=(PrimaryAssetType="TwinSkin",AssetBaseClass=/Script/OntoTwinSync.TwinSkinAsset,bHasBlueprintClasses=False,bIsEditorOnly=False,Directories=((Path="/Game/OntoTwin/SceneInteraction/Skins")),Rules=(Priority=0,ChunkId=-1,bApplyRecursively=True,CookRule=AlwaysCook))
```

## 关卡约定

- 地面和可选对象需能被 `Visibility` 射线检测。
- 人物出生点和路线起点必须能容纳配置胶囊；阻挡时运行时拒绝生成或拒绝归线，不穿墙传送。
- `StartupViewCameraId` 对应的锚点决定开局固定视角以及退出漫游/模型编辑后的恢复视角；`camera.god.default` 决定漫游会话第一次进入全局视角的位置，之后的视角循环为上帝、过肩、第一人称。
- F7 与 F10 是互斥模式键：漫游中按 F10 直接进入编辑，编辑中按 F7 在退出完成后启动新的漫游会话；编辑存在未保存修改时必须先选择保存、放弃或继续编辑。新漫游会话不恢复上次人物位置和路线进度。
- 在 Tab/Dock 中点击“主页”会完整退出当前漫游并回到启动镜头；人物、路线、小地图、漫游输入和底部漫游状态随会话一并清理。
- 普通漫游人物是观察者，不会自动成为 OntoTwin Instance，也不会在心跳上传位置。

## 验收顺序

1. PIE：资源解析、出生、F7 进入/退出、V 按“第一人称→过肩→全局”循环、WASD 与左键选择。
   第一人称准星默认关闭，可在底部 Dock 的“准星”按钮开启；瞄准配置了 `selected` 面板的标准实例时准星变为青色并放大。
   漫游 HUD 使用 Performance 静态深灰界面：底部状态与右侧说明文字无共享底板，按键使用独立胶囊，Tab 展开 20% 透明度的小型操作抽屉。
   状态文字前显示与行高一致的双层呼吸状态球；线路名称使用“线路漫游 - 名称”的用户可读格式。
2. 路线：自动接入、P 暂停/继续、WASD 接管、R 归线、从头开始、非闭合 loop 降级。近身与第一人称模式下，释放鼠标可点击，按住右键观察镜头；点位解说期间 P 只切换“解说后暂停/继续”，字幕和语音保持播放。
3. 热更新：速度/相机/皮肤直接生效，人物/出生/路线/全局相机显示待重载；Tab Dock 可从宿主已安装的人物中做当前会话原地切换，不写回 Web 默认值，也不重置视角、路线或解说。
4. 路线解说：运行投影携带稳定点位 ID、字幕段和项目音频资产；到点后暂停路线并在底部 Screen Space HUD 播放，WASD 中断、按钮跳过，异常时字幕兜底。
5. 解说音频：优先读取 `Saved/OntoTwin/Narration/<sha256>.wav`，缺失时从绑定的 OntoTwin 后端下载；校验失败不阻断路线。
4. Development exe：绑定失败关闭、后端断线保持最后配置、Primary Asset Cook 完整。
5. Pixel Streaming：桌面浏览器键鼠输入和 HUD 不穿透。
