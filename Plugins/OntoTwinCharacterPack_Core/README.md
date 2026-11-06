# OntoTwinCharacterPack_Core

OntoTwin 人物漫游的基础内容包。宿主工程安装 `OntoTwinSync` 后，再安装本插件即可使用 Nexus 资源目录中的 Ground Staff“工人”、两套基础工装和独立的 Manny 机器人。

## 固定运行时资产

- `TwinCharacter:ObserverBase`
- `TwinCharacter:MannyRobot`
- `TwinSkin:ObserverGray`
- `TwinSkin:ObserverGreen`
- `TwinSkin:MannyRobotDefault`

资产位于插件挂载点：

- `/OntoTwinCharacterPack_Core/Characters`
- `/OntoTwinCharacterPack_Core/Skins`
- `/OntoTwinCharacterPack_Core/Materials`
- `/Game/Art/A08_Characters/00_Ground_Staff`（Ground Staff 工人及其动画、材质依赖）
- `/Game/Art/A08_Characters/maozi`（工人安全帽依赖）
- `/Game/Characters/Mannequins`（由安装器从插件的 `HostContent/` 载荷复制）

`ObserverBase` 固定使用 `Ground_Staff/ThirdPerson/SkeletonIK/SK_Charactor` 与 `myAnimBlueprint`。该动画蓝图通过 Retarget Pose From Mesh 读取隐藏的 Manny 无武器移动动画源；隐藏源只驱动步态，画面中仍然只显示 Ground Staff 工人。`MannyRobot` 使用 UE 5.6 模板中的 `SKM_Manny_Simple` 与无武器移动动画，两者是两个独立目录项。内容包不包含 Fab、Renderpeople 或其他第三方商城人物。

## 安装到宿主工程

在仓库根目录执行：

```powershell
.\scripts\install_ontotwin_character_pack_core.ps1 `
  -ProjectPath "D:\SCC\SCC2\scc2\scc2.uproject"
```

安装器会复制插件、安装 Ground Staff 与 `/Game/Characters/Mannequins` 载荷、在 `.uproject` 中启用插件，并向 `Config/DefaultGame.ini` 写入带标记且可重复执行的 Asset Manager 扫描规则。若宿主已有同名但内容不同的人物文件，安装器默认拒绝覆盖。已确认宿主需要保留自有 Ground Staff 版本时，可显式增加 `-UseExistingHostContent`，安装器只补缺失文件。执行安装前必须关闭 Unreal Editor。

安装后可执行一次冷启动验证：

```powershell
.\scripts\verify_ontotwin_character_pack_core.ps1 `
  -ProjectPath "D:\SCC\SCC2\scc2\scc2.uproject"
```

## 重新生成 Core 资产

仅在升级 UE 基础人物或重建母本内容包时执行：

```powershell
.\scripts\build_ontotwin_character_pack_core.ps1 `
  -ProjectPath "D:\SCC\SCC2\scc2\scc2.uproject" `
  -EngineRoot "D:\UE_5.6"
```

构建器使用 Core `HostContent/` 中受控的 Ground Staff 与 Mannequin 依赖，生成两个 `TwinCharacter` 和三个 `TwinSkin` Primary Data Asset，再把验证通过的插件内容同步回本母本插件。Ground Staff 依赖更新时，先执行 `migrate_ground_staff_to_core.ps1` 生成受限依赖闭包。
