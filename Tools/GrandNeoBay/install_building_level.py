"""Create only GrandNeoBay assets, then add its always-loaded sublevel to Office_Demo.

Existing office actor transforms/properties are not edited. The original main map
is backed up before execution. Re-running updates our owned actor only.
"""
import json
import traceback
from pathlib import Path
import unreal as u

PROJECT = Path(__file__).resolve().parents[2]
OUT = PROJECT / 'Saved/GrandNeoBay'
OUT.mkdir(parents=True, exist_ok=True)
ASSETS = '/Game/GrandNeoBay/Interaction'
SUB = '/Game/OfficeShell/Maps/L_GrandNeoBay_Building'
MAIN = '/Game/OfficeShell/Maps/Office_Demo'
lib = u.EditorAssetLibrary
tools = u.AssetToolsHelpers.get_asset_tools()
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
report = {'status': 'running'}

def prop(o, key, value): o.set_editor_property(key, value)
def vec(v): return [v.x, v.y, v.z]

def material(name, color, translucent=False, glow=1):
    path = ASSETS + '/' + name
    if lib.does_asset_exist(path): return lib.load_asset(path)
    m = tools.create_asset(name, ASSETS, u.Material, u.MaterialFactoryNew())
    prop(m, 'two_sided', True)
    prop(m, 'shading_model', u.MaterialShadingModel.MSM_UNLIT)
    if translucent: prop(m, 'blend_mode', u.BlendMode.BLEND_TRANSLUCENT)
    edit = u.MaterialEditingLibrary
    col = edit.create_material_expression(m, u.MaterialExpressionConstant3Vector, -400, 0)
    prop(col, 'constant', u.LinearColor(*color, 1))
    strength = edit.create_material_expression(m, u.MaterialExpressionScalarParameter, -400, 160)
    prop(strength, 'parameter_name', 'Glow'); prop(strength, 'default_value', glow)
    mult = edit.create_material_expression(m, u.MaterialExpressionMultiply, -150, 0)
    edit.connect_material_expressions(col, '', mult, 'A')
    edit.connect_material_expressions(strength, '', mult, 'B')
    edit.connect_material_property(mult, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
    if translucent:
        opacity = edit.create_material_expression(m, u.MaterialExpressionScalarParameter, -350, 300)
        prop(opacity, 'parameter_name', 'Opacity'); prop(opacity, 'default_value', .22)
        edit.connect_material_property(opacity, '', u.MaterialProperty.MP_OPACITY)
    edit.recompile_material(m)
    lib.save_loaded_asset(m)
    return m

def import_mesh(name):
    path = ASSETS+'/'+name
    task = u.AssetImportTask()
    prop(task, 'filename', str(PROJECT/'Art/GrandNeoBay/Floor09'/(name+'.fbx')))
    prop(task, 'destination_path', ASSETS); prop(task, 'destination_name', name)
    prop(task, 'automated', True); prop(task, 'save', True)
    prop(task, 'replace_existing', True)
    options = u.FbxImportUI()
    prop(options, 'import_mesh', True); prop(options, 'import_materials', False)
    prop(options, 'import_textures', False); prop(options, 'import_as_skeletal', False)
    prop(options, 'automated_import_should_detect_type', False)
    prop(options, 'mesh_type_to_import', u.FBXImportType.FBXIT_STATIC_MESH)
    prop(options.static_mesh_import_data, 'combine_meshes', True)
    prop(options.static_mesh_import_data, 'auto_generate_collision', False)
    prop(task, 'options', options); prop(task, 'factory', u.FbxFactory())
    tools.import_asset_tasks([task])
    sm = lib.load_asset(path)
    assert isinstance(sm, u.StaticMesh), path
    body = sm.get_editor_property('body_setup')
    prop(body, 'collision_trace_flag', u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    lib.save_loaded_asset(sm)
    return sm

try:
    actual = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir())).resolve()
    assert actual == PROJECT.resolve(), str(actual)
    volume = import_mesh('SM_GNB_Floor09_Volume')
    outline = import_mesh('SM_GNB_Floor09_Outline')
    volume_material = material('M_GNB_FloorGlass', (.03, .68, .76), True, 1.5)
    edge_material = material('M_GNB_FloorOutline', (.08, .8, .95), False, 2.5)
    obsolete_base = ASSETS + '/M_GNB_Base'
    if lib.does_asset_exist(obsolete_base):
        assert lib.delete_asset(obsolete_base), 'Could not remove obsolete base material'
    if lib.does_asset_exist(SUB): assert levels.load_level(SUB)
    else: assert levels.new_level(SUB)
    experience_class = u.load_class(None, '/Script/OfficeHost.GrandNeoBayExperience')
    assert experience_class
    existing = [a for a in actors.get_all_level_actors() if a.get_class() == experience_class]
    assert len(existing) <= 1, 'Duplicate GrandNeoBay experience actors'
    experience = existing[0] if existing else actors.spawn_actor_from_class(
        experience_class, u.Vector(3275, -3080, 480), u.Rotator(yaw=90))
    # Repair only the first draft's pitch/yaw positional-argument mismatch.
    if abs(experience.get_actor_rotation().pitch - 90) < .1:
        experience.set_actor_rotation(u.Rotator(yaw=90), False)
    experience.set_actor_label('GNB_BuildingExperience - move this actor to align building')
    experience.set_folder_path('GrandNeoBay')
    prop(experience, 'overview_distance', 22000.0)
    experience.get_editor_property('building').set_child_actor_class(lib.load_blueprint_class('/Game/GrandNeoBay/BP_GrandNeoBay'))
    experience.get_editor_property('floor_volume').set_static_mesh(volume)
    experience.get_editor_property('floor_volume').set_material(0, volume_material)
    experience.get_editor_property('floor_outline').set_static_mesh(outline)
    experience.get_editor_property('floor_outline').set_material(0, edge_material)
    report['experience_location_cm'] = vec(experience.get_actor_location())
    report['floor_world_location_cm'] = vec(experience.get_editor_property('floor_root').get_world_location())
    assert abs(report['floor_world_location_cm'][2] - 4320) < 1, report['floor_world_location_cm']
    for sm in (volume, outline):
        bounds = sm.get_bounding_box()
        report[sm.get_name()] = {'min_cm': vec(bounds.min), 'max_cm': vec(bounds.max)}
    assert levels.save_current_level()
    assert levels.load_level(MAIN)
    world = u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    streaming = u.GameplayStatics.get_streaming_level(world, SUB)
    if not streaming:
        streaming = u.EditorLevelUtils.add_level_to_world(world, SUB, u.LevelStreamingAlwaysLoaded)
    assert streaming, 'Could not add building sublevel'
    assert levels.set_current_level_by_name('Office_Demo')
    assert levels.save_current_level()
    lib.save_directory(ASSETS, only_if_is_dirty=True, recursive=True)
    report.update(status='complete', main_map=MAIN, building_sublevel=SUB,
                  streaming_class=streaming.get_class().get_name(),
                  office_actor_transforms_modified=False,
                  runtime_entry='F9 (no visible prompt)')
    u.log('GNB_INSTALL_COMPLETE')
except Exception:
    report.update(status='failed', error=traceback.format_exc())
    u.log_error(report['error'])
    raise
finally:
    (OUT/'install_report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
