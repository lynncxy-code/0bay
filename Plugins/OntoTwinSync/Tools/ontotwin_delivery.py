"""Windows UE editor companion. Standard library only; no backend mutation.

Source identity protocol must match OntoTwinSync.Build.cs. Delivery is an
editable source project, not a packaged executable or a server installer.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import threading
import time
import urllib.parse
import urllib.request
import uuid
import zipfile


SKIP_DIRS = {"Intermediate", "Saved", "DerivedDataCache", ".git", ".vs", "__pycache__"}
MAX_PACKAGE = 512 * 1024 * 1024


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def write_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def inside(path, parent):
    return Path(path).resolve().is_relative_to(Path(parent).resolve())


def source_hash(plugin):
    root = Path(plugin)
    files = [p for p in (root / "Source").rglob("*") if p.is_file()]
    files.append(root / "OntoTwinSync.uplugin")
    # C# ordinal ordering is UTF-16; plugin source names are ASCII today.
    files.sort(key=lambda p: p.relative_to(root).as_posix().encode("utf-16-be"))
    lines = "".join(f"{p.relative_to(root).as_posix()}\0{digest(p)}\n" for p in files)
    return hashlib.sha256(lines.encode("utf-8")).hexdigest()


def plugin_info(plugin):
    root = Path(plugin)
    descriptor = read_json(root / "OntoTwinSync.uplugin")
    source_files = [p for p in (root / "Source").rglob("*") if p.is_file()]
    resource_files = []
    for name in ("Content", "Resources", "Config", "Tools"):
        if (root / name).is_dir():
            resource_files.extend((p, p.relative_to(root)) for p, _ in inventory(root / name))
    resource_files.sort(key=lambda item: item[1].as_posix())
    tracked_files = source_files + [root / "OntoTwinSync.uplugin"] + [p for p, _ in resource_files]
    updated_at = max((p.stat().st_mtime for p in tracked_files), default=0)
    resource_hash = hashlib.sha256("".join(f"{rel.as_posix()}\0{digest(p)}\n" for p, rel in resource_files).encode("utf-8")).hexdigest()
    return {"path": str(root.absolute()), "resolved_path": str(root.resolve()), "resource_hash": resource_hash,
            "version": descriptor.get("VersionName", "未标记"), "source_hash": source_hash(root),
            "last_updated": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(updated_at)) if updated_at else "未知",
            "shared": root.absolute() != root.resolve()}


def version_report(context, mother=None):
    current = plugin_info(context["plugin"])
    loaded = context.get("loaded_hash", "")
    master = plugin_info(mother) if mother else None
    status = "运行构建与磁盘源码一致" if loaded == current["source_hash"] else "源码已变化：需编译并重启 UE"
    if not loaded:
        status = "当前运行构建未知（旧插件未提供标识）"
    return {"project": context["project"], "current": current, "loaded_hash": loaded,
            "runtime_status": status, "mother": master,
            "mother_status": ("与本机母本源码、资源一致" if master["source_hash"] == current["source_hash"] and master["resource_hash"] == current["resource_hash"]
                              else "与本机母本不同（不代表母本更新，更新前检查差异）") if master else "未指定本机母本"}


def inventory(root, omit_binaries=False):
    """Follow junctions into real files, reject cycles and secret files explicitly."""
    root = Path(root)
    result = []

    def walk(folder, ancestors):
        resolved = folder.resolve(strict=True)
        if resolved in ancestors:
            raise ValueError(f"目录链接循环，停止导出：{folder}")
        for p in sorted(folder.iterdir()):
            if p.is_dir():
                if p.name in SKIP_DIRS or (omit_binaries and p.name == "Binaries"):
                    continue
                walk(p, ancestors | {resolved})
            elif p.is_file():
                if p.name.startswith(".env") or p.suffix.lower() in {".pem", ".key", ".pfx"}:
                    raise ValueError(f"发现可能的凭据文件，请先移出交付范围：{p}")
                result.append((p, p.relative_to(root)))
            else:
                raise ValueError(f"无法读取文件或链接：{p}")
    walk(root, set())
    return result


def ensure_destination(destination, roots):
    destination = Path(destination).absolute()
    if destination.exists():
        raise ValueError("输出目录已存在。请选择一个全新的目录，不覆盖原文件。")
    for root in roots:
        if inside(destination, root) or inside(root, destination):
            raise ValueError(f"输出目录不能包含或位于源工程/链接目录内：{root}")
    return destination


def api_bytes(base, route, limit=MAX_PACKAGE):
    parsed = urllib.parse.urlsplit(base)
    if parsed.scheme not in {"http", "https"} or not parsed.hostname or parsed.username or parsed.password:
        raise ValueError("后端地址必须是无用户名密码的 http(s) 地址")
    # Local backends must not accidentally go through the machine's HTTP proxy.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({})) if parsed.hostname in {"localhost", "127.0.0.1", "::1"} else urllib.request.build_opener()
    with opener.open(base.rstrip("/") + route, timeout=120) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError("响应超过安全大小上限")
    return data


def datasets(base):
    records = json.loads(api_bytes(base, "/api/v2/ontology/datasets", 4 * 1024 * 1024))
    if not isinstance(records, list):
        raise ValueError("后端数据集列表格式不正确")
    return records


def fetch_package(base, dataset_id, ue_id):
    if not ue_id:
        raise ValueError("没有明确的 UE 项目编号。请填写场景管理器的 UEProjectId，不能按当前激活数据集猜测。")
    matches = [d for d in datasets(base) if d.get("id") == dataset_id]
    if len(matches) != 1 or matches[0].get("bound_ue_project_id") != ue_id:
        raise ValueError("所选数据集未绑定当前 UE 编号，停止导出。请在平台确认绑定关系。")
    data = api_bytes(base, "/api/v2/ontology/datasets/" + urllib.parse.quote(dataset_id, safe="") + "/package")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        if sum(info.file_size for info in archive.infolist()) > MAX_PACKAGE:
            raise ValueError("数据集包解压后超过安全大小限制")
        if archive.testzip() is not None:
            raise ValueError("数据集包校验失败")
        manifest = json.loads(archive.read("manifest.json"))
        if manifest.get("delivery_mode") != "project_assets":
            raise ValueError("后端返回的不是包含附件的完整交付包，请先更新后端")
        if (manifest.get("source") or {}).get("dataset_id") != dataset_id:
            raise ValueError("返回的数据集包编号与所选数据集不一致")
        payload = archive.read("project.json")
        if hashlib.sha256(payload).hexdigest() != (manifest.get("payload") or {}).get("sha256"):
            raise ValueError("数据集内容校验失败")
        for attachment in manifest.get("attachments", []):
            if hashlib.sha256(archive.read(attachment["file"])).hexdigest() != attachment["sha256"]:
                raise ValueError("数据集附件校验失败")
    return data


def _additional_plugin_entries(project, descriptor):
    """Materialize UE's external plugin roots under the delivery Plugins folder."""
    raw_roots = descriptor.get("AdditionalPluginDirectories") or []
    if isinstance(raw_roots, str):
        raw_roots = [raw_roots]
    if not isinstance(raw_roots, list):
        raise ValueError("工程的 AdditionalPluginDirectories 格式不正确")
    entries = []
    roots = []
    for raw in raw_roots:
        value = str(raw or "").strip()
        if not value:
            continue
        candidate = Path(value)
        if not candidate.is_absolute():
            candidate = project.parent / candidate
        try:
            candidate = candidate.resolve(strict=True)
        except FileNotFoundError as exc:
            raise ValueError(f"外部插件目录不存在：{value}") from exc
        if not candidate.is_dir():
            raise ValueError(f"外部插件路径不是目录：{candidate}")
        # A project-local directory is already covered by the normal project scan.
        if inside(candidate, project.parent / "Plugins"):
            continue
        direct_plugin = any(p.is_file() and p.suffix.lower() == ".uplugin" for p in candidate.iterdir())
        prefix = Path("Plugins") / candidate.name if direct_plugin else Path("Plugins")
        for source, relative in inventory(candidate):
            entries.append((source, prefix / relative))
        roots.append(candidate)
    return entries, roots


def _merge_entries(local_entries, extra_entries):
    merged = []
    seen = {}
    plugins = {}
    for source, relative in [*local_entries, *extra_entries]:
        if relative.suffix.lower() == ".uplugin":
            name = relative.stem.casefold()
            if name in plugins:
                raise ValueError(f"交付目录存在重复插件：{relative.stem}（{plugins[name]} 与 {source}）")
            plugins[name] = source
        key = relative.as_posix().casefold()
        if key in seen:
            raise ValueError(f"交付目录存在重复插件文件：{relative}（来源：{seen[key]} 与 {source}）")
        seen[key] = source
        merged.append((source, relative))
    return merged


def project_files(project):
    project = Path(project).resolve(strict=True)
    if project.suffix.lower() != ".uproject":
        raise ValueError("请选择 .uproject 文件")
    desc = read_json(project)
    if desc.get("AdditionalRootDirectories"):
        raise ValueError("工程引用了额外资源目录，需先迁入工程 Content/Art 后再导出。")
    entries = [(project, Path(project.name))]
    for name in ("Config", "Content", "Source", "Build", "Plugins", "Art"):
        folder = project.parent / name
        if folder.exists():
            entries.extend((p, Path(name) / rel) for p, rel in inventory(folder))
    external_entries, _ = _additional_plugin_entries(project, desc)
    return project, desc, _merge_entries(entries, external_entries)


def export_project(context, destination, base, dataset_id, ue_id, source_only=False, progress=lambda s: None):
    project, desc, entries = project_files(context["project"])
    _, external_plugin_roots = _additional_plugin_entries(project, desc)
    plugin = Path(context["plugin"])
    plugin_descriptors = [(src, rel) for src, rel in entries
                          if rel.name == "OntoTwinSync.uplugin"]
    if len(plugin_descriptors) != 1 or plugin_descriptors[0][0].parent.resolve() != plugin.resolve():
        raise ValueError("导出范围中的 OntoTwinSync 与当前运行插件不一致，停止导出。")
    roots = {project.parent}
    roots.update(external_plugin_roots)
    for p, _ in entries:
        roots.add(p.parent.resolve())
    output = ensure_destination(destination, roots)
    progress("检查数据集与附件…")
    package = None if source_only else fetch_package(base, dataset_id, ue_id)
    size = sum(p.stat().st_size for p, _ in entries) + len(package or b"")
    output.parent.mkdir(parents=True, exist_ok=True)
    if shutil.disk_usage(output.parent).free < size + 256 * 1024 * 1024:
        raise ValueError("目标磁盘剩余空间不足（预留 256 MB）")
    # Exclusive creation protects against concurrent exports and accidental overwrite.
    output.mkdir()
    marker = output / "EXPORT_INCOMPLETE.txt"
    marker.write_text("交付尚未完成，请勿使用；失败后可保留排查或手动删除此独立目录。", encoding="utf-8")
    records = []
    try:
        for index, (src, relative) in enumerate(entries):
            dst = output / relative
            dst.parent.mkdir(parents=True, exist_ok=True)
            before = src.stat()
            source_hash_before = digest(src)
            is_project_descriptor = relative == Path(project.name)
            if is_project_descriptor:
                if read_json(src) != desc:
                    raise ValueError("导出期间工程配置变化，请重新导出")
                delivery_desc = dict(desc)
                delivery_desc.pop("AdditionalPluginDirectories", None)
                dst.write_text(json.dumps(delivery_desc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            else:
                shutil.copyfile(src, dst)
            copied_hash = digest(dst)
            if src.stat().st_mtime_ns != before.st_mtime_ns:
                raise ValueError(f"复制期间文件发生变化，请停止保存后重新导出：{relative}")
            if not is_project_descriptor and copied_hash != source_hash_before:
                raise ValueError(f"复制期间文件发生变化，请停止保存后重新导出：{relative}")
            record = {"path": relative.as_posix(), "size": dst.stat().st_size, "sha256": copied_hash}
            if is_project_descriptor:
                record["source_sha256"] = source_hash_before
                record["transformed"] = True
            records.append(record)
            if index % 100 == 0:
                progress(f"复制并校验 {index + 1}/{len(entries)}：{relative}")
        # Catch additions/removals and late edits, not only edits during a single copy.
        _, _, final_entries = project_files(project)
        if {(str(p.resolve()), str(r)) for p, r in final_entries} != {(str(p.resolve()), str(r)) for p, r in entries}:
            raise ValueError("导出期间源工程文件列表变化，请重新导出")
        progress("复核源文件未变化…")
        for (src, _), record in zip(entries, records):
            expected_source_hash = record.get("source_sha256", record["sha256"])
            if digest(src) != expected_source_hash:
                raise ValueError(f"导出期间源文件变化：{record['path']}")
        delivery = output / "OntoTwinDelivery"
        delivery.mkdir()
        if package is not None:
            (delivery / "project.otdataset").write_bytes(package)
        versions = version_report(context)
        plugin_descriptors = []
        for _, relative in entries:
            if relative.suffix == ".uplugin":
                descriptor = read_json(output / relative)
                plugin_descriptors.append({"path": relative.as_posix(), "version": descriptor.get("VersionName"),
                                           "dependencies": descriptor.get("Plugins", [])})
        manifest = {"schema": 1, "created_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
                    "kind": "editable-source-project", "engine_association": desc.get("EngineAssociation"),
                    "versions": versions, "dataset_id": None if source_only else dataset_id,
                    "ue_project_id": ue_id, "dataset_sha256": hashlib.sha256(package).hexdigest() if package else None,
                    "files": records, "requires_rebuild": True, "plugin_descriptors": plugin_descriptors,
                    "notes": ["这是可编辑工程，不是已打包程序。请在目标机重编译，不承诺复制的旧 DLL 与源码一致。",
                              "引擎级第三方插件不会自动携带，请按依赖清单安装。",
                              "外部视频网关、实时接口、绝对路径引用与商业资源授权需另行配置。"],
                    "project_plugin_requirements": desc.get("Plugins", []),
                    "external_plugin_directories": [str(path) for path in external_plugin_roots]}
        write_json(delivery / "manifest.json", manifest)
        (delivery / "交付说明.md").write_text(
            "# OntoTwin 可编辑工程交付\n\n"
            "1. 安装与源工程一致的 UE、C++ 编译工具链和引擎级第三方插件。\n"
            "2. 为工程生成项目文件并编译 Development Editor，再打开工程；不要把旧 DLL 当成已验证的新版本。\n"
            "3. 启动兼容版本 OntoTwin 后端，在“导入数据集包”导入 project.otdataset，并确认绑定此工程的 UE 编号。\n"
            "4. 核对后端地址、视频网关和其他外部服务，验收漫游、空间跳转、底图及业务交互。\n\n"
            + ("本次明确选择仅工程，未带数据集/底图。\n" if source_only else "数据集及其附件随 project.otdataset 提供；不是仅拷 UE 插件。\n")
            + ("外部插件目录已复制到本工程 Plugins，并已移除交付副本中的 AdditionalPluginDirectories。\n" if external_plugin_roots else "")
            + "\n所有工程内目录链接已展开为真实文件。版本及逐文件校验见 manifest.json。\n", encoding="utf-8")
        marker.unlink()
        progress(f"导出完成：{output}（目标机需编译与验收）")
        return manifest
    except Exception:
        progress(f"导出未完成，原工程未改动。诊断副本保留在：{output}")
        raise


def editors_running():
    if os.name != "nt":
        raise ValueError("更新安全检查仅支持 Windows")
    result = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode:
        raise ValueError("无法确认 UE 已关闭，停止更新")
    return b"unrealeditor" in result.stdout.lower()


def update_copy(plugin, mother, progress=lambda s: None):
    target, master = Path(plugin).absolute(), Path(mother).resolve(strict=True)
    if target.resolve() == master:
        raise ValueError("当前工程共享母本，不需要复制更新；源码变化后请关闭 UE、编译并重新打开。")
    if target.resolve() != target or inside(master, target) or inside(target, master):
        raise ValueError("目标是链接目录或与母本嵌套，禁止替换；请先明确插件来源。")
    plugin_info(target)
    plugin_info(master)
    if editors_running():
        raise ValueError("请先关闭所有 UE 编辑器，再点击更新。本窗口可以保持打开。")
    token = uuid.uuid4().hex[:10]
    # Keep backups outside Plugins: otherwise UE discovers duplicate .uplugin files.
    backups = target.parent.parent / "Saved" / "OntoTwinPluginBackups"
    backups.mkdir(parents=True, exist_ok=True)
    stage, backup = backups / ("prepared-" + token), backups / ("previous-" + token)
    stage.mkdir()
    entries = inventory(master, omit_binaries=True)
    copied = []
    for src, rel in entries:
        dest = stage / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dest)
        copied_hash = digest(dest)
        if digest(src) != copied_hash:
            raise ValueError("母本在复制期间变化，未替换原插件")
        copied.append((src, copied_hash))
    if source_hash(stage) != source_hash(master):
        raise ValueError("母本源码变化，请重新更新")
    if [rel for _, rel in entries] != [rel for _, rel in inventory(master, omit_binaries=True)]:
        raise ValueError("母本文件列表变化，请重新更新")
    if any(digest(src) != value for src, value in copied):
        raise ValueError("母本资源变化，请重新更新")
    if editors_running():
        raise ValueError("检测到 UE 重新打开，未替换原插件")
    target.rename(backup)
    try:
        stage.rename(target)
    except Exception:
        backup.rename(target)
        raise
    progress(f"已更新源码与资源；旧插件完整保存在 {backup}。下一步：编译并重启 UE。")
    return backup


def rebuild(context, progress=lambda s: None):
    if editors_running():
        raise ValueError("请先关闭所有 UE 编辑器，再编译更新")
    engine = Path(context["engine"])
    runtimes = sorted((engine / "Binaries/ThirdParty/DotNet").glob("*/win-x64/dotnet.exe"))
    ubt = engine / "Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll"
    if not runtimes or not ubt.is_file():
        raise ValueError("找不到 UE 内置编译工具。请确认完整安装 UE 5.6 与 Visual Studio C++ 工具链。")
    project = Path(context["project"]).resolve(strict=True)
    targets = sorted((project.parent / "Source").glob("*Editor.Target.cs"))
    if len(targets) > 1:
        raise ValueError("工程有多个 Editor 编译目标，请在 Visual Studio 明确选择后编译")
    target = targets[0].name.removesuffix(".Target.cs") if targets else "UnrealEditor"
    log_dir = project.parent / "Saved/OntoTwinDelivery"
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / ("build-" + time.strftime("%Y%m%d-%H%M%S") + ".log")
    args = [str(runtimes[-1]), str(ubt), target, "Win64", "Development", "-Project=" + str(project), "-WaitMutex", "-NoHotReloadFromIDE"]
    progress("开始编译，请等待。日志：" + str(log_path))
    with log_path.open("w", encoding="utf-8") as log:
        with subprocess.Popen(args, cwd=engine / "Source", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              encoding="utf-8", errors="replace", creationflags=subprocess.CREATE_NO_WINDOW) as process:
            for line in process.stdout:
                log.write(line)
                log.flush()
                progress(line.rstrip())
            code = process.wait()
    if code:
        raise ValueError(f"编译失败（{code}），未宣称更新完成。日志：{log_path}")
    progress("编译成功。请重新打开 UE，再从工具菜单检查版本（本窗口运行标识仍是启动时快照）。")


def _window_base(context, title, subtitle, size="720x520"):
    import tkinter as tk
    from tkinter import ttk
    root = tk.Tk()
    root.title(title)
    root.geometry(size)
    root.minsize(620, 420)
    style = ttk.Style(root)
    style.theme_use("clam")
    style.configure(".", font=("Microsoft YaHei UI", 10))
    style.configure("TFrame", background="#f5f5f5")
    style.configure("TLabel", background="#f5f5f5", foreground="#222222")
    outer = ttk.Frame(root, padding=18)
    outer.pack(fill="both", expand=True)
    ttk.Label(outer, text=title, font=("Microsoft YaHei UI", 16, "bold")).pack(anchor="w")
    ttk.Label(outer, text=subtitle, wraplength=680).pack(anchor="w", pady=(6, 12))
    ttk.Label(outer, text="当前工程：" + context["project"], wraplength=680).pack(anchor="w", pady=(0, 12))
    return root, outer


def _start_job(root, report, status, events, busy, work):
    if busy[0]:
        return
    busy[0] = True
    status.set("处理中…")
    def worker():
        try:
            work()
        except Exception as exc:
            events.put(("error", str(exc)))
        finally:
            events.put(("done", None))
    threading.Thread(target=worker, daemon=False).start()


def _attach_job_loop(root, report, status, events, busy):
    def log(message):
        report.configure(state="normal")
        report.insert("end", message + "\n")
        report.see("end")
        report.configure(state="disabled")
    def poll():
        try:
            while True:
                kind, value = events.get_nowait()
                if kind == "log":
                    log(value); status.set(value)
                elif kind == "error":
                    log("未完成：" + value); status.set("未完成，请查看原因")
                elif kind == "done":
                    busy[0] = False
                elif kind == "datasets":
                    yield_value = value
                    root._ontotwin_datasets = yield_value
                    root.event_generate("<<OntoTwinDatasets>>", when="tail")
        except queue.Empty:
            pass
        root.after(150, poll)
    poll()
    return log


def launch_version_gui(context):
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    root, outer = _window_base(context, "OntoTwin · 插件版本", "只查看和更新当前工程的 OntoTwin 插件。导出交付请从工具菜单单独打开。")
    config_file = Path(context["project"]).parent / "Saved" / "OntoTwinDelivery" / "preferences.json"
    settings = read_json(config_file) if config_file.exists() else {}
    inferred = Path(context["plugin"]).resolve()
    default_mother = str(inferred) if (inferred.parent.parent.parent / "backend").is_dir() else ""
    mother = tk.StringVar(value=settings.get("mother", default_mother))
    events, busy = queue.Queue(), [False]
    def row(label, variable, browse=False):
        frame = ttk.Frame(outer); frame.pack(fill="x", pady=5)
        ttk.Label(frame, text=label, width=16).pack(side="left")
        ttk.Entry(frame, textvariable=variable).pack(side="left", fill="x", expand=True)
        if browse:
            ttk.Button(frame, text="选择目录", command=lambda: variable.set(filedialog.askdirectory(parent=root) or variable.get())).pack(side="right", padx=(8, 0))
    row("本机母本", mother, True)
    report = tk.Text(outer, height=13, wrap="word", background="#ffffff", relief="flat", padx=12, pady=10)
    report.pack(fill="both", expand=True, pady=(10, 6)); report.configure(state="disabled")
    status = tk.StringVar(value="就绪")
    ttk.Label(outer, textvariable=status, wraplength=680).pack(anchor="w")
    log = _attach_job_loop(root, report, status, events, busy)
    def run(work):
        config_file.parent.mkdir(parents=True, exist_ok=True)
        write_json(config_file, {"mother": mother.get().strip()})
        _start_job(root, report, status, events, busy, work)
    def check():
        def work():
            r = version_report(context, mother.get().strip() or None); c = r["current"]
            events.put(("log", f"当前插件：{c['version']}\n最后更新：{c['last_updated']}\n运行状态：{r['runtime_status']}\n插件来源：{'共享母本' if c['shared'] else '独立副本'}\n源码编号：{c['source_hash'][:12]}\n资源编号：{c['resource_hash'][:12]}\n母本状态：{r['mother_status']}"))
            if r["mother"]:
                events.put(("log", f"本机母本最后更新：{r['mother']['last_updated']}"))
        run(work)
    def update():
        if not mother.get().strip():
            messagebox.showwarning("需要母本", "请先选择本机母本插件目录。", parent=root); return
        if messagebox.askokcancel("同步插件", "请先关闭所有 UE 编辑器。\n将把母本同步到当前工程，并保留旧副本备份。", parent=root):
            run(lambda: update_copy(context["plugin"], mother.get().strip(), lambda m: events.put(("log", m))))
    def compile_update():
        if messagebox.askokcancel("重新编译", "请先关闭所有 UE 编辑器，再重新编译当前工程插件。", parent=root):
            run(lambda: rebuild(context, lambda m: events.put(("log", m))))
    buttons = ttk.Frame(outer); buttons.pack(fill="x", pady=(8, 0))
    ttk.Button(buttons, text="检查版本", command=check).pack(side="left", padx=(0, 8))
    ttk.Button(buttons, text="同步母本", command=update).pack(side="left", padx=(0, 8))
    ttk.Button(buttons, text="重新编译", command=compile_update).pack(side="left")
    root.protocol("WM_DELETE_WINDOW", lambda: root.destroy() if not busy[0] else messagebox.showinfo("正在处理", "请等待当前操作结束。", parent=root))
    check(); root.mainloop()


def launch_export_gui(context):
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    root, outer = _window_base(context, "OntoTwin · 导出交付工程", "把当前工程和已绑定数据集导出成一份可换机继续编辑的副本。")
    base = tk.StringVar(value=context.get("backend") or "http://localhost:5000")
    ue_id = tk.StringVar(value=context.get("ue_id", "")); selected = tk.StringVar()
    output = tk.StringVar(value=str(Path(context["project"]).parent.parent / (Path(context["project"]).stem + "_delivery_" + time.strftime("%Y%m%d_%H%M%S"))))
    events, busy, records = queue.Queue(), [False], []
    def row(label, variable, browse=False):
        frame = ttk.Frame(outer); frame.pack(fill="x", pady=5)
        ttk.Label(frame, text=label, width=16).pack(side="left")
        ttk.Entry(frame, textvariable=variable).pack(side="left", fill="x", expand=True)
        if browse:
            ttk.Button(frame, text="选择目录", command=lambda: variable.set(filedialog.askdirectory(parent=root) or variable.get())).pack(side="right", padx=(8, 0))
    row("后端地址", base); row("UE 项目编号", ue_id); row("输出目录", output, True)
    frame = ttk.Frame(outer); frame.pack(fill="x", pady=5)
    ttk.Label(frame, text="数据集", width=16).pack(side="left")
    combo = ttk.Combobox(frame, textvariable=selected, state="readonly"); combo.pack(side="left", fill="x", expand=True)
    report = tk.Text(outer, height=10, wrap="word", background="#ffffff", relief="flat", padx=12, pady=10)
    report.pack(fill="both", expand=True, pady=(10, 6)); report.configure(state="disabled")
    status = tk.StringVar(value="先点击“读取数据集”")
    ttk.Label(outer, textvariable=status, wraplength=680).pack(anchor="w")
    log = _attach_job_loop(root, report, status, events, busy)
    def run(work): _start_job(root, report, status, events, busy, work)
    def load():
        def work():
            found = [d for d in datasets(base.get().strip()) if d.get("id") != "demo"]
            labels = [f"{d['id']} | {d.get('name', '')} | UE: {d.get('bound_ue_project_id') or '未绑定'}" for d in found]
            events.put(("datasets", (found, labels)))
        run(work)
    def export():
        if not selected.get() or not output.get().strip():
            messagebox.showwarning("信息不完整", "请先读取并选择数据集，再确认输出目录。", parent=root); return
        if messagebox.askokcancel("开始导出", "请先保存工程，并在导出期间不要修改文件。\n输出目录必须是全新的目录。", parent=root):
            dataset_id = selected.get().split(" | ")[0]
            run(lambda: export_project(context, output.get().strip(), base.get().strip(), dataset_id, ue_id.get().strip(), False, lambda m: events.put(("log", m))))
    def on_datasets(_=None):
        found, labels = getattr(root, "_ontotwin_datasets", ([], [])); records[:] = found; combo["values"] = labels
        matches = [i for i, d in enumerate(found) if ue_id.get().strip() and d.get("bound_ue_project_id") == ue_id.get().strip()]
        if len(matches) == 1:
            combo.current(matches[0]); status.set("已按 UE 项目编号匹配数据集")
            d = found[matches[0]]
            log(f"已读取数据集：{d.get('name') or '未命名'}\n数据集 ID：{d.get('id', '未知')}\n绑定 UE：{d.get('bound_ue_project_id') or '未绑定'}\n不会改变平台当前激活项。")
        elif labels:
            status.set("请选择与当前 UE 项目编号绑定的数据集")
            log(f"已读取 {len(found)} 个数据集；尚未自动选定。请从下拉框选择后再导出。")
        else:
            status.set("没有可用数据集")
            log("未读取到可用数据集。")
    root.bind("<<OntoTwinDatasets>>", on_datasets)
    buttons = ttk.Frame(outer); buttons.pack(fill="x", pady=(8, 0))
    ttk.Button(buttons, text="读取数据集", command=load).pack(side="left", padx=(0, 8))
    ttk.Button(buttons, text="开始导出", command=export).pack(side="left")
    root.protocol("WM_DELETE_WINDOW", lambda: root.destroy() if not busy[0] else messagebox.showinfo("正在处理", "请等待当前操作结束。", parent=root))
    root.mainloop()


def launch_gui(context, mode=None):
    selected_mode = mode or context.get("mode") or "version"
    if selected_mode == "export":
        launch_export_gui(context)
    else:
        launch_version_gui(context)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--context", required=True)
    parser.add_argument("--mode", choices=("version", "export"), default=None)
    args = parser.parse_args()
    launch_gui(read_json(args.context), args.mode)
