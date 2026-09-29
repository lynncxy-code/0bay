# 调研：DLSS 4.5 只给显示器，Pico 保持现版

日期：2026-09-22。引擎：UE 5.6.1 启动器版，DX12，延迟渲染，Nanite/Lumen。头显：PICO Connect OpenXR 串流。本机引擎目录没有 NVIDIA DLSS 插件。

## 结论

不能在当前这条串流里打开官方 DLSS，再指望显示器是 DLSS、Pico 仍是现版。

显示器上的 VR Preview 窗口不是第二次渲染。`officevr.Spectator` 用的是 `SingleEyeCroppedToFill`：把已经画好的一只眼睛拷到电脑窗口。Pico 串的是同一对眼睛的 OpenXR 交换链。DLSS 插在场景视图上，改的是这幅眼睛，两边一起变。

官方插件装上之后，头显在线时本仓会把 DLSS 和帧生成 cvar 强制写成 0。没有插件时这些 cvar 不存在，行为与现在相同。

## 官方插件

NVIDIA 论坛插件归档（2026-05-21）列出 **DLSS 4.5 Plugin 8.6.1**，有 Unreal Engine 5.6 包。更早还有 8.6.0、8.5.0。下载在开发者论坛，要登录，不能从引擎里直接启用。本机 `D:\UE_5.6` 下没有 DLSS / Streamline 插件。

DLSS 5 的 Neural Rendering 仍不是这条官方包。社区 ReShade 注入不进交付，也不支持 OpenXR。

## 为什么不能只开在显示器上

1. **旁观窗口是拷贝。** `ISpectatorScreenController::SetSpectatorScreenMode(SingleEyeCroppedToFill)` 只做纹理拷贝，不单独跑超分。
2. **DLSS 跟的是场景视图，不是窗口。** 插件的 Streamline 视图扩展挂在后处理上。立体视图要么被超分（头显一起变），要么被插件跳过（显示器拷贝的也不是 DLSS）。
3. **OpenXR 上开 DLSS 会坏。** UE 5.1 起就有记录：`r.ScreenPercentage` 配合 DLSS 在 OpenXR / SteamVR / CloudXR 上立刻崩溃；Quest Link 有时能过。5.4 的 Streamline 在 VR 交换链上断言失败（目标纹理不是 `Backbuffer` / `XRSwapChainBackingTex`）。5.6 另有一条：DLSS 走 TSR，延迟渲染 + DX12 下转头拖影，Epic 的修复没进 5.6.1 启动器。
4. **不要用 SceneCapture 再画一张给显示器。** 5.6 里 `SceneCaptureComponent2D` 勾 Capture Every Frame，会把头显拖花。关掉每帧捕获、Tick 里手动 `CaptureScene`、并开 Always Persist Rendering State，可以减轻，但和 DLSS/TSR 叠在一起仍有人报告头显发糊。这条不能拿来当「只超分显示器」。
5. **帧生成更不能开。** 旧插件用 `StereoPass != eSSP_FULL` 把 FG 从 VR 视图拿掉，是因为 FG 进 VR 会崩。超分没有同等的「只处理桌面窗口」开关。

## 要同时满足两边，缺什么

需要第二路**不进 OpenXR 交换链**的渲染，并且 DLSS 视图扩展不能碰到立体视图。官方 8.6.1 没有这个开关。同一编辑器进程里开插件，保护不了 Pico。

可行、但本次不做的做法：另一个不含 VR 的进程或打包窗口自己跑 DLSS，头显进程保持现版。那是两套画面，不是把现在的旁观窗口超分。

## 本仓现在的行为

- Pico：现版 Nanite / Lumen / VSM，`officevr.LowGI 0`。
- 显示器：单眼裁切铺满窗口，内容与这只眼睛相同。
- 头显设备存在时：若将来装了插件，`r.NGX.DLSS.Enable`、`r.Streamline.DLSSG.Enable`、`r.NGX.DLSS.FrameGeneration.Enable` 被写成 0。
