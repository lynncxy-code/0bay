"""Read-only capture of the editor's current camera geometry before migration."""
import json
from pathlib import Path
import unreal

out = Path('D:/0bay/OfficeShell/Saved/CameraDynamicRestore')
out.mkdir(parents=True, exist_ok=True)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
records = []
for actor in actors:
    tags = [str(t) for t in actor.tags]
    ids = [t.split('=', 1)[1] for t in tags if t.startswith('OntoTwinInstanceId=')]
    if not ids or ids[0] not in {str(n) for n in range(17, 31)}:
        continue
    transform = actor.get_actor_transform()
    loc, rot, scale = transform.translation, transform.rotation.rotator(), transform.scale3d
    meshes = actor.get_components_by_class(unreal.StaticMeshComponent)
    records.append(dict(instance_id=ids[0], path=actor.get_path_name(), label=actor.get_actor_label(),
        actor_class=actor.get_class().get_name(), tags=tags,
        transform=dict(tx=loc.x, ty=loc.y, tz=loc.z, rx=rot.roll, ry=rot.pitch, rz=rot.yaw,
                       sx=scale.x, sy=scale.y, sz=scale.z),
        meshes=[dict(asset=m.static_mesh.get_path_name() if m.static_mesh else None,
                     materials=[x.get_path_name() if x else None for x in m.get_materials()],
                     relative_transform=str(m.get_editor_property('relative_location')),
                     collision=str(m.get_collision_enabled())) for m in meshes]))
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
data = dict(world=world.get_path_name(), cameras=records,
    managers=[dict(path=a.get_path_name(), project=a.get_editor_property('ue_project_id'),
                   use_level_geometry=a.get_editor_property('use_level_geometry'))
              for a in actors if isinstance(a, unreal.TwinSceneManager)])
(out / 'editor-before.json').write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
print('CAMERA_EXPORT', len(records), str(out))
