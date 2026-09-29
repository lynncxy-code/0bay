# 办公室 WebXR

PC 浏览器第一人称 + Edge WebXR。头显出画走 SteamVR（PICO Connect 作为 SteamVR 驱动）；Unreal VR Preview 仍用 `StartEditor.bat` 的 PicoStreamingXR。  
方案正文：`Docs/OfficeWebXR_开发方案.md`。

## 启动

1. 安装 Node.js。PICO Connect 已连接 Pico 4 Ultra。不要同时开 Unreal VR Preview。
2. 双击 `OfficeWebXR/start.cmd`，**不要关那个黑色窗口**。
3. 等出现 `SteamVR is running`，SteamVR 认出头显后，只用新开的 Edge 点「进入 VR」。
4. 还没有 `data/architecture.glb` 时是灰盒房间。

旧标签页没有 WebXR 标志，进不去头显。

## 桌面操作

| 按键 | 作用 |
|------|------|
| 点击画面 | 锁定鼠标 |
| WASD | 走 |
| 鼠标 | 看 |
| Q / E | 左转 / 右转 45° |
| T | 按住瞄准地面，松开瞬移 |
| 左键 | 查看属性（世界空间面板） |
| 右键 / B / Esc | 关面板 |

墙和地板不能点。可点物对准时射线变绿。撞墙会挡住并沿墙滑。

数字人语音不在这个网页里。要对门口的人说话，退出本页后用仓库根目录的 `StartOfficeVR.bat` 进 Unreal VR Preview，射线变橙后按住左手 Y。

## Pico

1. 先退出 Unreal VR Preview。
2. PICO Connect 保持连接。
3. `start.cmd` 会把 OpenXR 设成 SteamVR 并启动 SteamVR。Unreal 路径仍用 `StartEditor.bat`（会关掉 SteamVR、改回 Pico OpenXR）。
4. 头显里应是房间，不应再看到网页。转头跟头显。
5. 左摇杆按头朝向走；右摇杆左右连续转向；右摇杆下推瞄准地面，松开瞬移；双手扳机开面板；B 关面板。
6. 手柄轴若反了：`http://127.0.0.1:4193/office.html?debugpad=1`。降分辨率：`?scale=0.7`（默认 0.85）。

## 从 UE 导出正式办公室

当前没有 `architecture.glb`。这一步必须在编辑器里做：

1. 打开 `Office_Demo`。把要进网页的网格从 Nanite 改为普通 Static Mesh，并生成 Lightmap UV。
2. Output Log：`py F:/Office_VR/Scripts/ExportOfficeWebXR.py`
3. 若还有 Nanite，脚本会写出 `data/nanite-blockers.json` 并停止 glTF。按名单关掉 Nanite 后再跑一次。
4. 需要引擎插件 **GLTFExporter**。脚本不会改 Lumen 工程默认。
5. 刷新网页。有 `architecture.glb` 就不再用灰盒。

## 测试

```text
cd OfficeWebXR
npm test
```
