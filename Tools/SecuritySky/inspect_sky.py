import unreal as u
import json
from pathlib import Path

out = Path('D:/0bay/OfficeShell/Saved/SecuritySky')
result = {}
for name, path in [('example', '/Game/HDRI_Futuristic_Technology/Example_Levels/Example_Map_11'),
                   ('office', '/Game/OfficeShell/Maps/Office_Demo_ArtMerged')]:
    u.get_editor_subsystem(u.LevelEditorSubsystem).load_level(path)
    actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
    rows = []
    for a in actors:
        if name == 'office' and not any(s in (a.get_class().get_name() + a.get_actor_label()).lower()
                                        for s in ['sky', 'fog', 'atmosphere', 'cameraactor_all']):
            continue
        row = dict(label=a.get_actor_label(), path=a.get_path_name(), cls=a.get_class().get_path_name(),
                   transform=a.get_actor_transform().export_text(), tags=[str(t) for t in a.tags], properties={})
        for prop in ['cubemap','intensity','size','projection_center','use_camera_projection','mesh','hidden']:
            try:
                value = a.get_editor_property(prop)
                row['properties'][prop] = value.get_path_name() if isinstance(value, u.Object) else str(value)
            except Exception:
                pass
        row['components'] = [dict(cls=c.get_class().get_name(), name=c.get_name()) for c in a.get_components_by_class(u.ActorComponent)]
        rows.append(row)
    result[name] = rows
    (out / 'inspection.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print('SECURITY_SKY_INSPECT_COMPLETE')
