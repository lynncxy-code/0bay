"""Read-only scene inventory for the GrandNeoBay integration."""
import json
from pathlib import Path
import unreal as u

OUT = Path(u.Paths.project_saved_dir()) / 'GrandNeoBay'
OUT.mkdir(parents=True, exist_ok=True)
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level('/Game/OfficeShell/Maps/Office_Demo')
actors = u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
def vec(v): return [v.x, v.y, v.z]
data = []
for a in actors:
    center, extent = a.get_actor_bounds(False)
    data.append(dict(name=a.get_name(), label=a.get_actor_label(), cls=a.get_class().get_path_name(),
                     location=vec(a.get_actor_location()), rotation=str(a.get_actor_rotation()),
                     scale=vec(a.get_actor_scale3d()), center=vec(center), extent=vec(extent),
                     level=a.get_outer().get_path_name()))
(OUT / 'scene_inventory.json').write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
u.log('GNB_INSPECTION_COMPLETE actors=' + str(len(data)))
