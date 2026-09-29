# 大零号湾建筑交互 V1

工程：`D:\0bay\OfficeShell\ShanghaiOfficeShell_OpenSteel.uproject`，UE 5.6。

## 明早如何验收

1. 打开现有 `Office_Demo`，点击 Play。新增建筑子关卡会随主场景加载。
2. 按 **F9** 进入建筑总览。界面不再显示“建筑总览 / F9”入口提示框。
3. 鼠标移到青色九层区域：高亮增强。点击该区域，九层代理向外抽出。
4. 右侧出现液态玻璃楼层卡片。它使用 UE `UBackgroundBlur`、50% 中性蓝黑遮罩、22px 圆角和 28% 白色细描边；玻璃装饰层不抢占场景或按钮输入。
5. 点击 **进入九层**，恢复打开建筑预览之前的办公室视角和输入。自动漫游在预览期间暂停，退出后恢复。
6. 再次打开建筑，测试 **收起楼层**、点击空白处取消、反复进入/返回。

预览操作：右键拖动观察，滚轮缩放，`9` 选中楼层，`Enter` 进入，`Esc` 先收起选中楼层、再返回办公室，`F9` 返回办公室。

## 关卡和资源

- 主关卡：`/Game/OfficeShell/Maps/Office_Demo`。
- 新子关卡：`/Game/OfficeShell/Maps/L_GrandNeoBay_Building`，Always Loaded。
- 子关卡内的控制 Actor：`GNB_BuildingExperience - move this actor to align building`。
- 建筑外观继续引用 `/Game/GrandNeoBay/BP_GrandNeoBay`。
- 新材质/代理网格：`/Game/GrandNeoBay/Interaction`。
- 可编辑 Blender 文件：`Art/GrandNeoBay/Floor09/Floor09Proxy.blend`。
- 运行逻辑：`Source/OfficeHost/GrandNeoBayExperience.*`、`GrandNeoBayWidget.*`。

原有办公室 Actor 和交互资源保留。没有通过 Open Level 切换地图，也没有修改外部公共插件源码。

## 手动对齐

在 Levels 面板中选中建筑子关卡，再选中上述 `GNB_BuildingExperience` Actor。

| 参数 | 当前值/作用 |
|---|---|
| Actor Location | `(3275, -3080, 480)` cm，移动整栋建筑及其九层代理 |
| Actor Rotation | Yaw `90°` |
| Floor Offset | 建筑局部坐标 `(0, 0, 3840)` cm；当前九层地面世界 Z 为 `4320` cm |
| Floor Height | `480` cm，单独改变代理的高度 |
| Drawer Offset | 建筑局部坐标 `(850, 0, 0)` cm，可调整抽出方向和距离 |
| Drawer Duration | `0.65` 秒 |
| Overview Distance | `22000` cm，预览镜头距离 |
| Overview Target / Yaw / Elevation | 预览镜头中心和观察方向 |
| Camera Blend Seconds | `0.8` 秒 |
| Card Anchor | 楼层局部坐标中的卡片定位点，弹窗同时限制在屏幕内 |
| Floor Title / Description | 卡片文案 |

在仅绕 Z 轴旋转、缩放为 1 时：九层世界高度 = Actor Z + Floor Offset Z。调整建筑高度后，可反向调整 Floor Offset.Z 保持九层在 4320。

建筑下方不再生成临时灰色底座。

编辑室内时，可以在 Levels 面板暂时关闭建筑子关卡的编辑器可见性。

## 本版边界

- 九层是根据原建筑使用的 CAD 半层轮廓制作的简化体块：体块 36 三角面，描边 360 三角面。它表达办公室所在的半层/楼翼区域。
- 抽出的是代理模型，原建筑外墙保留，没有制作抽出后的真实空洞，也不移动真实办公室。
- “进入九层”恢复进入预览前的办公室相机，保持人物和业务状态。若从原上帝视角打开，返回仍是那个视角。本轮没有新增室内出生点或改写上帝视角。
- `Show Building In Office` 默认关闭：建筑预览以外隐藏建筑外壳，避免遮挡现有室内视图。上帝视角下的建筑显示/剖切留待下一轮确定。
- 原工程运行时存在后台绑定 409 报错，旧日志中也能找到。本轮没有改动连接配置；建筑交互测试不依赖后台绑定成功。在线业务链路需要在正确数据集连接下另行验收。
- 使用了现有场景灯光、地形和天空，没有重做建筑材质、场景灯光或城市环境。

## 验证和备份

本次结果：UE 5.6 Editor Development 编译通过；24 项运行检查通过（包括按钮回调和 UIOnly 模式的三维楼层点击坐标）；重载地图后确认原有 350 个 Actor 的位置、旋转、缩放和类型保持一致，并确认灰色底座组件和旧材质均已删除。已目视检查建筑总览、九层抽出卡片及返回办公室的实际截图。

- `Saved/GrandNeoBay/runtime_test.json`：独立游戏进程中的相机、射线选中、抽屉、输入恢复、人物暂停/恢复、重复打开/返回检查。
- `Saved/GrandNeoBay/install_verification.json`：重载主地图后核对新子关卡，以及原有 350 个 Actor 的位置、旋转、缩放和类型。
- `Saved/GrandNeoBay/01_building_overview.png`、`02_floor09_selected.png`、`03_return_to_office.png`：实际 UE 运行截图。
- `Saved/GrandNeoBay/Backup_20260920/Office_Demo.umap`：修改前主地图备份。仅在没有后续地图编辑时可直接用它回退；否则在 Levels 中移除新增子关卡引用即可停用建筑交互。

截图测试使用进程级 `DisableAllScreenMessages` 去掉已有调试文字，不改工程调试设置。测试覆盖状态、按钮回调、原生场景指针事件和真实渲染，但不代替你在编辑器里体验鼠标手感和空间对齐。无窗口测试没有执行系统鼠标端到端操作。

## 开发脚本

`Tools/GrandNeoBay/build_floor_proxy.py` 用 Blender 生成代理；`install_building_level.py` 导入自有资源并接入子关卡；`verify_install.py` 只读检查。安装脚本会重导入本功能自己的网格并设置默认预览距离；手动调好场景后不要无目的地重新运行安装脚本。

编译目标：`OfficeHostEditor Win64 Development`。运行回归检查使用 `UnrealEditor-Cmd.exe <工程> /Game/OfficeShell/Maps/Office_Demo -game -RenderOffscreen -windowed -ResX=1600 -ResY=1000 -ForceRes -GNBTest -unattended`；该测试进程约半分钟后写报告并自行退出。
