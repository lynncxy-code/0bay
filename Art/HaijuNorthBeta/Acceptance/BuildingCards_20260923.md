# 0bay 楼高度与三处建筑信息卡验收

工程：`D:/0bay/OfficeShell/ShanghaiOfficeShell_OpenSteel.uproject`，关卡 `/Game/OfficeShell/Maps/Office_Demo`。

## 改动

- 0bay 整栋高亮外壳在预览开启时按楼体当前 Actor 位移同步。原外壳制作基准是 `(3275,-3080,480)` cm；现有关卡楼体位置是 `(3275,-3080,70)` cm，因此外壳向下跟随 410 cm。拾取射线、可见外壳及弹窗锚点使用相同偏移。九层代理原本挂在楼体 Actor 上，继续随其移动；实机九层世界基准为 3910 cm。
- 0bay 楼、霍英东体育馆、交大理科楼群点击后沿用现有 Screen Space 半透明信息卡。三个建筑只显示“收起信息”，不显示“进入”按钮。理科楼群的七个已有拾取块显示同一建筑信息；高亮仍对应点击的楼块。九层独立卡片继续保留“进入九层”。
- 卡片底层保持既有 `UBackgroundBlur` 与可读遮罩；文字和按钮位于其上层。关闭卡片会清除选择及高亮，Esc 和返回办公室沿用现有流程。

## 验证

- UE 5.6 `OfficeHostEditor Win64 Development` 编译通过；`git diff --check` 通过。
- DX12 实机地标检查：[landmark_runtime_test.json](D:/0bay/OfficeShell/Saved/HaijuNorthBeta/landmark_runtime_test.json)，31/31 通过，包括两个地标的点选、信息卡无进入按钮、关闭并清除高亮。
- DX12 实机园区检查：[campus_runtime_test.json](D:/0bay/OfficeShell/Saved/HaijuNorthBeta/campus_runtime_test.json)，15/15 通过，包括 0bay 高度跟随、整栋与九层切换、信息卡关闭。
- 九层进入和返回：[runtime_test.json](D:/0bay/OfficeShell/Saved/GrandNeoBay/runtime_test.json)，24/24 通过，楼层标高检查现按关卡中当前楼体位置计算。
- 实机图：[0bay 整栋高亮及卡片](D:/0bay/OfficeShell/Saved/HaijuNorthBeta/campus_03_main_building.png)、[九层高亮及原进入按钮](D:/0bay/OfficeShell/Saved/HaijuNorthBeta/campus_04_floor_detail.png)、[体育馆信息卡](D:/0bay/OfficeShell/Saved/HaijuNorthBeta/landmark_02_stadium_highlight.png)、[理科楼信息卡](D:/0bay/OfficeShell/Saved/HaijuNorthBeta/landmark_04_science_highlight.png)。

本次未改变楼体 Actor 位置、主楼网格、地标 GLB、Campus500 几何或主 OntoTwin 工具。编译生成在工程 `Binaries/Win64` 的六个重复插件 DLL/PDB 已移入 `Saved/HaijuNorthBeta/ProjectBinaryPluginDuplicates_20260923_1150`，可恢复；运行时加载插件本身的 Binaries。既有 UE 数据集绑定 HTTP 409 日志与本次场景交互无关。
