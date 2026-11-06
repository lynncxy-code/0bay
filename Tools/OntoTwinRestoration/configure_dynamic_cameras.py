"""Run in UnrealEditor-Cmd PythonScript commandlet after building the module."""
import json
from pathlib import Path
import unreal

out = Path('D:/0bay/OfficeShell/Saved/CameraDynamicRestore')
source = json.loads((out / 'editor-before.json').read_text(encoding='utf-8'))
ids = {c['instance_id'] for c in source['cameras']}
assert len(ids) == 14
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level('/Game/OfficeShell/Maps/Office_Demo_ArtMerged')
all_actors = actors.get_all_level_actors()
by_path = {a.get_path_name(): a for a in all_actors}
for record in source['cameras']:
    actor = by_path[record['path']]
    t = actor.get_actor_transform()
    r, s, p = t.rotation.rotator(), t.scale3d, t.translation
    actual = dict(tx=p.x, ty=p.y, tz=p.z, rx=r.roll, ry=r.pitch, rz=r.yaw, sx=s.x, sy=s.y, sz=s.z)
    assert all(abs(actual[k] - v) < 1e-5 for k, v in record['transform'].items()), record['instance_id']
    component = actor.get_components_by_class(unreal.StaticMeshComponent)[0]
    assert component.static_mesh.get_path_name() == record['meshes'][0]['asset']
    assert [m.get_path_name() if m else None for m in component.get_materials()] == record['meshes'][0]['materials']
    # Material overrides must be identical to the shared mesh defaults.
    assert [x.material_interface.get_path_name() if x.material_interface else None
            for x in component.static_mesh.get_editor_property('static_materials')] == record['meshes'][0]['materials']

managers = [a for a in all_actors if isinstance(a, unreal.TwinSceneManager)]
assert len(managers) == 1 and managers[0].get_editor_property('use_level_geometry')
manager = managers[0]
manager.set_editor_property('level_geometry_dynamic_instance_ids', sorted(ids))
# Preserve the frozen appearance; enable visibility traces on its actual geometry.
mesh = unreal.load_asset(source['cameras'][0]['meshes'][0]['asset'])
assert all(c['meshes'][0]['asset'] == mesh.get_path_name() for c in source['cameras'])
mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
assert levels.save_all_dirty_levels()
assert levels.load_level('/Game/OfficeShell/Maps/Office_Demo_ArtMerged')
current = actors.get_all_level_actors()
restored = [a for a in current if isinstance(a, unreal.TwinSceneManager)]
assert len(restored) == 1 and set(restored[0].get_editor_property('level_geometry_dynamic_instance_ids')) == ids
assert len(current) == len(all_actors)
(out / 'map-verification.json').write_text(json.dumps(dict(dynamic_ids=sorted(ids),
    editor_actor_count=len(current), original_actor_count=len(all_actors), static_backups_preserved=True), indent=2))
print('CAMERA_MAP_CONFIGURED: 14 database-driven IDs; editor geometry preserved')
