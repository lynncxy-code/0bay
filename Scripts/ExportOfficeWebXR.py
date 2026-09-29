"""Export Office_Demo metadata and glTF for OfficeWebXR.

Automatically disables Nanite and generates Lightmap UVs on meshes used
by the loaded map. Materials and textures stay on the assets.

Editor Output Log:
    py F:/Office_VR/Scripts/ExportOfficeWebXR.py

Or double-click:
    F:\\Office_VR\\ExportOfficeWebXR.bat
"""

from __future__ import annotations

import json
import os
import traceback

import unreal


ARCHITECTURE_TOKENS = (
    "SM_Wall", "SM_Floor", "SM_Ceiling", "SM_Column", "SM_Facade", "SM_Roof",
    "SM_Cove", "SM_Skirting", "SM_Core", "SM_Door", "SM_Interior", "SM_Exterior",
    "SM_Partition", "SM_Feature", "Sky", "Landscape", "BP_OfficeShell",
    "WorldDataLayers", "Brush", "Light", "Camera", "PlayerStart", "SkyAtmosphere",
)

SKIP_EXPORT_TOKENS = (
    "zerobay",
    "skyatmosphere",
    "skylight",
    "exponentialheightfog",
    "volumetriccloud",
    "landscape",
    "postprocessvolume",
    "directionallight",
    "rectlight",
    "pointlight",
    "spotlight",
    "playerstart",
    "cameraactor",
    "cinecamera",
    "recastnavmesh",
    "navmeshbounds",
    "abstractnavdata",
    "worldsettings",
    "lodactor",
    "defaultphysicsvolume",
)

PROJECT_ROOT = os.path.normpath(os.path.join(unreal.Paths.project_dir()))
DATA_DIR = os.path.join(PROJECT_ROOT, "OfficeWebXR", "data")
PDF_JSON = os.path.join(PROJECT_ROOT, "Content", "Art", "家具_PDF匹配", "完整清单.json")


def _log(message):
    unreal.log("OfficeWebXR: {}".format(message))


def _warn(message):
    unreal.log_warning("OfficeWebXR: {}".format(message))


def _write_json(path, payload):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    _log("wrote {}".format(path))


def _infer_category(name):
    if any(token in name for token in ("电视", "TV", "冰箱")):
        return "电器"
    if any(token in name for token in ("椅", "沙发", "凳")):
        return "座椅"
    if any(token in name for token in ("桌", "几", "吧台")):
        return "桌台"
    if any(token in name for token in ("柜", "文件")):
        return "储物"
    if "电话亭" in name:
        return "隔间"
    if any(token in name for token in ("植物", "绿萝")):
        return "绿化"
    return "家具"


def _format_mm(values):
    if not isinstance(values, list) or len(values) < 3:
        return "待现场复核"
    return "{:.0f} × {:.0f} × {:.0f} mm".format(values[0], values[1], values[2])


def _asset_id(catalog_id):
    return "OFC-{:03d}".format(int(catalog_id))


def _builtin_items():
    return [
        {
            "catalogId": 101,
            "displayName": "会议室电视",
            "category": "电器",
            "room": "接待区 / 会议室",
            "dimensions": "约 122 × 14 × 81 cm",
            "material": "塑料壳体 + 玻璃屏（CC0 场景资产）",
            "assetId": "OFC-101",
            "systemName": "办公家具",
            "maintenanceCycle": "12个月（示例）",
            "nextMaintenance": "2027-03-15（示例）",
            "description": "普通无支架电视，用于会议投屏示意。一期仅支持查看属性。",
            "matchKeys": ["SM_TV_Plain", "TV_Plain_NoStand", "电视", "tv-stand", "TV", "PDF_101"],
        },
        {
            "catalogId": 102,
            "displayName": "双人电话亭",
            "category": "隔间",
            "room": "打印茶水区",
            "dimensions": "约 1600 mm 宽封闭舱",
            "material": "玻璃 + 织物 + 金属型材",
            "assetId": "OFC-102",
            "systemName": "办公家具",
            "maintenanceCycle": "12个月（示例）",
            "nextMaintenance": "2027-03-15（示例）",
            "description": "隔音通话舱。一期仅支持查看属性，不可开关门。",
            "matchKeys": ["PhoneBooth", "电话亭", "PB26", "PDF_102"],
        },
        {
            "catalogId": 103,
            "displayName": "绿萝盆栽",
            "category": "绿化",
            "room": "公共区域",
            "dimensions": "盆栽尺度",
            "material": "植物 + 陶盆",
            "assetId": "OFC-103",
            "systemName": "办公家具",
            "maintenanceCycle": "12个月（示例）",
            "nextMaintenance": "2027-03-15（示例）",
            "description": "场景绿化点缀。",
            "matchKeys": ["Pothos", "绿萝", "PDF_103"],
        },
        {
            "catalogId": 0,
            "displayName": "办公室 VR 操作说明",
            "category": "系统",
            "room": "公司大门 / 出生点",
            "dimensions": "-",
            "material": "-",
            "assetId": "OFC-HELP",
            "systemName": "VR 交互",
            "maintenanceCycle": "-",
            "nextMaintenance": "-",
            "description": "左摇杆或 WASD 行走。右摇杆左右或 Q/E 转身。右摇杆下推对准地面后松开瞬移。右手扳机或鼠标左键查看属性。B / 鼠标右键关闭面板。",
            "matchKeys": ["HelpPlaque", "操作说明"],
        },
    ]


def _load_catalog():
    items = {}
    if os.path.isfile(PDF_JSON):
        with open(PDF_JSON, "r", encoding="utf-8") as handle:
            records = json.load(handle)
        for record in records:
            catalog_id = int(record.get("id", 0))
            name = record.get("name") or ""
            file_name = record.get("file") or ""
            keys = []
            if file_name:
                keys.append(os.path.splitext(os.path.basename(file_name))[0])
            keys.extend([name, "PDF_{:02d}".format(catalog_id)])
            items[catalog_id] = {
                "catalogId": catalog_id,
                "displayName": name,
                "category": _infer_category(name),
                "room": record.get("room") or "办公室",
                "dimensions": _format_mm(record.get("dimensions_mm") or []),
                "material": record.get("material_note") or "面层按场景近似，未对色卡",
                "assetId": _asset_id(catalog_id),
                "systemName": "办公家具",
                "maintenanceCycle": "12个月（示例）",
                "nextMaintenance": "2027-03-15（示例）",
                "description": record.get("geometry_note") or "",
                "matchKeys": keys,
            }
    for item in _builtin_items():
        items[item["catalogId"]] = item
    return [items[key] for key in sorted(items)]


def _is_architecture(name):
    return any(token in name for token in ARCHITECTURE_TOKENS)


def _should_export_actor(actor):
    name = _collect_names(actor).lower()
    return not any(token in name for token in SKIP_EXPORT_TOKENS)


def _collect_names(actor):
    parts = [actor.get_name(), actor.get_actor_label()]
    try:
        comps = actor.get_components_by_class(unreal.StaticMeshComponent)
    except Exception:
        comps = []
    for comp in comps:
        parts.append(comp.get_name())
        mesh = comp.static_mesh
        if mesh:
            parts.append(mesh.get_name())
            parts.append(str(mesh.get_path_name()))
    return " ".join(parts)


def _find_item(combined, catalog):
    best = None
    best_score = 0
    for item in catalog:
        score = 0
        display = item.get("displayName") or ""
        if display and display in combined:
            score = max(score, len(display) + 10)
        for key in item.get("matchKeys") or []:
            if key and key in combined:
                score = max(score, len(key))
        if score > best_score:
            best_score = score
            best = item
    if not best or best_score < 2:
        return None
    return best


def _player_start(world):
    starts = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerStart)
    if not starts:
        return {"x": 0, "y": 0, "z": 92}
    loc = starts[0].get_actor_location()
    return {"x": loc.x, "y": loc.y, "z": loc.z}


def _is_nanite_mesh(mesh):
    if not mesh:
        return False
    try:
        settings = mesh.get_editor_property("nanite_settings")
        return bool(settings and settings.get_editor_property("enabled"))
    except Exception:
        return False


def _list_nanite(world):
    blockers = []
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        try:
            comps = actor.get_components_by_class(unreal.StaticMeshComponent)
        except Exception:
            continue
        for comp in comps:
            mesh = comp.static_mesh
            if _is_nanite_mesh(mesh):
                blockers.append({
                    "actor": actor.get_actor_label(),
                    "mesh": mesh.get_name(),
                    "path": str(mesh.get_path_name()),
                })
    return blockers


def _export_gltf(world, dest, actors):
    exporter = getattr(unreal, "GLTFExporter", None)
    options_cls = getattr(unreal, "GLTFExportOptions", None)
    if exporter is None or options_cls is None:
        raise RuntimeError(
            "GLTFExporter plugin is not available. Enable the engine plugin 'GLTFExporter' and restart the editor."
        )
    options = options_cls()
    # Keep centimeters. OfficeWebXR scales the scene by 0.01 at runtime.
    _set_opt(options, "export_uniform_scale", 1.0)
    _set_opt(options, "export_unlit_materials", True)
    _set_opt(options, "export_proxy_materials", False)
    _set_opt(options, "export_lights", False)
    _set_opt(options, "export_cameras", False)
    _set_opt(options, "export_hidden_in_game", False)
    _set_opt(options, "adjust_normalmaps", True)
    _set_opt(options, "export_texture_transforms", True)
    _set_opt(options, "export_clear_coat_materials", True)
    _set_opt(options, "export_emissive_strength", True)
    try:
        options.set_editor_property("texture_image_format", unreal.GLTFTextureImageFormat.PNG)
    except Exception:
        pass
    # SIMPLE bakes the UE material onto a quad, which is what we need when UV0 is degenerate.
    try:
        options.set_editor_property("bake_material_inputs", unreal.GLTFMaterialBakeMode.SIMPLE)
    except Exception:
        try:
            options.set_editor_property("bake_material_inputs", unreal.GLTFMaterialBakeMode.USE_MESH_DATA)
        except Exception:
            pass
    try:
        bake_size = options.get_editor_property("default_material_bake_size")
        bake_size.set_editor_property("x", 1024)
        bake_size.set_editor_property("y", 1024)
        bake_size.set_editor_property("b_auto_detect", True)
        options.set_editor_property("default_material_bake_size", bake_size)
    except Exception as error:
        _warn("bake size: {}".format(error))
    selected = unreal.Array(unreal.Actor)
    for actor in actors:
        selected.append(actor)
    result = exporter.export_to_gltf(world, dest, options, selected)
    if result is False:
        raise RuntimeError("GLTFExporter.export_to_gltf returned False for {}".format(dest))
    _log("exported {}".format(dest))


def _set_opt(options, name, value):
    try:
        options.set_editor_property(name, value)
    except Exception:
        pass


def _prepare_web_meshes(world):
    """Disable Nanite in memory only so GLTFExporter can write triangles.

    Does not save .uasset files. Materials, vertex colors and texture
    assignments stay on the mesh. Lightmap UVs are optional and skipped
    here because StaticMeshEditorSubsystem in 5.6 has no
    set_generate_lightmap_u_vs.
    """
    sm_sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    seen = set()
    converted = []
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        try:
            comps = actor.get_components_by_class(unreal.StaticMeshComponent)
        except Exception:
            continue
        for comp in comps:
            mesh = comp.static_mesh
            if not mesh:
                continue
            path = str(mesh.get_path_name())
            if path in seen:
                continue
            seen.add(path)
            try:
                settings = sm_sub.get_nanite_settings(mesh)
                if settings and settings.get_editor_property("enabled"):
                    settings.set_editor_property("enabled", False)
                    sm_sub.set_nanite_settings(mesh, settings, True)
                    converted.append(path)
                    _log("disabled Nanite (memory only): {}".format(mesh.get_name()))
            except Exception as error:
                _warn("Nanite {}: {}".format(path, error))
            try:
                build = sm_sub.get_lod_build_settings(mesh, 0)
                build.set_editor_property("generate_lightmap_u_vs", True)
                build.set_editor_property("dst_lightmap_index", 1)
                sm_sub.set_lod_build_settings(mesh, 0, build)
            except Exception:
                pass
    _log("prepared {} unique static meshes for WebXR".format(len(converted)))
    return converted


def _collision_actors(world):
    chosen = []
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        name = _collect_names(actor)
        if any(token in name for token in ("SM_Floor", "SM_Wall", "Floor", "Wall")):
            if "zerobay" in name.lower():
                continue
            chosen.append(actor)
    return chosen


def main():
    os.makedirs(DATA_DIR, exist_ok=True)
    editor_sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    try:
        level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        world = editor_sub.get_editor_world()
        path = str(world.get_path_name()) if world else ""
        if "Office_Demo" not in path:
            _log("loading /Game/OfficeShell/Maps/Office_Demo")
            level_sub.load_level("/Game/OfficeShell/Maps/Office_Demo")
    except Exception as error:
        _warn("load_level: {}".format(error))
    world = editor_sub.get_editor_world()
    catalog = _load_catalog()
    _write_json(os.path.join(DATA_DIR, "inspect-catalog.json"), catalog)

    spawn = {
        "gltfUp": "y",
        "units": "cm",
        "alignWorldToXrOrigin": True,
        "playerStart": _player_start(world),
        "capsuleRadius": 34,
        "capsuleHalfHeight": 92,
        "eyeHeightFromFloor": 164,
        "walkSpeed": 220,
        "snapTurnDegrees": 45,
        "laserMaxDistance": 1200,
        "map": str(world.get_path_name()),
    }
    _write_json(os.path.join(DATA_DIR, "spawn.json"), spawn)

    bindings = []
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        if actor.get_class() and "PlayerStart" in actor.get_class().get_name():
            continue
        combined = _collect_names(actor)
        if _is_architecture(combined):
            continue
        item = _find_item(combined, catalog)
        if not item:
            continue
        bindings.append({"nodeName": actor.get_actor_label(), "catalogId": item["catalogId"]})
    _write_json(os.path.join(DATA_DIR, "inspect-bindings.json"), {"bindings": bindings})
    _log("inspect bindings = {}".format(len(bindings)))

    converted = _prepare_web_meshes(world)
    blockers = _list_nanite(world)
    _write_json(os.path.join(DATA_DIR, "nanite-blockers.json"), blockers)
    report = {
        "catalogItems": len(catalog),
        "bindings": len(bindings),
        "preparedMeshes": len(converted),
        "naniteBlockers": len(blockers),
        "architectureGlb": False,
        "collisionGlb": False,
        "notes": [],
    }

    if blockers:
        report["notes"].append(
            "Some meshes are still Nanite after auto-prepare. They will be skipped. See nanite-blockers.json."
        )
        _warn("{} meshes still Nanite; exporting the rest".format(len(blockers)))

    nanite_paths = {item.get("path") for item in blockers}
    actors = []
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        skip = False
        try:
            comps = actor.get_components_by_class(unreal.StaticMeshComponent)
        except Exception:
            comps = []
        for comp in comps:
            mesh = comp.static_mesh
            if mesh and str(mesh.get_path_name()) in nanite_paths:
                skip = True
                break
        if not skip and _should_export_actor(actor):
            actors.append(actor)
    try:
        _export_gltf(world, os.path.join(DATA_DIR, "architecture.glb"), actors)
        report["architectureGlb"] = True
    except Exception as error:
        report["notes"].append(str(error))
        _warn(str(error))

    try:
        floors = _collision_actors(world)
        if floors:
            _export_gltf(world, os.path.join(DATA_DIR, "collision.glb"), floors)
            report["collisionGlb"] = True
        else:
            report["notes"].append("No floor/wall actors found for collision.glb")
    except Exception as error:
        report["notes"].append(str(error))
        _warn(str(error))

    report["notes"].append("Lightmap PNGs are not copied automatically. Bake in a static-lighting session, then put PNGs in OfficeWebXR/data/lightmaps/.")
    _write_json(os.path.join(DATA_DIR, "export-report.json"), report)
    _log("export finished")


if __name__ == "__main__":
    try:
        main()
    except Exception:
        unreal.log_error("OfficeWebXR export failed:\n{}".format(traceback.format_exc()))
        raise
