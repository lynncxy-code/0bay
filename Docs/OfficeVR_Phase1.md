# 办公室 VR 阶段 1（PC 串流）

工程：`F:\Office_VR\ShanghaiOfficeShell_OpenSteel.uproject`  
关卡：`/Game/OfficeShell/Maps/Office_Demo`  
GameMode：`OfficeVRGameMode`（默认 Pawn 为 `OfficeVRPawn`）

## 要做什么

1. 用 UE 5.6.1 打开工程，等 C++ 编译完成（或先编译 `OfficeHostEditor`）。
2. 打开 `Office_Demo`。若 Play 仍是旧摄像机，在 Output Log 执行：
   `py F:/Office_VR/Scripts/SetupOfficeVR.py`
   然后保存关卡。
3. 无头显：Play In Editor，WASD 走，Q/E 转，鼠标左键点选，右键关面板，T 瞄准瞬移。
4. 有 Pico 4 Ultra：PC 开 PICO Connect，头显进串流，编辑器点 **VR Preview**。
   - 左摇杆走
   - 右摇杆左右转（45°）
   - 右摇杆下推对准地面，松开瞬移
   - 右手扳机查看属性
   - B 关闭面板
5. 出生点用关卡里已有的 `PlayerStart`。门口附近会有一块「操作说明」立方体；若场景家具识别不足 5 个，会在出生点前再生成示例方块（桌子/椅子/沙发/电视等属性）。
6. 场景里已有的 PDF 家具、电视、电话亭会按名称自动挂上属性。

## 属性字段

名称、类别、区域、尺寸、材质、资产编号、所属系统、维保周期、下次维保、备注。  
家具数据来自 `Content/Art/家具_PDF匹配/完整清单.json`；维保为示例值。

## 光照

默认 `officevr.LowGI 1`：降低 Lumen 质量，**不关 Nanite**（串流渲染在 PC 上）。  
完全关掉 Lumen：控制台 `officevr.LowGI 2`。保持工程原样：`officevr.LowGI 0`。

## 还没做

独立 APK、烘焙光照进 UE 运行时。网页导出见 `Docs/OfficeWebXR_开发方案.md` 与 `OfficeWebXR/README.md`。

WebXR 与本文件的 VR Preview **不要同时占用 OpenXR**。
