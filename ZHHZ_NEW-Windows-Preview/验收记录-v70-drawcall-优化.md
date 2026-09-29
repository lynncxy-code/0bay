# ZHHZ_NEW v70：全高清 DrawCall 第二轮优化验收

日期：2026-09-04  
对象：`ZHHZ_NEW-Xinchuang-v70-fullhd`  
范围：Windows 单页浏览器复测，并按信创包方式交付；碰撞未启用、未新增。

## 优化边界

- 建筑和 Twin 仍为全高清源几何；不切 HLOD、不删视口内物体、不降低屏幕或 Lightmap 贴图。
- 12 张 4096 superatlas（189 个源 atlas 的双 layer）全量驻留，20/20 个高清 Twin 空间块全部加载。
- Twin 合批默认空间格由 20 m 调整为 50 m。材质、顶点布局和实例根仍作为合并条件，保留实例选择、实时 delta 更新与共面修复。
- 建筑只合并同材质、同 Lightmap atlas、同 40 m 空间格且带逐顶点解码属性的网格；本机内置机位实际减少 6 个静态提交。
- 全量 Lightmap 的 shader uniform 在首次编译上传后冻结，避免每帧重复写入；画面采样与光照计算不变。

## 单页基准对照

测试地址：`http://127.0.0.1:4195/zhhz-new.html?lightmaps=full&twinDetail=all&roam=1&camera=interior_east&collision=0`

| 项目 | v69 / 20 m | v70 / 50 m |
|---|---:|---:|
| 高清区块 | 20/20 | 20/20 |
| HLOD | 0 | 0 |
| Twin 源网格 | 3,132 | 3,132 |
| Twin 合批网格 | 2,308 | 2,164 |
| Twin 合并减少提交 | 824 | 968 |
| 可见 `renderCalls` | 2,308（样本） | 2,221（样本） |
| `renderTriangles` | 28,370,516（样本） | 28,618,059（样本） |
| 本机单页 FPS | 约 48–60 | 约 49–60 |
| Lightmap 常驻 | 12 | 12 |
| 画面质量 | 全高清 | 全高清 |

50 m 相比 20 m 的可见提交减少约 87（约 3.6%）；三角形只因视锥边界合批略有增加，未减少任何几何或纹理。FPS 受浏览器/GPU 争用影响，X300 必须在单页条件下重新测量。

## 交付启动

- Windows：双击 `启动ZHHZ_NEW.cmd`。
- 信创/类 Linux：`bash 启动ZHHZ_NEW.sh`（需要 Node.js；脚本不安装软件、不启用碰撞）。
- 默认 URL 带 `lightmaps=full&twinDetail=all&roam=1&collision=0`，不会下载或渲染 HLOD。

## 结果

本轮代码检查、单页 20/20 高清加载、无错误日志、双屏视频和人物/路线功能均通过；最终压缩包 SHA-256 见包内 `SHA256SUMS.txt` 及交付目录记录。
