# 调研：Pico 4 Ultra 麦做 VR 语音交互

日期：2026-09-20。栈：UE 5.6.1 + PICO Connect OpenXR 串流 + Pico 4 Ultra（OS ≥ 5.14），不做独立 APK。大脑是凌云智文本进/文本出。本文只定 **麦怎么进 PC、谁做 ASR/TTS**，不改引擎代码。

---

## 结论（最可行）

头显上的 4 个麦 **会** 进电脑，但 **不是** OpenXR 接口，也 **不是** Pico 官方 SpeechService。PICO Connect 在 Windows 上挂一台虚拟录音设备；本仓 Sidecar 按住会话期间一直打开这台设备，按住左手 Y 截一段 PCM，CPU 上跑中文 ASR，把文本交给凌云智，再把回复合成 WAV 给 UE 在人物旁播放。Windows 默认播放设备必须是 Pico 虚拟扬声器，否则声音在显示器。

不要做：一体机 APK 上的 Pico 语音 SDK、在 Unreal 里抢默认麦、在同一张 5090 上用 Whisper 大模型和 VR 抢 GPU。

---

## 硬件：麦确实在头显上

[PICO 4 Ultra 规格](https://www.picoxr.com/global/products/pico4-ultra/specs)：双立体声扬声器；**4 microphones and supports spatial audio recording**。经销商规格写 Quad MEMS。戴着说话时，物理拾音在头显，不在手柄、不在 PC。

问题只是：这条模拟信号在 **PC 串流** 下交给谁。

---

## 三条路，为什么只留一条

| 路径 | 拾音 | ASR | 和本仓约束 |
|------|------|-----|------------|
| A. Pico Speech-to-text / SpeechService | 一体机 Android 麦 | Pico 云/设备侧识别 | [Unity 文档](https://developer.picoxr.com/document/unity/speech-to-text/)、[Unreal demo](https://developer.picoxr.com/document/unreal/speech-to-text/) 面向 **独立 APK**。本仓 L1 不做 APK。PICO Unreal OpenXR 插件公开页写支持 UE 5.2–5.4，本仓是 5.6.1。**排除。** |
| B. OpenXR 采麦 | 运行时扩展 | 自接 | OpenXR 管姿态与双目出画，不提供麦克风流。社区与语音插件（Convai、ODIN）在 PC 上都走系统麦。**排除。** |
| C. PICO Connect → Windows WASAPI | 虚拟录音设备 | 本仓 Sidecar | 与现有 VR Preview 路径一致。VRChat 等 PCVR 都这么用。**采用。** |

官方串流帮助写明：串流用 **虚拟麦克风技术** 做 PC↔头显音频，并要求给 “Streaming Service” 麦克风权限（[旧版串流助手帮助](https://sf16-draftcdn-sg.ibytedtos.com/obj/ies-hotsoon-draft-sg/pico_streaming/pico_streaming_guide_cn.html)）。设备名随版本变化，验收时按子串匹配，不要写死一个字符串。

社区实测出现过的 Windows 录音设备名：

- `PicoStreamingMicrophone`（Connect 较新驱动，INF `ps_microphone.inf`，Hardware ID 含 PICOSTREAMINGMICROPHONE）
- `Pico Streaming Virtual Audio`（Streaming Assistant / 旧驱动）
- 另有播放端 `Pico Streaming Speaker` / `Avsoft Virtual Audio Device`

驱动安装位置社区多次提到：`C:\Program Files\Streaming Service\AudioDriver(Un)Install.exe` 或 `AudioDriver_inst64.exe`。设备禁用或未装驱动时，头显麦物理上在工作，PC 上却没有输入。

已知坑（VRChat + Connect，2025）：反复打开/关闭麦会导致延迟堆积到几百毫秒～1 秒；**保持设备一直被某个进程占用** 可避免。重连 Connect 或开关降噪能清延迟，不适合展示中途做。[反馈](https://feedback.vrchat.com/bug-reports/p/significant-audio-latency-when-switching-instances) / [记录](https://gist.github.com/fz6m/337aa44c27a7bde449ccff0e352f3040)。因此 Sidecar 在整段会话里保持 capture，不要每句开关设备。

---

## 谁来采麦：Sidecar，不进 UE AudioCapture

UE 的 `UAudioCapture` 默认打开 **系统默认录音设备**。蓝图有 `Get Available Audio Input Devices`，**没有** 对等的 SetCaptureDevice（[UE 论坛 5.2+](https://forums.unrealengine.com/t/how-do-you-change-a-users-audio-input-device-via-blueprints/1273503)）。C++ 可 `OpenCaptureStream` 带 device index，但 Windows 上 Pico 设备名不稳定，引擎层选设备脆弱。

本仓已经要有进程外 Sidecar 对接凌云智。Python `sounddevice` / WASAPI 按设备名子串 `Pico` / `PicoStreaming` 打开，失败再退默认录音设备（L9）。Pawn 只发开始/结束说话，不碰 PCM。

---

## ASR：CPU 上的中文模型，不和 VR 抢 5090

展示机同一张 RTX 5090 要跑 Nanite + Lumen + 双目 90Hz。Whisper large-v3 在 5090 上 RTF 很好（第三方测约 0.16），但会占 CUDA 和显存，VR Preview 掉帧风险高。中文 CER 上 Whisper-large-v3 约 20%，[FunASR 2026-06 对比](https://www.funasr.com/en/blog/funasr-vs-whisper-benchmark.html) 里 **SenseVoice-Small 约 7.8% CER，CPU 约 17× 实时**。

锁定：

1. **首选** Sidecar 调 FunASR **SenseVoice-Small，`device=cpu`**（约 234M）。工具包 MIT；权重另有 FunASR Model License，展示前核一次商用条款。
2. 装不上或识别差：Windows **语音识别** zh-CN 语言包（离线，质量一般）。
3. 有 Azure Speech 密钥再升云端流式识别（不占本地 GPU，依赖网络）。

不要把 faster-whisper 和大模型塞进和 UnrealEditor 同一 GPU，除非 D0 之后 `stat unit` 证明有余量。

PTT 比 VAD 更适合展厅：办公室噪声、手柄碰撞。VAD 可作以后选项，不作为第一刀。

---

## TTS：本仓合成，UE 在人物旁播放

凌云智不吐音频。回复是文本。

1. **首选** Windows WinRT `Windows.Media.SpeechSynthesis` + 已安装的 **zh-CN** 语音（设置 → 时间和语言 → 语音）。Sidecar 写成 WAV。
2. 没有中文语音包：装 Microsoft 中文 TTS；SAPI「Huihui Desktop」等是保底，音色旧。
3. 质量不够再上 Azure Neural（要密钥）。不要把非官方 edge-tts / 泄露 DLL 当交付。

UE 用 Slot 上的 `UAudioComponent` 播这份 WAV（Listener 已在 `VRCamera`）。**Windows 默认播放设备** 必须是 Pico 虚拟扬声器，否则参观者在显示器听、戴眼镜的人听不到。不要用 `bUISound` 绕过空间化除非实测串流耳机吃不到 3D 衰减。

口型：占位人没有 viseme 通道，本阶段只出声、出字幕，不做人脸。

---

## 按键（叠在已锁的左手 Y / B 上）

| 动作 | 行为 |
|------|------|
| 射线对准某人 + **点** 左手 Y | 无会话 → `BeginTalk`：Sidecar 打开并 **保持** Pico 录音设备；面板提示「按住 Y 说话」 |
| 会话中 **按住** 左手 Y | 开始截取 PCM（设备已经在占用，不重新 Open） |
| **松开** Y | 停截取 → ASR → `session/prompt` → 字幕 + TTS WAV 在该 Slot 播放 |
| B | `EndTalk`：停播放、关麦流、丢 Session |

同时只 1 路（L10）。第二人点 Y：忽略或面板「忙碌」。右手扳机仍只开家具。

---

## 机器验收（先于写 UE 对话代码）

在 5090 + 已连上的 Pico 上，**不启动 Unreal** 也应过：

1. 声音设置 → 录制：出现 Pico 相关设备且未禁用。没有则跑 Connect 自带 `AudioDriver*Install`。
2. 系统「声音录音机」选该设备，戴头显说话有电平。失败则 Sidecar 退 PC 麦，并在面板写当前设备名。
3. Sidecar 按子串打开同一设备，录 3 秒 WAV，能听出人声。
4. 对该 WAV 跑 SenseVoice，中文大致正确。
5. 合成一句中文 WAV，Windows 默认输出设为 Pico 扬声器，头显里听得到。

过了 1–5 再把 Y 接到 Adapter。1 失败不是引擎 bug。

---

## 明确不做

- 独立 APK / Pico SpeechService / 升引擎去接 PICO Unreal OpenXR 5.2–5.4 插件
- OpenXR 采麦
- Convai / ACE 整包数字人
- 展示中途反复开关虚拟麦设备
- 把 PCM 直接 POST 给凌云智（对方只要文本）
