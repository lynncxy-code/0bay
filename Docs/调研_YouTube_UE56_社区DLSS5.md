# 调研笔记：YouTube「UE 5.6 装 DLSS 5」教程

视频：[https://www.youtube.com/watch?v=zZEs68BXD-M](https://www.youtube.com/watch?v=zZEs68BXD-M)  
记录日期：2026-09-20  
性质：**社区注入**，不是 NVIDIA / Epic 官方 UE 插件。作者自己说文件来自 RenoDX Discord，杀毒会报，Windows 会拦截下载。

和本仓库关系：阶段性最终版 **禁止把这条写进交付机**（泄露 `nvngx_dlssnr.dll` + ReShade 挂钩 `UnrealEditor.exe`）。本页只沉淀视频里实际说了什么。作者全程在 **编辑器视口** 里演示，**没有** VR Preview / OpenXR / Pico。

---

## 视频在讲什么

作者之前做过 Cyberpunk 2077 的 DLSS 5 视频。有人问能不能进 Unreal Engine 5。他用 **UE 5.6**（自己有 5.6 工程）演示：先装官方 NVIDIA DLSS 插件，再把 Discord 里的 Streamline / DLSS NR / RenoDX add-on 拷进 **编辑器 exe 同目录**，用带 Add-on 的 ReShade 6.8 注入编辑器。开控制台 cvar 后，视口里挪灯光，画面像 Cyberpunk 那样被 Neural Rendering 改外观。

他声明：不对你的设备负责。

---

## 他让观众准备的文件

| 来源 | 视频里怎么说 |
|------|----------------|
| [reshade.me](https://reshade.me) | 下 **ReShade 6.8 with full add-on support**。明确说不要下普通版，要带 add-on 的那一个。 |
| NVIDIA 官网 | 下 **对应引擎版本的官方 DLSS 插件**。他先试 **UE 5.6** 那包。 |
| RenoDX Discord | 进服后左侧进 **DLSS 5** 频道，看 **置顶消息**。作者当时用 **4.6**（说 4.7 还没测）。另外再下 **DLSS NR**（`nvngx_dlssnr` 一类）和 **streamline.zip**。 |
| 杀毒 / 浏览器 | 说下载会被 flag、Chrome 会拦，他自己点 Keep。由观众自负。 |

他把上述 zip 放到一个工作文件夹里再解压。

---

## 步骤（按视频顺序）

### 1. 装官方 DLSS 插件到引擎

1. 解压 NVIDIA 下的 **UE 5.6 DLSS 插件**。
2. 取出包里的 `Plugins` 内容。
3. 打开 Epic 安装的引擎目录：`.../UE_5.6/Engine/Plugins/`。
4. 他选 **Runtime**，新建文件夹 `Nvidia`（他这台 5.6 里已经有过 Nvidia，直接覆盖粘贴）。
5. 把官方插件拷进去。

视频没有走「拷到工程 `Plugins/`」这条；他装的是 **引擎级** Runtime。

### 2. 用 ReShade 注入 UnrealEditor

1. 对 ReShade 安装程序 **以管理员运行**。
2. Browse，选引擎的编辑器：  
   `Engine/Binaries/Win64/UnrealEditor.exe`
3. API 选 **DirectX 12**。
4. 效果包跳过（他说这是给游戏用的），Next / OK。

注入目标是 **编辑器进程**，不是打包后的游戏 exe，也不是 VR 运行时。

### 3. 把 Discord 的 Streamline / NR / add-on 拷到同一 Binaries 目录

1. 解压 Discord 的 `streamline.zip`，**全部复制**（作者特别指里面有一份 DLSS NR）。
2. 粘贴到：`Engine/Binaries/Win64/`（和 `UnrealEditor.exe` 一目录）。
3. 再把置顶里的 **4.6** 那份拷进去，**覆盖** Streamline 包里较旧的同名文件。
4. 再把 `renodx-dlss5.addon64`（视频里的 RenoDX DLSS 5 add-on）拷到 **同一目录**。

### 4. 编辑器里开官方 NVIDIA 插件

打开任意 5.6 工程（他用自己的场景，不是本仓库）：

1. `Edit → Plugins`，启用他点到的：
   - DLSS Super Resolution
   - Frame Generation
   - NGX
   - Streamline（以及 **Streamline Core**）
   - Reflex（他说可能不必，但仍建议开）
   - **不要** NIS（他说那是 scaler，对不上这条管线）
2. `Project Settings → Plugins → NVIDIA`：勾上 **在 Editor Viewport 里启用 DLSS**；他自己把这一页能勾的（含 PIE）都勾了。
3. 重启编辑器若提示则重启。

### 5. 控制台打开（视频口播不精确）

按 `~`，依次把三项设为 **1**：

1. **DLSS G**（Frame Generation 一类，口播是 “DLSS G”）
2. **denoiser mode**
3. **DLSS enable**

作者说完整命令列表在 **官方 NVIDIA 插件文件夹的文档** 里，让观众自己去翻。视频 **没有** 念出完整 cvar 字符串（例如是否为 `r.NGX.DLSS.Enable`）。沉淀时不要把猜测的名字当成视频原文。

设完后他说「这就是 UE5 编辑器里的 DLSS 5」。

### 6. 他用来证明「在工作」的操作

- 退出全屏（F11），在 Outliner 里选 Key Light，实时拖强度/位置。
- 对比：强度拉到 0 时，背景红光（kicker）变弱，但仍能看见 NR 补的细节。
- 视口 Scalability：High 大约 **60 FPS**（他口头）；更低档更「crunchy」、噪点多；Epic 噪点更少。
- 结论：输入越干净（更高 scalability）NR 越好；不一定非要超高分辨率，但更高档有帮助。
- 提到 Movie Render Queue 里的 DLSS / DLAA 应能把这种质量烘出去；他的贴图当初是按 **没有 DLSS 5** 调的，所以现在可能显得过猛。

### 7. 他踩过的坑

- 用 **最新 NVIDIA 驱动**。
- 他 **卸了 NVIDIA App**，说会出问题。
- 按他步骤走「应该」能跑。

---

## 和本仓库已锁定方案的对照

| 视频实际做的 | 本仓阶段性最终版 |
|--------------|------------------|
| 注入 `UnrealEditor.exe` 的 DXGI 视口 | 交付画面是 OpenXR → Pico；ReShade 作者公开说过 **不支持 OpenXR** |
| 官方 4.x/4.5 插件 + 泄露 NR DLL + add-on | 交付机禁止泄露 NR；官方 DLSS 5 插件 2026-09 仍未上架 |
| 主视口直接开 NR（灯光一看就变） | 头显必须保持现版；NR 若进主视口，眼镜和屏幕会一起变 |
| 开了 Frame Generation cvar | 立体视图上官方 Streamline 会关 FG；VR 也不该开 FG |
| 引擎级 Plugins + Binaries 污染 | 换机/升级引擎容易把整份 5.6 装坏；也不进 Git |

若只是想让 **电脑窗口** 更好看：这条注入碰巧作用在编辑器 Present 上，头显走 OpenXR 时 **有可能** 只有窗口中招。视频 **没有验证** VR Preview。即便如此，文件来源和无签名挂钩仍与交付禁令冲突。

---

## 视频没覆盖的

- Pico / OpenXR / Spectator 1080p  
- 打包 Shipping exe（只演示编辑器）  
- 精确 cvar 全名  
- RTX 50 以外的卡（他没说卡型号；NR 官方限定 50 系，社区有改过的 DLL）  
- 与本仓 `StartEditor.bat`、Office_Demo 的任何步骤  

官方、无泄露的 UE 5.6 路径仍是：NVIDIA 开发者页的 **DLSS 4.5 插件**，且不要接进 OpenXR 主视口。见 `Docs/OfficeVR_Agent展示_开发方案.md` §7。
